#include "Archive.hpp"

#include <sys/stat.h>

#include <cstring>
#include <fstream>
#include <vector>

#include <boost/filesystem.hpp>
#include <string_utils/string_utils.hpp>

#include "../../EncodedVariableInterpreter.hpp"
#include "../../spdlog_with_specializations.hpp"
#include "../../streaming_compression/passthrough/Compressor.hpp"
#include "../../streaming_compression/zstd/Compressor.hpp"
#include "../../Utils.hpp"
#include "../ArchiveMetadata.hpp"
#include "../Constants.hpp"

using clp::string_utils::wildcard_match_unsafe;
using std::string;
using std::unordered_set;
using std::vector;

namespace glt::streaming_archive::reader {
void Archive::open(string const& path) {
    // Determine whether path is file or directory
    struct stat path_stat = {};
    char const* path_c_str = path.c_str();
    if (0 != stat(path_c_str, &path_stat)) {
        SPDLOG_ERROR("Failed to stat {}, errno={}", path_c_str, errno);
        throw OperationFailed(ErrorCode_errno, __FILENAME__, __LINE__);
    }
    if (!S_ISDIR(path_stat.st_mode)) {
        SPDLOG_ERROR("{} is not a directory", path_c_str);
        throw OperationFailed(ErrorCode_Unsupported, __FILENAME__, __LINE__);
    }
    m_path = path;

    // Read the metadata file
    string metadata_file_path = path + '/' + cMetadataFileName;
    archive_format_version_t format_version{};
    try {
        FileReader file_reader;
        file_reader.open(metadata_file_path);
        ArchiveMetadata const metadata{file_reader};
        format_version = metadata.get_archive_format_version();
        file_reader.close();
    } catch (TraceableException& traceable_exception) {
        auto error_code = traceable_exception.get_error_code();
        if (ErrorCode_errno == error_code) {
            SPDLOG_CRITICAL(
                    "streaming_archive::reader::Archive: Failed to read archive metadata file "
                    "{} at {}:{} - errno={}",
                    metadata_file_path.c_str(),
                    traceable_exception.get_filename(),
                    traceable_exception.get_line_number(),
                    errno
            );
        } else {
            SPDLOG_CRITICAL(
                    "streaming_archive::reader::Archive: Failed to read archive metadata file "
                    "{} at {}:{} - error={}",
                    metadata_file_path.c_str(),
                    traceable_exception.get_filename(),
                    traceable_exception.get_line_number(),
                    error_code
            );
        }
        throw;
    }

    // Check archive matches format version
    if (cArchiveFormatVersion != format_version) {
        SPDLOG_ERROR("streaming_archive::reader::Archive: Archive uses an unsupported format.");
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__);
    }

    auto metadata_db_path = boost::filesystem::path(path) / cMetadataDBFileName;
    if (false == boost::filesystem::exists(metadata_db_path)) {
        SPDLOG_ERROR(
                "streaming_archive::reader::Archive: Metadata DB not found: {}",
                metadata_db_path.string()
        );
        throw OperationFailed(ErrorCode_FileNotFound, __FILENAME__, __LINE__);
    }
    m_metadata_db.open(metadata_db_path.string());

    // Open log-type dictionary
    string logtype_dict_path = m_path;
    logtype_dict_path += '/';
    logtype_dict_path += cLogTypeDictFilename;
    string logtype_segment_index_path = m_path;
    logtype_segment_index_path += '/';
    logtype_segment_index_path += cLogTypeSegmentIndexFilename;
    m_logtype_dictionary.open(logtype_dict_path, logtype_segment_index_path);

    // Open variables dictionary
    string var_dict_path = m_path;
    var_dict_path += '/';
    var_dict_path += cVarDictFilename;
    string var_segment_index_path = m_path;
    var_segment_index_path += '/';
    var_segment_index_path += cVarSegmentIndexFilename;
    m_var_dictionary.open(var_dict_path, var_segment_index_path);

    // Open segment manager
    m_segments_dir_path = m_path;
    m_segments_dir_path += '/';
    m_segments_dir_path += cSegmentsDirname;
    m_segments_dir_path += '/';

    // Open segment list
    string segment_list_path = m_segments_dir_path;
    segment_list_path += cSegmentListFilename;

    // Set invalid segment ID
    m_current_segment_id = INT64_MAX;

    update_valid_segment_ids();
    load_filename_dict();
}

