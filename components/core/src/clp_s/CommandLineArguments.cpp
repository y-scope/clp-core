#include "CommandLineArguments.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

#include <boost/program_options.hpp>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include "../clp/cli_utils.hpp"
#include "../clp/type_utils.hpp"
#include "../reducer/types.hpp"
#include "FileReader.hpp"

namespace po = boost::program_options;

namespace clp_s {
namespace {
// Authorization method constants
constexpr std::string_view cNoAuth{"none"};
constexpr std::string_view cS3Auth{"s3"};

/**
 * Read a list of newline-delimited paths from a file and put them into a vector passed by reference
 * TODO: deduplicate this code with the version in clp
 * @param input_path_list_file_path path to the file containing the list of paths
 * @param path_destination the vector that the paths are pushed into
 * @return true on success
 * @return false on error
 */
bool read_paths_from_file(
        std::string const& input_path_list_file_path,
        std::vector<std::string>& path_destination
) {
    FileReader reader;
    auto error_code = reader.try_open(input_path_list_file_path);
    if (ErrorCodeFileNotFound == error_code) {
        SPDLOG_ERROR(
                "Failed to open input path list file {} - file not found",
                input_path_list_file_path
        );
        return false;
    } else if (ErrorCodeSuccess != error_code) {
        SPDLOG_ERROR("Error opening input path list file {}", input_path_list_file_path);
        return false;
    }

    std::string line;
    while (true) {
        error_code = reader.try_read_to_delimiter('\n', false, false, line);
        if (ErrorCodeSuccess != error_code) {
            break;
        }
        if (false == line.empty()) {
            path_destination.push_back(line);
        }
    }

    if (ErrorCodeEndOfFile != error_code) {
        return false;
    }
    return true;
}

/**
 * Validates and populates network authorization options.
 * @param auth_method
 * @param network_auth
 * @throws std::invalid_argument if the authorization option is invalid
 */
void validate_network_auth(std::string_view auth_method, NetworkAuthOption& auth) {
    if (cS3Auth == auth_method) {
        auth.method = AuthMethod::S3PresignedUrlV4;
    } else if (cNoAuth != auth_method) {
        throw std::invalid_argument(fmt::format("Invalid authentication type \"{}\"", auth_method));
    }
}

/**
 * Validates and populates archive paths.
 * @param archive_path
 * @param archive_id
 * @param archive_paths
 * @throws std::invalid_argument on any error
 */
void validate_archive_paths(
        std::string_view archive_path,
        std::string_view archive_id,
        std::vector<Path>& archive_paths
) {
    if (archive_path.empty()) {
        throw std::invalid_argument("No archive path specified");
    }

    if (false == archive_id.empty()) {
        auto archive_fs_path = std::filesystem::path(archive_path) / archive_id;
        std::error_code ec;
        if (false == std::filesystem::exists(archive_fs_path, ec) || ec) {
            throw std::invalid_argument("Requested archive does not exist");
        }
        archive_paths.emplace_back(clp_s::Path{
                .source = clp_s::InputSource::Filesystem,
                .path = archive_fs_path.string()
        });
    } else if (false == get_input_archives_for_raw_path(archive_path, archive_paths)) {
        throw std::invalid_argument("Invalid archive path");
    }

    if (archive_paths.empty()) {
        throw std::invalid_argument("No archive paths specified");
    }
}
}  // namespace

CommandLineArguments::ParsingResult
CommandLineArguments::parse_arguments(int argc, char const** argv) {
    if (1 == argc) {
        print_basic_usage();
        return ParsingResult::Failure;
    }

    po::options_description general_options("General options");
    general_options.add_options()("help,h", "Print help");

    char command_input;
    po::options_description general_positional_options("General positional options");
    // clang-format off
    general_positional_options.add_options()(
            "command", po::value<char>(&command_input)
    )(
            "command-args", po::value<std::vector<std::string>>()
    );
    // clang-format on

    po::positional_options_description general_positional_options_description;
    general_positional_options_description.add("command", 1);
    general_positional_options_description.add("command-args", -1);

    po::options_description all_descriptions;
    all_descriptions.add(general_options);
    all_descriptions.add(general_positional_options);

    try {
        po::variables_map parsed_command_line_options;
        po::parsed_options parsed = po::command_line_parser(argc, argv)
                                            .options(all_descriptions)
                                            .positional(general_positional_options_description)
                                            .allow_unregistered()
                                            .run();
        po::store(parsed, parsed_command_line_options);
        po::notify(parsed_command_line_options);

        if (parsed_command_line_options.count("command") == 0) {
            if (parsed_command_line_options.count("help") != 0) {
                if (argc > 2) {
                    SPDLOG_WARN("Ignoring all options besides --help.");
                }

                print_basic_usage();
                std::cerr << "COMMAND is one of:" << std::endl;
                std::cerr << "  c - compress" << std::endl;
                std::cerr << "  x - decompress" << std::endl;
                std::cerr << "  s - search" << std::endl;
                std::cerr << "  r - JSON to IR Format" << std::endl;
                std::cerr << std::endl;
                std::cerr << "Try "
                          << " c --help OR"
                          << " x --help OR"
                          << " s --help OR"
                          << " r --help for command-specific details." << std::endl;

                po::options_description visible_options;
                visible_options.add(general_options);
                std::cerr << visible_options << '\n';
                return ParsingResult::InfoCommand;
            }

            throw std::invalid_argument("Command unspecified");
        }

        switch (command_input) {
            case (char)Command::Compress:
            case (char)Command::Extract:
            case (char)Command::Search:
            case (char)Command::JsonToIr:
                m_command = (Command)command_input;
                break;
            default:
                throw std::invalid_argument(std::string("Unknown action '") + command_input + "'");
        }

        if (Command::Compress == m_command) {
            po::options_description compression_positional_options;
            std::vector<std::string> input_paths;
            // clang-format off
             compression_positional_options.add_options()(
                     "archives-dir",
                     po::value<std::string>(&m_archives_dir)->value_name("DIR"),
                     "output directory"
             )(
                     "input-paths",
                     po::value<std::vector<std::string>>(&input_paths)->value_name("PATHS"),
                     "input paths"
             );
            // clang-format on

            po::options_description compression_options("Compression options");
            std::string metadata_db_config_file_path;
            std::string input_path_list_file_path;
            constexpr std::string_view cJsonFileType{"json"};
            constexpr std::string_view cKeyValueIrFileType{"kv-ir"};
            std::string file_type{cJsonFileType};
            std::string auth{cNoAuth};
            // clang-format off
            compression_options.add_options()(
                    "compression-level",
                    po::value<int>(&m_compression_level)->value_name("LEVEL")->
                        default_value(m_compression_level),
                    "1 (fast/low compression) to 9 (slow/high compression)."
            )(
                    "target-encoded-size",
                    po::value<size_t>(&m_target_encoded_size)->value_name("TARGET_ENCODED_SIZE")->
                        default_value(m_target_encoded_size),
                    "Target size (B) for the dictionaries and encoded messages before a new "
                    "archive is created."
            )(
                    "min-table-size",
                    po::value<size_t>(&m_minimum_table_size)->value_name("MIN_TABLE_SIZE")->
                        default_value(m_minimum_table_size),
                    "Minimum size (B) for a packed table before it gets compressed."
            )(
                    "max-document-size",
                    po::value<size_t>(&m_max_document_size)->value_name("DOC_SIZE")->
                        default_value(m_max_document_size),
                    "Maximum allowed size (B) for a single document before compression fails."
            )(
                    "timestamp-key",
                    po::value<std::string>(&m_timestamp_key)->value_name("TIMESTAMP_COLUMN_KEY")->
                        default_value(m_timestamp_key),
                    "Path (e.g. x.y) for the field containing the log event's timestamp."
            )(
                    "db-config-file",
                    po::value<std::string>(&metadata_db_config_file_path)->value_name("FILE")->
                    default_value(metadata_db_config_file_path),
                    "Global metadata DB YAML config"
            )(
                    "files-from,f",
                    po::value<std::string>(&input_path_list_file_path)
                            ->value_name("FILE")
                            ->default_value(input_path_list_file_path),
                    "Compress files specified in FILE"
            )(
                    "print-archive-stats",
                    po::bool_switch(&m_print_archive_stats),
                    "Print statistics (json) about the archive after it's compressed."
            )(
                    "single-file-archive",
                    po::bool_switch(&m_single_file_archive),
                    "Create a single archive file instead of multiple files."
            )(
                    "structurize-arrays",
                    po::bool_switch(&m_structurize_arrays),
                    "Structurize arrays instead of compressing them as clp strings."
            )(
                    "disable-log-order",
                    po::bool_switch(&m_disable_log_order),
                    "Do not record log order at ingestion time."
            )(
                    "file-type",
                    po::value<std::string>(&file_type)->value_name("FILE_TYPE")->default_value(file_type),
                    "The type of file being compressed (json or kv-ir)"
            )(
                    "auth",
                    po::value<std::string>(&auth)
                        ->value_name("AUTH_METHOD")
                        ->default_value(auth),
                    "Type of authentication required for network requests (s3 | none). Authentication"
                    " with s3 requires the AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY environment"
                    " variables, and optionally the AWS_SESSION_TOKEN environment variable."
            );
            // clang-format on

            po::positional_options_description positional_options;
            positional_options.add("archives-dir", 1);
            positional_options.add("input-paths", -1);

            po::options_description all_compression_options;
            all_compression_options.add(compression_options);
            all_compression_options.add(compression_positional_options);

            std::vector<std::string> unrecognized_options
                    = po::collect_unrecognized(parsed.options, po::include_positional);
            unrecognized_options.erase(unrecognized_options.begin());
            po::store(
                    po::command_line_parser(unrecognized_options)
                            .options(all_compression_options)
                            .positional(positional_options)
                            .run(),
                    parsed_command_line_options
            );
            po::notify(parsed_command_line_options);

            if (parsed_command_line_options.count("help")) {
                print_compression_usage();

                std::cerr << "Examples:" << std::endl;
                std::cerr << "  # Compress file1.json and dir1 into archives-dir" << std::endl;
                std::cerr << "  " << m_program_name << " c archives-dir file1.json dir1"
                          << std::endl;

                po::options_description visible_options;
                visible_options.add(general_options);
                visible_options.add(compression_options);
                std::cerr << visible_options << '\n';
                return ParsingResult::InfoCommand;
            }

            if (m_archives_dir.empty()) {
                throw std::invalid_argument("No archives directory specified.");
            }

            if (false == input_path_list_file_path.empty()) {
                if (false == read_paths_from_file(input_path_list_file_path, input_paths)) {
                    SPDLOG_ERROR("Failed to read paths from {}", input_path_list_file_path);
                    return ParsingResult::Failure;
                }
            }

            for (auto const& path : input_paths) {
                if (false == get_input_files_for_raw_path(path, m_input_paths)) {
                    throw std::invalid_argument(fmt::format("Invalid input path \"{}\".", path));
                }
            }

            if (m_input_paths.empty()) {
                throw std::invalid_argument("No input paths specified.");
            }

            if (cJsonFileType == file_type) {
                m_file_type = FileType::Json;
            } else if (cKeyValueIrFileType == file_type) {
                m_file_type = FileType::KeyValueIr;
                if (m_structurize_arrays) {
                    SPDLOG_ERROR(
                            "Invalid combination of arguments; --file-type {} and "
                            "--structurize-arrays can't be used together",
                            cKeyValueIrFileType
                    );
                    return ParsingResult::Failure;
                }
                if (false == m_timestamp_key.empty()) {
                    SPDLOG_ERROR(
                            "Invalid combination of arguments; --file-type {} and "
                            "--timestamp-key can't be used together",
                            cKeyValueIrFileType
                    );
                    return ParsingResult::Failure;
                }
            } else {
                throw std::invalid_argument("Unknown FILE_TYPE: " + file_type);
            }

            validate_network_auth(auth, m_network_auth);

            // Parse and validate global metadata DB config
            if (false == metadata_db_config_file_path.empty()) {
                clp::GlobalMetadataDBConfig metadata_db_config;
                try {
                    metadata_db_config.parse_config_file(metadata_db_config_file_path);
                } catch (std::exception& e) {
                    SPDLOG_ERROR("Failed to validate metadata database config - {}.", e.what());
                    return ParsingResult::Failure;
                }

                if (clp::GlobalMetadataDBConfig::MetadataDBType::MySQL
                    != metadata_db_config.get_metadata_db_type())
                {
                    SPDLOG_ERROR(
                            "Invalid metadata database type for {}; only supported type is MySQL.",
                            m_program_name
                    );
                    return ParsingResult::Failure;
                }

                m_metadata_db_config = std::move(metadata_db_config);
            }
        } else if ((char)Command::Extract == command_input) {
            po::options_description extraction_options;
            std::string archive_path;
            // clang-format off
            extraction_options.add_options()(
                    "archive-path",
                    po::value<std::string>(&archive_path),
                    "Path to a directory containing archives, or the path to a single archive"
            )(
                    "output-dir",
                    po::value<std::string>(&m_output_dir),
                    "The output directory for the decompressed file"
            );
            // clang-format on

            po::options_description decompression_options("Decompression Options");
            std::string auth{cNoAuth};
            std::string archive_id;
            // clang-format off
            decompression_options.add_options()(
                    "ordered",
                    po::bool_switch(&m_ordered_decompression),
                    "Enable decompression in log order for this archive"
            )(
                    "target-ordered-chunk-size",
                    po::value<size_t>(&m_target_ordered_chunk_size)
                            ->default_value(m_target_ordered_chunk_size)
                            ->value_name("SIZE"),
                    "Chunk size (B) for each output file when decompressing records in log order."
                    " When set to 0, no chunking is performed."
            )(
                    "print-ordered-chunk-stats",
                    po::bool_switch(&m_print_ordered_chunk_stats),
                    "Print statistics (ndjson) about each chunk file after it's extracted."
            )(
                    "archive-id",
                    po::value<std::string>(&archive_id)->value_name("ID"),
                    "Limit decompression to the archive with the given ID in a subdirectory of"
                    " archive-path"
            )(
                    "auth",
                    po::value<std::string>(&auth)
                        ->value_name("AUTH_METHOD")
                        ->default_value(auth),
                    "Type of authentication required for network requests (s3 | none). Authentication"
                    " with s3 requires the AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY environment"
                    " variables, and optionally the AWS_SESSION_TOKEN environment variable."
            );
            // clang-format on
            extraction_options.add(decompression_options);

            po::options_description output_metadata_options("Output Metadata Options");
            // clang-format off
            output_metadata_options.add_options()(
                    "mongodb-uri",
                    po::value<std::string>(&m_mongodb_uri)->value_name("URI"),
                    "MongoDB URI for the database to write decompression metadata to"
            )(
                    "mongodb-collection",
                    po::value<std::string>(&m_mongodb_collection)->value_name("COLLECTION"),
                    "MongoDB collection to write decompression metadata to"
            );
            // clang-format on
            extraction_options.add(output_metadata_options);

            po::positional_options_description positional_options;
            positional_options.add("archive-path", 1);
            positional_options.add("output-dir", 1);

            std::vector<std::string> unrecognized_options
                    = po::collect_unrecognized(parsed.options, po::include_positional);
            unrecognized_options.erase(unrecognized_options.begin());
            po::store(
                    po::command_line_parser(unrecognized_options)
                            .options(extraction_options)
                            .positional(positional_options)
                            .run(),
                    parsed_command_line_options
            );

            po::notify(parsed_command_line_options);

            if (parsed_command_line_options.count("help")) {
                print_decompression_usage();

                std::cerr << "Examples:" << std::endl;
                std::cerr << "  # Decompress all files from archives in archives-dir"
                             "    into output-dir"
                          << std::endl;
                std::cerr << "  " << m_program_name << " x archives-dir output-dir" << std::endl;
                std::cerr << std::endl;

                po::options_description visible_options;
                visible_options.add(general_options);
                visible_options.add(decompression_options);
                visible_options.add(output_metadata_options);
                std::cerr << visible_options << std::endl;
                return ParsingResult::InfoCommand;
            }

            validate_archive_paths(archive_path, archive_id, m_input_paths);

            validate_network_auth(auth, m_network_auth);

            if (m_output_dir.empty()) {
                throw std::invalid_argument("No output directory specified");
            }

            if (false == m_ordered_decompression) {
                if (0 != m_target_ordered_chunk_size) {
                    throw std::invalid_argument(
                            "target-ordered-chunk-size must be used with ordered argument"
                    );
                }

                if (m_print_ordered_chunk_stats) {
                    throw std::invalid_argument(
                            "print-ordered-chunk-stats must be used with ordered argument"
                    );
                }

                if (false == m_mongodb_uri.empty()) {
                    throw std::invalid_argument("Recording decompression metadata only supported"
                                                " for ordered decompression");
                }
            }

            // We use xor to check that these arguments are either both specified or both
            // unspecified.
            if (m_mongodb_uri.empty() ^ m_mongodb_collection.empty()) {
                throw std::invalid_argument(
                        "mongodb-uri and mongodb-collection must both be non-empty"
                );
            }

        } else if ((char)Command::Search == command_input) {
            std::string archives_dir;
            std::string query;

            po::options_description search_options;
            std::string output_handler_name;
            std::string archive_path;
            // clang-format off
            search_options.add_options()(
                    "archive-path",
                    po::value<std::string>(&archive_path),
                    "Path to a directory containing archives, or the path to a single archive"
            )(
                    "query,q",
                    po::value<std::string>(&m_query),
                    "Query to perform"
            )(
                    "output-handler",
                    po::value<std::string>(&output_handler_name)
            )(
                    "output-handler-args",
                    po::value<std::vector<std::string>>()
            );
            // clang-format on
            po::positional_options_description positional_options;
            positional_options.add("archive-path", 1);
            positional_options.add("query", 1);
            positional_options.add("output-handler", 1);
            positional_options.add("output-handler-args", -1);

            po::options_description match_options("Match Controls");
            std::string auth{cNoAuth};
            std::string archive_id;
            // clang-format off
            match_options.add_options()(
                "tge",
                po::value<epochtime_t>()->value_name("TS"),
                "Find records with UNIX epoch timestamp >= TS ms"
            )(
                "tle",
                po::value<epochtime_t>()->value_name("TS"),
                "Find records with UNIX epoch timestamp <= TS ms"
            )(
                "ignore-case,i",
                po::bool_switch(&m_ignore_case),
                "Ignore case distinctions between values in the query and the compressed data"
            )(
                "archive-id",
                po::value<std::string>(&archive_id)->value_name("ID"),
                "Limit search to the archive with the given ID in a subdirectory of archive-path"
            )(
                "projection",
                po::value<std::vector<std::string>>(&m_projection_columns)
                    ->multitoken()
                    ->value_name("COLUMN_A COLUMN_B ..."),
                "Project only the given set of columns for matching results. This option must be"
                " specified after all positional options. Values that are objects or structured"
                " arrays are currently unsupported."
            )(
                "auth",
                po::value<std::string>(&auth)
                    ->value_name("AUTH_METHOD")
                    ->default_value(auth),
                "Type of authentication required for network requests (s3 | none). Authentication"
                " with s3 requires the AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY environment"
                " variables, and optionally the AWS_SESSION_TOKEN environment variable."
            );
            // clang-format on
            search_options.add(match_options);

            po::options_description aggregation_options("Aggregation Options");
            // clang-format off
            aggregation_options.add_options()(
                    "count",
                    po::bool_switch(&m_do_count_results_aggregation),
                    "Count the number of results"
            )(
                    "count-by-time",
                    po::value<int64_t>(&m_count_by_time_bucket_size)->value_name("SIZE"),
                    "Count the number of results in each time span of the given size (ms)"
            );
            // clang-format on
            search_options.add(aggregation_options);

            po::options_description network_output_handler_options("Network Output Handler Options"
            );
            // clang-format off
            network_output_handler_options.add_options()(
                    "host",
                    po::value<std::string>(&m_network_dest_host)->value_name("HOST"),
                    "Network destination host"
            )(
                    "port",
                    po::value<int>(&m_network_dest_port)->value_name("PORT"),
                    "Network destination port"
            );
            // clang-format on

            po::options_description reducer_output_handler_options("Reducer Output Handler Options"
            );
            // clang-format off
            reducer_output_handler_options.add_options()(
                    "host",
                    po::value<std::string>(&m_reducer_host)->value_name("HOST"),
                    "Host the reducer is running on"
            )(
                    "port",
                    po::value<int>(&m_reducer_port)->value_name("PORT"),
                    "Port the reducer is listening on"
            )(
                    "job-id",
                    po::value<reducer::job_id_t>(&m_job_id)->value_name("ID"),
                    "Job ID for the requested aggregation operation"
            );
            // clang-format on

            po::options_description results_cache_output_handler_options(
                    "Results Cache Output Handler Options"
            );
            results_cache_output_handler_options.add_options()(
                    "uri",
                    po::value<std::string>(&m_mongodb_uri)->value_name("URI"),
                    "MongoDB URI for the results cache"
            )(
                    "collection",
                    po::value<std::string>(&m_mongodb_collection)->value_name("COLLECTION"),
                    "MongoDB collection to output to"
            )(
                    "batch-size",
                    po::value<uint64_t>(&m_batch_size)->value_name("SIZE")->
                            default_value(m_batch_size),
                    "The number of documents to insert into MongoDB per batch"
            )(
                    "max-num-results",
                    po::value<uint64_t>(&m_max_num_results)->value_name("MAX")->
                            default_value(m_max_num_results),
                    "The maximum number of results to output"
            );

            std::vector<std::string> unrecognized_options
                    = po::collect_unrecognized(parsed.options, po::include_positional);
            unrecognized_options.erase(unrecognized_options.begin());
            po::parsed_options search_parsed = po::command_line_parser(unrecognized_options)
                                                       .options(search_options)
                                                       .positional(positional_options)
                                                       .allow_unregistered()
                                                       .run();
            po::store(search_parsed, parsed_command_line_options);

            po::notify(parsed_command_line_options);

            constexpr char cNetworkOutputHandlerName[] = "network";
            constexpr char cReducerOutputHandlerName[] = "reducer";
            constexpr char cResultsCacheOutputHandlerName[] = "results-cache";
            constexpr char cStdoutCacheOutputHandlerName[] = "stdout";

            if (parsed_command_line_options.count("help")) {
                print_search_usage();
                std::cerr << "OUTPUT_HANDLER is one of:" << std::endl;
                std::cerr << "  " << static_cast<char const*>(cStdoutCacheOutputHandlerName)
                          << " (default) - Output to stdout" << std::endl;
                std::cerr << "  " << static_cast<char const*>(cNetworkOutputHandlerName)
                          << " - Output to a network destination" << std::endl;
                std::cerr << "  " << static_cast<char const*>(cResultsCacheOutputHandlerName)
                          << " - Output to the results cache" << std::endl;
                std::cerr << "  " << static_cast<char const*>(cReducerOutputHandlerName)
                          << " - Output to the reducer" << std::endl;
                std::cerr << std::endl;

                std::cerr << "Examples:" << std::endl;
                std::cerr << "  # Search archives in archives-dir for logs matching a KQL query"
                             R"( "level: INFO" and output to stdout)"
                          << std::endl;
                std::cerr << "  " << m_program_name << R"( s archives-dir "level: INFO")"
                          << std::endl;
                std::cerr << std::endl;

                std::cerr << "  # Search archives in archives-dir for logs matching a KQL query"
                             R"( "level: INFO" and output to the results cache)"
                          << std::endl;
                std::cerr << "  " << m_program_name << R"( s archives-dir "level: INFO")"
                          << " " << cResultsCacheOutputHandlerName
                          << " --uri mongodb://127.0.0.1:27017/test"
                             " --collection test"
                          << std::endl;
                std::cerr << std::endl;

                std::cerr << "  # Search archives in archives-dir for logs matching a KQL query"
                             R"( "level: INFO" and output to a network destination)"
                          << std::endl;
                std::cerr << "  " << m_program_name << R"( s archives-dir "level: INFO")"
                          << " " << cNetworkOutputHandlerName
                          << " --host localhost"
                             " --port 18000"
                          << std::endl;
                std::cerr << std::endl;

                std::cerr << "  # Search archives in archives-dir for logs matching a KQL query"
                             R"( "level: INFO" and output perform a count aggregation)"
                          << std::endl;
                std::cerr << "  " << m_program_name << R"( s archives-dir "level: INFO")"
                          << " " << cReducerOutputHandlerName << " --count"
                          << " --host localhost"
                          << " --port 14009"
                          << " --job-id 1" << std::endl;

                po::options_description visible_options;
                visible_options.add(general_options);
                visible_options.add(match_options);
                visible_options.add(aggregation_options);
                visible_options.add(network_output_handler_options);
                visible_options.add(results_cache_output_handler_options);
                visible_options.add(reducer_output_handler_options);
                std::cerr << visible_options << '\n';
                return ParsingResult::InfoCommand;
            }

            validate_archive_paths(archive_path, archive_id, m_input_paths);

            validate_network_auth(auth, m_network_auth);

            if (m_query.empty()) {
                throw std::invalid_argument("No query specified");
            }

            if (parsed_command_line_options.count("tge")) {
                m_search_begin_ts = parsed_command_line_options["tge"].as<epochtime_t>();
            }

            if (parsed_command_line_options.count("tle")) {
                m_search_end_ts = parsed_command_line_options["tle"].as<epochtime_t>();
            }

            if (m_search_begin_ts.has_value() && m_search_end_ts.has_value()
                && m_search_begin_ts.value() > m_search_end_ts.value())
            {
                throw std::invalid_argument(
                        "Timestamp range is invalid - begin timestamp is after end timestamp."
                );
            }

            if (parsed_command_line_options.count("count-by-time") > 0) {
                m_do_count_by_time_aggregation = true;
                if (m_count_by_time_bucket_size <= 0) {
                    throw std::invalid_argument("Value for count-by-time must be greater than zero."
                    );
                }
            }

            if (parsed_command_line_options.count("output-handler") > 0) {
                if (static_cast<char const*>(cNetworkOutputHandlerName) == output_handler_name) {
                    m_output_handler_type = OutputHandlerType::Network;
                } else if ((static_cast<char const*>(cReducerOutputHandlerName)
                            == output_handler_name))
                {
                    m_output_handler_type = OutputHandlerType::Reducer;
                } else if ((static_cast<char const*>(cResultsCacheOutputHandlerName)
                            == output_handler_name))
                {
                    m_output_handler_type = OutputHandlerType::ResultsCache;
                } else if ((static_cast<char const*>(cStdoutCacheOutputHandlerName)
                            == output_handler_name))
                {
                    m_output_handler_type = OutputHandlerType::Stdout;
                } else if (output_handler_name.empty()) {
                    throw std::invalid_argument("OUTPUT_HANDLER cannot be an empty string.");
                } else {
                    throw std::invalid_argument("Unknown OUTPUT_HANDLER: " + output_handler_name);
                }
            }

            if (OutputHandlerType::Network == m_output_handler_type) {
                parse_network_dest_output_handler_options(
                        network_output_handler_options,
                        search_parsed.options,
                        parsed_command_line_options
                );
            } else if (OutputHandlerType::Reducer == m_output_handler_type) {
                parse_reducer_output_handler_options(
                        reducer_output_handler_options,
                        search_parsed.options,
                        parsed_command_line_options
                );
            } else if (OutputHandlerType::ResultsCache == m_output_handler_type) {
                parse_results_cache_output_handler_options(
                        results_cache_output_handler_options,
                        search_parsed.options,
                        parsed_command_line_options
                );
            } else if (m_output_handler_type != OutputHandlerType::Stdout) {
                throw std::invalid_argument(
                        "Unhandled OutputHandlerType="
                        + std::to_string(clp::enum_to_underlying_type(m_output_handler_type))
                );
            }

            bool aggregation_was_specified
                    = m_do_count_by_time_aggregation || m_do_count_results_aggregation;
            if (aggregation_was_specified && OutputHandlerType::Reducer != m_output_handler_type) {
                throw std::invalid_argument(
                        "Aggregations are only supported with the reducer output handler."
                );
            } else if ((false == aggregation_was_specified
                        && OutputHandlerType::Reducer == m_output_handler_type))
            {
                throw std::invalid_argument("The reducer output handler currently only supports "
                                            "count and count-by-time aggregations.");
            }

            if (m_do_count_by_time_aggregation && m_do_count_results_aggregation) {
                throw std::invalid_argument(
                        "The --count-by-time and --count options are mutually exclusive."
                );
            }
        } else if ((char)Command::JsonToIr == command_input) {
            po::options_description compression_positional_options;
            std::vector<std::string> input_paths;
            // clang-format off
             compression_positional_options.add_options()(
                     "irs-dir",
                     po::value<std::string>(&m_archives_dir)->value_name("DIR"),
                     "output directory"
             )(
                     "input-paths",
                     po::value<std::vector<std::string>>(&input_paths)->value_name("PATHS"),
                     "input paths"
             );
            // clang-format on

            po::options_description compression_options("Compression options");
            std::string input_path_list_file_path;
            // clang-format off
            compression_options.add_options()(
                    "compression-level",
                    po::value<int>(&m_compression_level)->value_name("LEVEL")->
                        default_value(m_compression_level),
                    "1 (fast/low compression) to 9 (slow/high compression)."
            )(
                    "max-document-size",
                    po::value<size_t>(&m_max_document_size)->value_name("DOC_SIZE")->
                        default_value(m_max_document_size),
                    "Maximum allowed size (B) for a single document before ir generation fails."
            )(
                    "max-ir-buffer-size",
                    po::value<size_t>(&m_max_ir_buffer_size)->value_name("BUFFER_SIZE")->
                        default_value(m_max_ir_buffer_size),
                    "Maximum allowed size (B) for an in memory IR buffer befroe being written to file."
            )(
                    "encoding-type",
                    po::value<int>(&m_encoding_type)->value_name("ENCODING_TYPE")->
                        default_value(m_encoding_type),
                    "4 (four byte encoding) or 8 (eight byte encoding)"
            )(
                    "files-from,f",
                    po::value<std::string>(&input_path_list_file_path)
                            ->value_name("FILE")
                            ->default_value(input_path_list_file_path),
                    "Compress files specified in FILE"
            );
            // clang-format on

            po::positional_options_description positional_options;
            positional_options.add("irs-dir", 1);
            positional_options.add("input-paths", -1);

            po::options_description all_compression_options;
            all_compression_options.add(compression_options);
            all_compression_options.add(compression_positional_options);

            std::vector<std::string> unrecognized_options
                    = po::collect_unrecognized(parsed.options, po::include_positional);
            unrecognized_options.erase(unrecognized_options.begin());
            po::store(
                    po::command_line_parser(unrecognized_options)
                            .options(all_compression_options)
                            .positional(positional_options)
                            .run(),
                    parsed_command_line_options
            );
            po::notify(parsed_command_line_options);

            if (parsed_command_line_options.count("help")) {
                print_json_to_ir_usage();

                std::cerr << "Examples:\n";
                std::cerr << "  # Parse file1.json and dir1 into irs-dir\n";
                std::cerr << "  " << m_program_name << " r irs-dir file1.json dir1\n";

                po::options_description visible_options;
                visible_options.add(general_options);
                visible_options.add(compression_options);
                std::cerr << visible_options << '\n';
                return ParsingResult::InfoCommand;
            }

            if (m_archives_dir.empty()) {
                throw std::invalid_argument("No IRs directory specified.");
            }

            if (false == input_path_list_file_path.empty()) {
                if (false == read_paths_from_file(input_path_list_file_path, input_paths)) {
                    SPDLOG_ERROR("Failed to read paths from {}", input_path_list_file_path);
                    return ParsingResult::Failure;
                }
            }

            for (auto const& path : input_paths) {
                if (false == get_input_files_for_raw_path(path, m_input_paths)) {
                    throw std::invalid_argument(fmt::format("Invalid input path \"{}\".", path));
                }
            }

            if (m_input_paths.empty()) {
                throw std::invalid_argument("No input paths specified.");
            }

            if ((4 != m_encoding_type) && (8 != m_encoding_type)) {
                SPDLOG_ERROR(
                        "Invalid encoding type specified; --encoding-type {}",
                        m_encoding_type
                );
                return ParsingResult::Failure;
            }

            if (0 >= m_max_ir_buffer_size) {
                SPDLOG_ERROR(
                        "Invalid max_ir_buffer_size specified; Buffer size must be greater than "
                        "zero; --max-ir-buffer-size {}",
                        m_max_ir_buffer_size
                );
                return ParsingResult::Failure;
            }

            if (0 >= m_max_document_size) {
                SPDLOG_ERROR(
                        "Invalid max_document_size specified; Document size must be greater than "
                        "zero; --max-document-size {}",
                        m_max_document_size
                );
                return ParsingResult::Failure;
            }

            if ((1 > m_compression_level) || (9 < m_compression_level)) {
                SPDLOG_ERROR(
                        "Invalid compression level specified; Compression level must be 1-9; "
                        "--compression-level {}",
                        m_compression_level
                );
                return ParsingResult::Failure;
            }
        }
    } catch (std::exception& e) {
        SPDLOG_ERROR("{}", e.what());
        print_basic_usage();
        std::cerr << "Try " << get_program_name() << " --help for detailed usage instructions"
                  << std::endl;
        return ParsingResult::Failure;
    }

    return ParsingResult::Success;
}

void CommandLineArguments::parse_network_dest_output_handler_options(
        po::options_description const& options_description,
        std::vector<po::option> const& options,
        po::variables_map& parsed_options
) {
    clp::parse_unrecognized_options(options_description, options, parsed_options);

    if (parsed_options.count("host") == 0) {
        throw std::invalid_argument("host must be specified.");
    }
    if (m_network_dest_host.empty()) {
        throw std::invalid_argument("host cannot be an empty string.");
    }

    if (parsed_options.count("port") == 0) {
        throw std::invalid_argument("port must be specified.");
    }
    if (m_network_dest_port <= 0) {
        throw std::invalid_argument("port must be greater than zero.");
    }
}

void CommandLineArguments::parse_reducer_output_handler_options(
        po::options_description const& options_description,
        std::vector<po::option> const& options,
        po::variables_map& parsed_options
) {
    clp::parse_unrecognized_options(options_description, options, parsed_options);

    if (parsed_options.count("host") == 0) {
        throw std::invalid_argument("host must be specified.");
    }
    if (m_reducer_host.empty()) {
        throw std::invalid_argument("host cannot be an empty string.");
    }

    if (parsed_options.count("port") == 0) {
        throw std::invalid_argument("port must be specified.");
    }
    if (m_reducer_port <= 0) {
        throw std::invalid_argument("port must be greater than zero.");
    }

    if (parsed_options.count("job-id") == 0) {
        throw std::invalid_argument("job-id must be specified.");
    }
    if (m_job_id < 0) {
        throw std::invalid_argument("job-id cannot be negative.");
    }
}

void CommandLineArguments::parse_results_cache_output_handler_options(
        po::options_description const& options_description,
        std::vector<po::option> const& options,
        po::variables_map& parsed_options
) {
    clp::parse_unrecognized_options(options_description, options, parsed_options);

    if (parsed_options.count("uri") == 0) {
        throw std::invalid_argument("uri must be specified.");
    }
    if (m_mongodb_uri.empty()) {
        throw std::invalid_argument("uri cannot be an empty string.");
    }

    if (parsed_options.count("collection") == 0) {
        throw std::invalid_argument("collection must be specified.");
    }
    if (m_mongodb_collection.empty()) {
        throw std::invalid_argument("collection cannot be an empty string.");
    }

    if (0 == m_batch_size) {
        throw std::invalid_argument("batch-size cannot be 0.");
    }

    if (0 == m_max_num_results) {
        throw std::invalid_argument("max-num-results cannot be 0.");
    }
}

void CommandLineArguments::print_basic_usage() const {
    std::cerr << "Usage: " << m_program_name << " [OPTIONS] COMMAND [COMMAND ARGUMENTS]"
              << std::endl;
}

void CommandLineArguments::print_compression_usage() const {
    std::cerr << "Usage: " << m_program_name << " c [OPTIONS] ARCHIVES_DIR [FILE/DIR ...]"
              << std::endl;
}

void CommandLineArguments::print_decompression_usage() const {
    std::cerr << "Usage: " << m_program_name << " x [OPTIONS] ARCHIVES_DIR OUTPUT_DIR" << std::endl;
}

void CommandLineArguments::print_json_to_ir_usage() const {
    std::cerr << "Usage: " << m_program_name << " r [OPTIONS] IRS_DIR [FILE/DIR ...]\n";
}

void CommandLineArguments::print_search_usage() const {
    std::cerr << "Usage: " << m_program_name
              << " s [OPTIONS] ARCHIVES_DIR KQL_QUERY"
                 " [OUTPUT_HANDLER [OUTPUT_HANDLER_OPTIONS]]"
              << std::endl;
}
}  // namespace clp_s
