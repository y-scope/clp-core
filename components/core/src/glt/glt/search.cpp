#include "search.hpp"

#include <sys/stat.h>

#include <filesystem>
#include <iostream>

#include <spdlog/sinks/stdout_sinks.h>

#include "../GlobalMySQLMetadataDB.hpp"
#include "../GlobalSQLiteMetadataDB.hpp"
#include "../Grep.hpp"
#include "../Profiler.hpp"
#include "CommandLineArguments.hpp"

using glt::combined_table_id_t;
using glt::epochtime_t;
using glt::ErrorCode;
using glt::ErrorCode_errno;
using glt::FileReader;
using glt::GlobalMetadataDB;
using glt::GlobalMetadataDBConfig;
using glt::Grep;
using glt::LogtypeQueries;
using glt::Profiler;
using glt::Query;
using glt::segment_id_t;
using glt::streaming_archive::MetadataDB;
using glt::streaming_archive::reader::Archive;
using glt::streaming_archive::reader::File;
using glt::streaming_archive::reader::Message;
using glt::TraceableException;
using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::vector;

namespace glt::glt {

/**
 * Opens the archive and reads the dictionaries
 * @param archive_path
 * @param archive_reader
 * @return true on success, false otherwise
 */
static bool open_archive(string const& archive_path, Archive& archive_reader);
/**
 * To update
 * @param queries
 * @param output_method
 * @param archive
 * @param segment_id
 * @return The total number of matches found across all files
 */
static size_t search_segments(
        vector<Query>& queries,
        CommandLineArguments::OutputMethod output_method,
        Archive& archive,
        size_t segment_id
);
/**
 * get all messages in the segment within query's time range
 * if query doesn't have a time range, outputs all messages
 * @param query
 * @param output_method
 * @param archive
 * @param segment_id
 * @return The total number of matches found across all files
 */
static size_t find_message_in_segment_within_time_range(
        Query const& query,
        CommandLineArguments::OutputMethod output_method,
        Archive& archive
);
/**
 * Prints search result to stdout in text format
 * @param orig_file_path
 * @param compressed_msg
 * @param decompressed_msg
 * @param custom_arg Unused
 */
static void print_result_text(
        string const& orig_file_path,
        Message const& compressed_msg,
        string const& decompressed_msg,
        void* custom_arg
);
/**
 * Prints search result to stdout in binary format
 * @param orig_file_path
 * @param compressed_msg
 * @param decompressed_msg
 * @param custom_arg Unused
 */
static void print_result_binary(
        string const& orig_file_path,
        Message const& compressed_msg,
        string const& decompressed_msg,
        void* custom_arg
);

/**
 * Gets an archive iterator for the given file path or for all files if the file path is empty
 * @param global_metadata_db
 * @param file_path
 * @param begin_ts
 * @param end_ts
 * @return An archive iterator
 */
static GlobalMetadataDB::ArchiveIterator* get_archive_iterator(
        GlobalMetadataDB& global_metadata_db,
        std::string const& file_path,
        epochtime_t begin_ts,
        epochtime_t end_ts
);

static GlobalMetadataDB::ArchiveIterator* get_archive_iterator(
        GlobalMetadataDB& global_metadata_db,
        std::string const& file_path,
        epochtime_t begin_ts,
        epochtime_t end_ts
) {
    if (!file_path.empty()) {
        return global_metadata_db.get_archive_iterator_for_file_path(file_path);
    } else if (begin_ts == cEpochTimeMin && end_ts == cEpochTimeMax) {
        return global_metadata_db.get_archive_iterator();
    } else {
        return global_metadata_db.get_archive_iterator_for_time_window(begin_ts, end_ts);
    }
}

static bool open_archive(string const& archive_path, Archive& archive_reader) {
    ErrorCode error_code;

    try {
        // Open archive
        archive_reader.open(archive_path);
    } catch (TraceableException& e) {
        error_code = e.get_error_code();
        if (ErrorCode_errno == error_code) {
            SPDLOG_ERROR(
                    "Opening archive failed: {}:{} {}, errno={}",
                    e.get_filename(),
                    e.get_line_number(),
                    e.what(),
                    errno
            );
            return false;
        } else {
            SPDLOG_ERROR(
                    "Opening archive failed: {}:{} {}, error_code={}",
                    e.get_filename(),
                    e.get_line_number(),
                    e.what(),
                    error_code
            );
            return false;
        }
    }

    try {
        archive_reader.refresh_dictionaries();
    } catch (TraceableException& e) {
        error_code = e.get_error_code();
        if (ErrorCode_errno == error_code) {
            SPDLOG_ERROR(
                    "Reading dictionaries failed: {}:{} {}, errno={}",
                    e.get_filename(),
                    e.get_line_number(),
                    e.what(),
                    errno
            );
            return false;
        } else {
            SPDLOG_ERROR(
                    "Reading dictionaries failed: {}:{} {}, error_code={}",
                    e.get_filename(),
                    e.get_line_number(),
                    e.what(),
                    error_code
            );
            return false;
        }
    }

    return true;
}

static bool search(
        vector<string> const& search_strings,
        CommandLineArguments& command_line_args,
        Archive& archive,
        size_t& num_matches
) {
    ErrorCode error_code;
    auto search_begin_ts = command_line_args.get_search_begin_ts();
    auto search_end_ts = command_line_args.get_search_end_ts();

    try {
        vector<Query> queries;
        bool no_queries_match = true;
        std::set<segment_id_t> ids_of_segments_to_search;
        bool is_superseding_query = false;
        for (auto const& search_string : search_strings) {
            auto query_processing_result = Grep::process_raw_query(
                    archive,
                    search_string,
                    search_begin_ts,
                    search_end_ts,
                    command_line_args.ignore_case()
            );
            if (query_processing_result.has_value()) {
                auto& query = query_processing_result.value();
                no_queries_match = false;

                if (false == query.contains_sub_queries()) {
                    // Search string supersedes all other possible search strings
                    is_superseding_query = true;
                    // Remove existing queries since they are superseded by this one
                    queries.clear();
                    // Add this query
                    queries.push_back(query);
                    // All other search strings will be superseded by this one, so break
                    break;
                }

                queries.push_back(query);

                // Add query's matching segments to segments to search
                for (auto& sub_query : query.get_sub_queries()) {
                    auto& ids_of_matching_segments = sub_query.get_ids_of_matching_segments();
                    ids_of_segments_to_search.insert(
                            ids_of_matching_segments.cbegin(),
                            ids_of_matching_segments.cend()
                    );
                }
            }
        }

        if (!no_queries_match) {
            if (is_superseding_query) {
                for (auto segment_id : archive.get_valid_segment()) {
                    archive.open_logtype_table_manager(segment_id);
                    // There should be only one query for a superceding query case
                    auto const& query = queries.at(0);
                    num_matches += find_message_in_segment_within_time_range(
                            query,
                            command_line_args.get_output_method(),
                            archive
                    );
                    archive.close_logtype_table_manager();
                }
            } else {
                for (auto segment_id : ids_of_segments_to_search) {
                    archive.open_logtype_table_manager(segment_id);
                    num_matches += search_segments(
                            queries,
                            command_line_args.get_output_method(),
                            archive,
                            segment_id
                    );
                    archive.close_logtype_table_manager();
                }
            }
            SPDLOG_DEBUG("# matches found: {}", num_matches);
        }
    } catch (TraceableException& e) {
        error_code = e.get_error_code();
        if (ErrorCode_errno == error_code) {
            SPDLOG_ERROR(
                    "Search failed: {}:{} {}, errno={}",
                    e.get_filename(),
                    e.get_line_number(),
                    e.what(),
                    errno
            );
            return false;
        } else {
            SPDLOG_ERROR(
                    "Search failed: {}:{} {}, error_code={}",
                    e.get_filename(),
                    e.get_line_number(),
                    e.what(),
                    error_code
            );
            return false;
        }
    }

    return true;
}

static size_t find_message_in_segment_within_time_range(
        Query const& query,
        CommandLineArguments::OutputMethod const output_method,
        Archive& archive
) {
    size_t num_matches = 0;

    // Setup output method
    Grep::OutputFunc output_func;
    void* output_func_arg;
    switch (output_method) {
        case CommandLineArguments::OutputMethod::StdoutText:
            output_func = print_result_text;
            output_func_arg = nullptr;
            break;
        case CommandLineArguments::OutputMethod::StdoutBinary:
            output_func = print_result_binary;
            output_func_arg = nullptr;
            break;
        default:
            SPDLOG_ERROR("Unknown output method - {}", (char)output_method);
            return num_matches;
    }
    num_matches = Grep::output_message_in_segment_within_time_range(
            query,
            SIZE_MAX,
            archive,
            output_func,
            output_func_arg
    );
    num_matches += Grep::output_message_in_combined_segment_within_time_range(
            query,
            SIZE_MAX,
            archive,
            output_func,
            output_func_arg
    );
    return num_matches;
}

static size_t search_segments(
        vector<Query>& queries,
        CommandLineArguments::OutputMethod const output_method,
        Archive& archive,
        size_t segment_id
) {
    size_t num_matches = 0;

    // Setup output method
    Grep::OutputFunc output_func;
    void* output_func_arg;
    switch (output_method) {
        case CommandLineArguments::OutputMethod::StdoutText:
            output_func = print_result_text;
            output_func_arg = nullptr;
            break;
        case CommandLineArguments::OutputMethod::StdoutBinary:
            output_func = print_result_binary;
            output_func_arg = nullptr;
            break;
        default:
            SPDLOG_ERROR("Unknown output method - {}", (char)output_method);
            return num_matches;
    }

    for (auto& query : queries) {
        query.make_sub_queries_relevant_to_segment(segment_id);
        // here convert old queries to new query type
        auto converted_logtype_based_queries = Grep::get_converted_logtype_query(query, segment_id);
        // use a vector to hold queries so they are sorted based on the ascending or descending
        // order of their size, i.e. the order they appear in the segment.
        std::vector<LogtypeQueries> single_table_queries;
        // first level index is basically combined table index
        // because we might not search through all combined tables, the first level is a map instead
        // of a vector.
        std::map<combined_table_id_t, std::vector<LogtypeQueries>> combined_table_queires;
        archive.get_logtype_table_manager().rearrange_queries(
                converted_logtype_based_queries,
                single_table_queries,
                combined_table_queires
        );

        // first search through the single variable table
        num_matches += Grep::search_segment_optimized_and_output(
                single_table_queries,
                query,
                SIZE_MAX,
                archive,
                output_func,
                output_func_arg
        );
        for (auto const& iter : combined_table_queires) {
            combined_table_id_t table_id = iter.first;
            auto const& combined_logtype_queries = iter.second;
            num_matches += Grep::search_combined_table_and_output(
                    table_id,
                    combined_logtype_queries,
                    query,
                    SIZE_MAX,
                    archive,
                    output_func,
                    output_func_arg
            );
        }
    }
    return num_matches;
}

static void print_result_text(
        string const& orig_file_path,
        Message const& compressed_msg,
        string const& decompressed_msg,
        void* custom_arg
) {
    printf("%s:%s", orig_file_path.c_str(), decompressed_msg.c_str());
}

static void print_result_binary(
        string const& orig_file_path,
        Message const& compressed_msg,
        string const& decompressed_msg,
        void* custom_arg
) {
    bool write_successful = true;
    do {
        size_t length;
        size_t num_elems_written;

        // Write file path
        length = orig_file_path.length();
        num_elems_written = fwrite(&length, sizeof(length), 1, stdout);
        if (num_elems_written < 1) {
            write_successful = false;
            break;
        }
        num_elems_written = fwrite(orig_file_path.c_str(), sizeof(char), length, stdout);
        if (num_elems_written < length) {
            write_successful = false;
            break;
        }

        // Write timestamp
        epochtime_t timestamp = compressed_msg.get_ts_in_milli();
        num_elems_written = fwrite(&timestamp, sizeof(timestamp), 1, stdout);
        if (num_elems_written < 1) {
            write_successful = false;
            break;
        }

        // Write logtype ID
        auto logtype_id = compressed_msg.get_logtype_id();
        num_elems_written = fwrite(&logtype_id, sizeof(logtype_id), 1, stdout);
        if (num_elems_written < 1) {
            write_successful = false;
            break;
        }

        // Write message
        length = decompressed_msg.length();
        num_elems_written = fwrite(&length, sizeof(length), 1, stdout);
        if (num_elems_written < 1) {
            write_successful = false;
            break;
        }
        num_elems_written = fwrite(decompressed_msg.c_str(), sizeof(char), length, stdout);
        if (num_elems_written < length) {
            write_successful = false;
            break;
        }
    } while (false);
    if (!write_successful) {
        SPDLOG_ERROR("Failed to write result in binary form, errno={}", errno);
    }
}

bool search(CommandLineArguments& command_line_args) {
    // Create vector of search strings
    vector<string> search_strings;
    if (command_line_args.get_search_strings_file_path().empty()) {
        search_strings.push_back(command_line_args.get_search_string());
    } else {
        FileReader file_reader;
        file_reader.open(command_line_args.get_search_strings_file_path());
        string line;
        while (file_reader.read_to_delimiter('\n', false, false, line)) {
            if (!line.empty()) {
                search_strings.push_back(line);
            }
        }
        file_reader.close();
    }

    // Validate archives directory
    struct stat archives_dir_stat = {};
    auto archives_dir = std::filesystem::path(command_line_args.get_archives_dir());
    if (0 != stat(archives_dir.c_str(), &archives_dir_stat)) {
        SPDLOG_ERROR(
                "'{}' does not exist or cannot be accessed - {}.",
                archives_dir.c_str(),
                strerror(errno)
        );
        return false;
    } else if (S_ISDIR(archives_dir_stat.st_mode) == false) {
        SPDLOG_ERROR("'{}' is not a directory.", archives_dir.c_str());
        return false;
    }

    auto const& global_metadata_db_config = command_line_args.get_metadata_db_config();
    std::unique_ptr<GlobalMetadataDB> global_metadata_db;
    switch (global_metadata_db_config.get_metadata_db_type()) {
        case GlobalMetadataDBConfig::MetadataDBType::SQLite: {
            auto global_metadata_db_path = archives_dir / streaming_archive::cMetadataDBFileName;
            global_metadata_db
                    = std::make_unique<GlobalSQLiteMetadataDB>(global_metadata_db_path.string());
            break;
        }
        case GlobalMetadataDBConfig::MetadataDBType::MySQL:
            global_metadata_db = std::make_unique<GlobalMySQLMetadataDB>(
                    global_metadata_db_config.get_metadata_db_host(),
                    global_metadata_db_config.get_metadata_db_port(),
                    global_metadata_db_config.get_metadata_db_username(),
                    global_metadata_db_config.get_metadata_db_password(),
                    global_metadata_db_config.get_metadata_db_name(),
                    global_metadata_db_config.get_metadata_table_prefix()
            );
            break;
    }
    global_metadata_db->open();

    string archive_id;
    Archive archive_reader;
    size_t num_matches = 0;
    for (auto archive_ix = std::unique_ptr<GlobalMetadataDB::ArchiveIterator>(get_archive_iterator(
                 *global_metadata_db,
                 command_line_args.get_file_path(),
                 command_line_args.get_search_begin_ts(),
                 command_line_args.get_search_end_ts()
         ));
         archive_ix->contains_element();
         archive_ix->get_next())
    {
        archive_ix->get_id(archive_id);
        auto archive_path = archives_dir / archive_id;

        if (false == std::filesystem::exists(archive_path)) {
            SPDLOG_WARN(
                    "Archive {} does not exist in '{}'.",
                    archive_id,
                    command_line_args.get_archives_dir()
            );
            continue;
        }

        // Open archive
        if (!open_archive(archive_path.string(), archive_reader)) {
            return false;
        }

        // Generate lexer if schema file exists
        auto schema_file_path = archive_path / streaming_archive::cSchemaFileName;
        // Perform search
        if (!search(search_strings, command_line_args, archive_reader, num_matches)) {
            return false;
        }
        archive_reader.close();
    }

    global_metadata_db->close();
    return true;
}
}  // namespace glt::glt