void Archive::close() {
    // close GLT
    m_segment.close();
    m_message_order_table.close();

    m_logtype_dictionary.close();
    m_var_dictionary.close();
    m_segments_dir_path.clear();
    m_metadata_db.close();
    m_path.clear();

    m_filename_dict.clear();
}

void Archive::refresh_dictionaries() {
    m_logtype_dictionary.read_new_entries();
    m_var_dictionary.read_new_entries();
}

ErrorCode Archive::open_file(File& file, MetadataDB::FileIterator const& file_metadata_ix) {
    auto const segment_id = file_metadata_ix.get_segment_id();
    if (segment_id != m_current_segment_id) {
        if (m_current_segment_id != INT64_MAX) {
            m_segment.close();
            m_message_order_table.close();
        }
        ErrorCode error_code = m_segment.try_open(m_segments_dir_path, segment_id);
        if (error_code != ErrorCode_Success) {
            m_segment.close();
            return error_code;
        }
        error_code = m_message_order_table.try_open(m_segments_dir_path, segment_id);
        if (error_code != ErrorCode_Success) {
            m_message_order_table.close();
            m_segment.close();
            return error_code;
        }
        m_current_segment_id = segment_id;
    }
    return file.open_me(m_logtype_dictionary, file_metadata_ix, m_segment, m_message_order_table);
}

void Archive::close_file(File& file) {
    file.close_me();
}

void Archive::reset_file_indices(File& file) {
    file.reset_indices();
}

LogTypeDictionaryReader const& Archive::get_logtype_dictionary() const {
    return m_logtype_dictionary;
}

VariableDictionaryReader const& Archive::get_var_dictionary() const {
    return m_var_dictionary;
}

bool Archive::get_next_message(File& file, Message& msg) {
    return file.get_next_message(msg);
}

bool
Archive::decompress_message(File& file, Message const& compressed_msg, string& decompressed_msg) {
    decompressed_msg.clear();

    // Build original message content
    logtype_dictionary_id_t const logtype_id = compressed_msg.get_logtype_id();
    auto const& logtype_entry = m_logtype_dictionary.get_entry(logtype_id);
    if (!EncodedVariableInterpreter::decode_variables_into_message(
                logtype_entry,
                m_var_dictionary,
                compressed_msg.get_vars(),
                decompressed_msg
        ))
    {
        SPDLOG_ERROR(
                "streaming_archive::reader::Archive: Failed to decompress variables from "
                "logtype id {}",
                compressed_msg.get_logtype_id()
        );
        return false;
    }

    // Determine which timestamp pattern to use
    auto const& timestamp_patterns = file.get_timestamp_patterns();
    if (!timestamp_patterns.empty()
        && compressed_msg.get_message_number()
                   >= timestamp_patterns[file.get_current_ts_pattern_ix()].first)
    {
        while (true) {
            if (file.get_current_ts_pattern_ix() >= timestamp_patterns.size() - 1) {
                // Already at last timestamp pattern
                break;
            }
            auto next_patt_start_message_num
                    = timestamp_patterns[file.get_current_ts_pattern_ix() + 1].first;
            if (compressed_msg.get_message_number() < next_patt_start_message_num) {
                // Not yet time for next timestamp pattern
                break;
            }
            file.increment_current_ts_pattern_ix();
        }
        timestamp_patterns[file.get_current_ts_pattern_ix()].second.insert_formatted_timestamp(
                compressed_msg.get_ts_in_milli(),
                decompressed_msg
        );
    }

    return true;
}

void Archive::decompress_empty_directories(string const& output_dir) {
    boost::filesystem::path output_dir_path = boost::filesystem::path(output_dir);

    string path;
    auto ix_ptr = m_metadata_db.get_empty_directory_iterator();
    for (auto& ix = *ix_ptr; ix.has_next(); ix.next()) {
        ix.get_path(path);
        auto empty_directory_path = output_dir_path / path;
        auto error_code = create_directory_structure(empty_directory_path.string(), 0700);
        if (ErrorCode_Success != error_code) {
            SPDLOG_ERROR(
                    "Failed to create directory structure {}, errno={}",
                    empty_directory_path.string().c_str(),
                    errno
            );
            throw OperationFailed(error_code, __FILENAME__, __LINE__);
        }
    }
}

// GLT specific functions
bool Archive::get_next_message_in_logtype_table(Message& msg) {
    return m_logtype_table_manager.get_next_row(msg);
}

void Archive::open_logtype_table_manager(size_t segment_id) {
    std::string segment_path = m_segments_dir_path + std::to_string(segment_id);
    m_logtype_table_manager.open(segment_path);
}

