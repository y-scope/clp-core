#include "compression.hpp"

#include <iostream>
#include <memory>

#include <archive_entry.h>
#include <boost/filesystem/operations.hpp>
#include <boost/uuid/random_generator.hpp>

#include "../GlobalMySQLMetadataDB.hpp"
#include "../GlobalSQLiteMetadataDB.hpp"
#include "../spdlog_with_specializations.hpp"
#include "../streaming_archive/writer/Archive.hpp"
#include "../streaming_archive/writer/utils.hpp"
#include "../Utils.hpp"
#include "FileCompressor.hpp"
#include "utils.hpp"

using clp::streaming_archive::writer::split_archive;
using std::cerr;
using std::cout;
using std::endl;
using std::make_unique;
using std::out_of_range;
using std::string;
using std::unique_ptr;
using std::vector;

namespace clp::clp {
// Local prototypes
/**
 * Comparator to sort files based on their group ID
 * @param lhs
 * @param rhs
 * @return true if lhs' group ID is less than rhs' group ID, false otherwise
 */
static bool file_group_id_comparator(FileToCompress const& lhs, FileToCompress const& rhs);
/**
 * Comparator to sort files based on their last write time
 * @param lhs
 * @param rhs
 * @return true if lhs' last write time is greater than rhs' last write time, false otherwise
 */
static bool
file_gt_last_write_time_comparator(FileToCompress const& lhs, FileToCompress const& rhs);

static bool file_group_id_comparator(FileToCompress const& lhs, FileToCompress const& rhs) {
    return lhs.get_group_id() < rhs.get_group_id();
}

static bool
file_gt_last_write_time_comparator(FileToCompress const& lhs, FileToCompress const& rhs) {
    return boost::filesystem::last_write_time(lhs.get_path())
           > boost::filesystem::last_write_time(rhs.get_path());
}

bool compress(
        CommandLineArguments& command_line_args,
        vector<FileToCompress>& files_to_compress,
        vector<string> const& empty_directory_paths,
        vector<FileToCompress>& grouped_files_to_compress,
        size_t target_encoded_file_size,
        std::unique_ptr<log_surgeon::ReaderParser> reader_parser,
        bool use_heuristic
) {
    auto output_dir = boost::filesystem::path(command_line_args.get_output_dir());

    // Create output directory in case it doesn't exist
    auto error_code = create_directory(output_dir.parent_path().string(), 0700, true);
    if (ErrorCode_Success != error_code) {
        SPDLOG_ERROR("Failed to create {} - {}", output_dir.parent_path().c_str(), strerror(errno));
        return false;
    }

    auto const& global_metadata_db_config = command_line_args.get_metadata_db_config();
    std::unique_ptr<GlobalMetadataDB> global_metadata_db;
    switch (global_metadata_db_config.get_metadata_db_type()) {
        case GlobalMetadataDBConfig::MetadataDBType::SQLite: {
            auto global_metadata_db_path = output_dir / streaming_archive::cMetadataDBFileName;
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

    auto uuid_generator = boost::uuids::random_generator();

    // Setup config
    streaming_archive::writer::Archive::UserConfig archive_user_config;
    archive_user_config.id = uuid_generator();
    archive_user_config.creator_id = uuid_generator();
    archive_user_config.creation_num = 0;
    archive_user_config.target_segment_uncompressed_size
            = command_line_args.get_target_segment_uncompressed_size();
    archive_user_config.compression_level = command_line_args.get_compression_level();
    archive_user_config.output_dir = command_line_args.get_output_dir();
    archive_user_config.global_metadata_db = global_metadata_db.get();
    archive_user_config.print_archive_stats_progress
            = command_line_args.print_archive_stats_progress();
    archive_user_config.use_single_file_archive = command_line_args.single_file_archive();

    // Open Archive
    streaming_archive::writer::Archive archive_writer;
    // Set schema file if specified by user
    if (false == command_line_args.get_use_heuristic()) {
        archive_writer.m_schema_file_path = command_line_args.get_schema_file_path();
    }
    // Open archive
    archive_writer.open(archive_user_config);

    archive_writer.add_empty_directories(empty_directory_paths);

    bool all_files_compressed_successfully = true;
    FileCompressor file_compressor(uuid_generator, std::move(reader_parser));
    auto target_data_size_of_dictionaries
            = command_line_args.get_target_data_size_of_dictionaries();

    // Compress all files
    size_t num_files_compressed = 0;
    size_t num_files_to_compress = 0;
    if (command_line_args.show_progress()) {
        num_files_to_compress = files_to_compress.size() + grouped_files_to_compress.size();
    }
    if (command_line_args.sort_input_files()) {
        sort(files_to_compress.begin(), files_to_compress.end(), file_gt_last_write_time_comparator
        );
    }
    for (auto it = files_to_compress.cbegin(); it != files_to_compress.cend(); ++it) {
        if (archive_writer.get_data_size_of_dictionaries() >= target_data_size_of_dictionaries) {
            split_archive(archive_user_config, archive_writer);
        }
        if (false
            == file_compressor.compress_file(
                    target_data_size_of_dictionaries,
                    archive_user_config,
                    target_encoded_file_size,
                    *it,
                    archive_writer,
                    use_heuristic
            ))
        {
            all_files_compressed_successfully = false;
        }
        if (command_line_args.show_progress()) {
            ++num_files_compressed;
            cerr << "Compressed " << num_files_compressed << '/' << num_files_to_compress
                 << " files" << '\r';
        }
    }

    // Sort files by group ID to avoid spreading groups over multiple segments
    sort(grouped_files_to_compress.begin(),
         grouped_files_to_compress.end(),
         file_group_id_comparator);
    // Compress grouped files
    for (auto const& file_to_compress : grouped_files_to_compress) {
        if (archive_writer.get_data_size_of_dictionaries() >= target_data_size_of_dictionaries) {
            split_archive(archive_user_config, archive_writer);
        }
        if (false
            == file_compressor.compress_file(
                    target_data_size_of_dictionaries,
                    archive_user_config,
                    target_encoded_file_size,
                    file_to_compress,
                    archive_writer,
                    use_heuristic
            ))
        {
            all_files_compressed_successfully = false;
        }
        if (command_line_args.show_progress()) {
            ++num_files_compressed;
            cerr << "Compressed " << num_files_compressed << '/' << num_files_to_compress
                 << " files" << '\r';
        }
    }

    archive_writer.close();

    return all_files_compressed_successfully;
}

bool read_and_validate_grouped_file_list(
        boost::filesystem::path const& path_prefix_to_remove,
        string const& list_path,
        vector<FileToCompress>& grouped_files
) {
    unique_ptr<FileReader> grouped_file_path_reader;
    try {
        grouped_file_path_reader = make_unique<FileReader>(list_path);
    } catch (FileReader::OperationFailed const& exception) {
        auto const error_code = exception.get_error_code();
        if (ErrorCode_FileNotFound == error_code) {
            SPDLOG_ERROR("'{}' does not exist.", list_path.c_str());
        } else if (ErrorCode_errno == error_code) {
            SPDLOG_ERROR("Failed to read '{}', errno={}", list_path.c_str(), errno);
        } else {
            SPDLOG_ERROR("Failed to read '{}', error_code={}", list_path.c_str(), error_code);
        }
        return false;
    }

    unique_ptr<FileReader> grouped_file_id_reader;
    string grouped_file_ids_path = list_path.substr(0, list_path.length() - 4) + ".gid";
    try {
        grouped_file_id_reader = make_unique<FileReader>(grouped_file_ids_path);
    } catch (FileReader::OperationFailed const& exception) {
        auto const error_code = exception.get_error_code();
        if (ErrorCode_FileNotFound == error_code) {
            SPDLOG_ERROR("'{}' does not exist.", grouped_file_ids_path.c_str());
        } else if (ErrorCode_errno == error_code) {
            SPDLOG_ERROR("Failed to read '{}', errno={}", grouped_file_ids_path.c_str(), errno);
        } else {
            SPDLOG_ERROR(
                    "Failed to read '{}', error_code={}",
                    grouped_file_ids_path.c_str(),
                    error_code
            );
        }
        return false;
    }

    // Read list
    bool all_paths_valid = true;
    string path;
    string path_without_prefix;
    group_id_t group_id;
    ErrorCode error_code{ErrorCode_Success};
    while (true) {
        // Read path
        error_code = grouped_file_path_reader->try_read_to_delimiter('\n', false, false, path);
        if (ErrorCode_Success != error_code) {
            break;
        }
        // Validate path is not empty
        if (path.empty()) {
            SPDLOG_ERROR("Found empty line in {}", list_path.c_str());
            all_paths_valid = false;
            continue;
        }

        // Read group ID
        error_code = grouped_file_id_reader->try_read_numeric_value(group_id);
        if (ErrorCode_Success != error_code) {
            if (ErrorCode_EndOfFile == error_code) {
                SPDLOG_ERROR("There are more grouped file paths than IDs.");
                return false;
            }
            break;
        }

        // Validate path exists
        if (boost::filesystem::exists(path) == false) {
            SPDLOG_ERROR("'{}' does not exist.", path.c_str());
            all_paths_valid = false;
            continue;
        }

        // Validate path is not a directory
        if (boost::filesystem::is_directory(path)) {
            SPDLOG_ERROR(
                    "Directory '{}' found in list of grouped files. If the directory contains "
                    "grouped files, please specify them individually.",
                    path.c_str()
            );
            all_paths_valid = false;
            continue;
        }

        if (false
            == remove_prefix_and_clean_up_path(path_prefix_to_remove, path, path_without_prefix))
        {
            SPDLOG_ERROR(
                    "'{}' does not contain prefix '{}'.",
                    path.c_str(),
                    path_prefix_to_remove.c_str()
            );
            all_paths_valid = false;
            continue;
        }

        // Add grouped file
        grouped_files.emplace_back(path, path_without_prefix, group_id);
    }
    // Check for any unexpected errors
    if (ErrorCode_EndOfFile != error_code) {
        if (ErrorCode_errno == error_code) {
            SPDLOG_ERROR("Failed to read grouped file paths or IDs, errno={}", errno);
        } else {
            SPDLOG_ERROR("Failed to read grouped file paths or IDs, error_code={}", error_code);
        }
        return false;
    }

    // Validate the list contained at least one file
    if (grouped_files.empty()) {
        SPDLOG_ERROR("'{}' did not contain any paths.", list_path.c_str());
        return false;
    }

    return all_paths_valid;
}
}  // namespace clp::clp