void Archive::close_logtype_table_manager() {
    m_logtype_table_manager.close();
}

std::string Archive::get_file_name(file_id_t file_id) const {
    if (file_id >= m_filename_dict.size()) {
        SPDLOG_ERROR("file id {} out of bound", file_id);
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }
    return m_filename_dict[file_id];
}

void Archive::load_filename_dict() {
#if USE_PASSTHROUGH_COMPRESSION
    FileReader filename_dict_reader;
#elif USE_ZSTD_COMPRESSION
    streaming_compression::zstd::Decompressor filename_dict_reader;
#else
    static_assert(false, "Unsupported compression mode.");
#endif
    std::string filename_dict_path = m_path + '/' + cFileNameDictFilename;
    filename_dict_reader.open(filename_dict_path);
    std::string file_name;

    while (true) {
        auto errorcode = filename_dict_reader.try_read_to_delimiter('\n', false, false, file_name);
        if (errorcode == ErrorCode_Success) {
            m_filename_dict.push_back(file_name);
        } else if (errorcode == ErrorCode_EndOfFile) {
            break;
        } else {
            SPDLOG_ERROR("Failed to read from {}, errno={}", filename_dict_path.c_str(), errno);
            throw OperationFailed(errorcode, __FILENAME__, __LINE__);
        }
    }
    filename_dict_reader.close();
}

void Archive::update_valid_segment_ids() {
    m_valid_segment_id.clear();
    // Better question here is why we produce 0 size segment
    size_t segment_count = 0;
    while (true) {
        std::string segment_file_path = m_segments_dir_path + "/" + std::to_string(segment_count);
        if (!boost::filesystem::exists(segment_file_path)) {
            break;
        }
        boost::system::error_code boost_error_code;
        size_t segment_file_size
                = boost::filesystem::file_size(segment_file_path, boost_error_code);
        if (boost_error_code) {
            SPDLOG_ERROR(
                    "streaming_archive::reader::Segment: Unable to obtain file size for segment: "
                    "{}",
                    segment_file_path.c_str()
            );
            SPDLOG_ERROR(
                    "streaming_archive::reader::Segment: {}",
                    boost_error_code.message().c_str()
            );
            throw ErrorCode_Failure;
        }
        if (segment_file_size != 0) {
            m_valid_segment_id.push_back(segment_count);
        }
        segment_count++;
    }
}

bool Archive::find_message_matching_with_logtype_query_from_combined(
        std::vector<LogtypeQuery> const& logtype_query,
        Message& msg,
        bool& wildcard,
        Query const& query,
        size_t left_boundary,
        size_t right_boundary
) {
    auto& combined_tables = m_logtype_table_manager.combined_tables();
    while (true) {
        // break if there's no next message
        if (!combined_tables.get_next_message_partial(msg, left_boundary, right_boundary)) {
            break;
        }

        if (query.timestamp_is_in_search_time_range(msg.get_ts_in_milli())) {
            for (auto const& possible_sub_query : logtype_query) {
                if (possible_sub_query.matches_vars(msg.get_vars())) {
                    // Message matches completely, so set remaining properties
                    wildcard = possible_sub_query.get_wildcard_flag();
                    combined_tables.get_remaining_message(msg, left_boundary, right_boundary);
                    return true;
                }
            }
        }
        // if there is no match, skip next row
        combined_tables.skip_next_row();
    }
    return false;
}

bool Archive::find_message_matching_with_logtype_query(
        std::vector<LogtypeQuery> const& logtype_query,
        Message& msg,
        bool& wildcard,
        Query const& query
) {
    while (true) {
        if (!m_logtype_table_manager.get_next_row(msg)) {
            break;
        }

        if (query.timestamp_is_in_search_time_range(msg.get_ts_in_milli())) {
            // that means we need to loop through every loop. that takes time.
            for (auto const& possible_sub_query : logtype_query) {
                if (possible_sub_query.matches_vars(msg.get_vars())) {
                    // Message matches completely, so set remaining properties
                    wildcard = possible_sub_query.get_wildcard_flag();
                    return true;
                }
            }
        }
    }
    return false;
}

void Archive::find_message_matching_with_logtype_query_optimized(
        std::vector<LogtypeQuery> const& logtype_query,
        std::vector<size_t>& matched_rows,
        std::vector<bool>& wildcard,
        Query const& query
) {
    epochtime_t ts;
    auto& logtype_table = m_logtype_table_manager.logtype_table();
    size_t num_row = logtype_table.get_num_row();
    size_t num_column = logtype_table.get_num_column();
    std::vector<encoded_variable_t> vars_to_load(num_column);
    for (size_t row_ix = 0; row_ix < num_row; row_ix++) {
        m_logtype_table_manager.peek_next_ts(ts);
        if (query.timestamp_is_in_search_time_range(ts)) {
            // that means we need to loop through every loop. that takes time.
            for (auto const& possible_sub_query : logtype_query) {
                logtype_table.get_next_row(
                        vars_to_load,
                        possible_sub_query.get_begin_ix(),
                        possible_sub_query.get_end_ix()
                );
                if (possible_sub_query.matches_vars(vars_to_load)) {
                    // Message matches completely, so set remaining properties
                    wildcard.push_back(possible_sub_query.get_wildcard_flag());
                    matched_rows.push_back(row_ix);
                    // don't need to look into other sub-queries as long as there is a match
                    break;
                }
            }
        }
        m_logtype_table_manager.skip_row();
    }
}

size_t Archive::decompress_messages_and_output(
        logtype_dictionary_id_t logtype_id,
        std::vector<epochtime_t>& ts,
        std::vector<file_id_t>& id,
        std::vector<encoded_variable_t>& vars,
        std::vector<bool>& wildcard_required,
        Query const& query,
        OutputFunc output_func,
        void* output_func_arg
) {
    auto const& logtype_entry = m_logtype_dictionary.get_entry(logtype_id);
    size_t num_vars = logtype_entry.get_num_variables();
    size_t const total_matches = wildcard_required.size();
    std::string decompressed_msg;
    // The sole purpose of this dummy message is to call output func
    Message dummy_compressed_msg;
    size_t matches = 0;
    for (size_t ix = 0; ix < total_matches; ix++) {
        decompressed_msg.clear();

        // first decompress the message with fixed time stamp
        size_t vars_offset = num_vars * ix;
        if (!EncodedVariableInterpreter::decode_variables_into_message_with_offset(
                    logtype_entry,
                    m_var_dictionary,
                    vars,
                    decompressed_msg,
                    vars_offset
            ))
        {
            SPDLOG_ERROR(
                    "streaming_archive::reader::Archive: Failed to decompress variables from "
                    "logtype id {}",
                    logtype_id
            );
            throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
        }
        if (ts[ix] != 0) {
            std::string const fixed_timestamp_pattern = "%Y-%m-%d %H:%M:%S,%3";
            TimestampPattern ts_pattern(0, fixed_timestamp_pattern);
            ts_pattern.insert_formatted_timestamp(ts[ix], decompressed_msg);
        }
        // Perform wildcard match if required
        // Check if:
        // - Sub-query requires wildcard match, or
        // - no subqueries exist and the search string is not a match-all
        if ((query.contains_sub_queries() && wildcard_required[ix])
            || (query.contains_sub_queries() == false && query.search_string_matches_all() == false
            ))
        {
            bool matched = wildcard_match_unsafe(
                    decompressed_msg,
                    query.get_search_string(),
                    query.get_ignore_case() == false
            );
            if (!matched) {
                continue;
            }
        }
        matches++;
        std::string const& orig_file_path = get_file_name(id[ix]);
        // Print match
        output_func(orig_file_path, dummy_compressed_msg, decompressed_msg, output_func_arg);
    }
    return matches;
}

bool Archive::decompress_message_with_fixed_timestamp_pattern(
        Message const& compressed_msg,
        std::string& decompressed_msg
) {
    decompressed_msg.clear();

    // Build original message content
    logtype_dictionary_id_t const logtype_id = compressed_msg.get_logtype_id();
    auto const& logtype_entry = m_logtype_dictionary.get_entry(logtype_id);
    if (!EncodedVariableInterpreter::decode_variables_into_message(
                logtype_entry,
                m_var_dictionary,
                compressed_msg.get_vars(),
                decompressed_msg
        ))
    {
        SPDLOG_ERROR(
                "streaming_archive::reader::Archive: Failed to decompress variables from logtype "
                "id {}",
                compressed_msg.get_logtype_id()
        );
        return false;
    }
    if (compressed_msg.get_ts_in_milli() != 0) {
        std::string const fixed_timestamp_pattern = "%Y-%m-%d %H:%M:%S,%3";
        TimestampPattern ts_pattern(0, fixed_timestamp_pattern);
        ts_pattern.insert_formatted_timestamp(compressed_msg.get_ts_in_milli(), decompressed_msg);
    }
    return true;
}
}  // namespace glt::streaming_archive::reader
