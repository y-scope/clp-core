#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include <mongocxx/instance.hpp>
#include <spdlog/sinks/stdout_sinks.h>

#include "../../reducer/network_utils.hpp"
#include "../clp/FileDecompressor.hpp"
#include "../Defs.h"
#include "../Grep.hpp"
#include "../ir/constants.hpp"
#include "../Profiler.hpp"
#include "../spdlog_with_specializations.hpp"
#include "../Utils.hpp"
#include "CommandLineArguments.hpp"
#include "constants.hpp"
#include "OutputHandler.hpp"

using clp::clo::CommandLineArguments;
using clp::clo::CountByTimeOutputHandler;
using clp::clo::CountOutputHandler;
using clp::clo::NetworkOutputHandler;
using clp::clo::OutputHandler;
using clp::clo::ResultsCacheOutputHandler;
using clp::clp::FileDecompressor;
using clp::CommandLineArgumentsBase;
using clp::create_directory;
using clp::epochtime_t;
using clp::ErrorCode;
using clp::ErrorCode_BadParam_DB_URI;
using clp::ErrorCode_errno;
using clp::ErrorCode_FileExists;
using clp::ErrorCode_Success;
using clp::Grep;
using clp::ir::cIrFileExtension;
using clp::load_lexer_from_file;
using clp::Query;
using clp::streaming_archive::MetadataDB;
using clp::streaming_archive::reader::Archive;
using clp::streaming_archive::reader::File;
using clp::streaming_archive::reader::Message;
using clp::TraceableException;
using std::cerr;
using std::cout;
using std::endl;
using std::runtime_error;
using std::string;
using std::to_string;
using std::unique_ptr;
using std::vector;

// Local types
enum class SearchFilesResult {
    OpenFailure,
    ResultSendFailure,
    Success
};

/**
 * Searches a file referenced by a given database cursor
 * @param query
 * @param archive
 * @param file_metadata_ix
 * @param output_handler
 * @return SearchFilesResult::OpenFailure on failure to open a compressed file
 * @return SearchFilesResult::ResultSendFailure on failure to send a result
 * @return SearchFilesResult::Success otherwise
 */
static SearchFilesResult search_file(
        Query& query,
        Archive& archive,
        MetadataDB::FileIterator& file_metadata_ix,
        std::unique_ptr<OutputHandler>& output_handler
);
/**
 * Searches all files referenced by a given database cursor
 * @param query
 * @param archive
 * @param file_metadata_ix
 * @param output_handler
 * @param segments_to_search
 */
static void search_files(
        Query& query,
        Archive& archive,
        MetadataDB::FileIterator& file_metadata_ix,
        std::unique_ptr<OutputHandler>& output_handler,
        std::set<clp::segment_id_t> const& segments_to_search
);
/**
 * Searches an archive with the given path
 * @param command_line_args
 * @param output_handler
 * @return true on success, false otherwise
 */
static bool search_archive(
        CommandLineArguments const& command_line_args,
        std::unique_ptr<OutputHandler> output_handler
);

namespace {
/**
 * Extracts a file split as IR chunks, writing them to the local filesystem and writing their
 * metadata to the results cache.
 * @param command_line_args
 * @return Whether the file split was successfully extracted.
 */
bool extract_ir(CommandLineArguments const& command_line_args);

/**
 * Performs a searches acccording to the given arguments.
 * @param command_line_args
 * @return Whether the search was successful.
 */
bool search(CommandLineArguments const& command_line_args);

/**
 * @param archive_path
 * @return Whether the given path exists and contains an archive metadata file.
 */
bool validate_archive_path(std::filesystem::path const& archive_path);

bool extract_ir(CommandLineArguments const& command_line_args) {
    std::filesystem::path const archive_path{command_line_args.get_archive_path()};
    if (false == validate_archive_path(archive_path)) {
        return false;
    }

    try {
        // Create output directory in case it doesn't exist
        std::filesystem::path const output_dir{command_line_args.get_ir_output_dir()};
        if (auto const error_code = create_directory(output_dir.string(), 0700, true);
            ErrorCode_Success != error_code)
        {
            SPDLOG_ERROR("Failed to create {} - {}", output_dir.string(), strerror(errno));
            return false;
        }

        Archive archive_reader;
        archive_reader.open(archive_path.string());
        archive_reader.refresh_dictionaries();

        auto const& file_split_id = command_line_args.get_file_split_id();
        auto file_metadata_ix_ptr = archive_reader.get_file_iterator_by_split_id(file_split_id);
        if (false == file_metadata_ix_ptr->has_next()) {
            SPDLOG_ERROR(
                    "File split '{}' doesn't exist in archive '{}'",
                    file_split_id,
                    archive_path.string()
            );
            return false;
        }

        mongocxx::client client;
        mongocxx::collection collection;

        try {
            auto const mongo_uri{mongocxx::uri(command_line_args.get_ir_mongodb_uri())};
            client = mongocxx::client{mongo_uri};
            collection
                    = client[mongo_uri.database()][command_line_args.get_ir_mongodb_collection()];
        } catch (mongocxx::exception const& e) {
            SPDLOG_ERROR("Failed to connect to results cache - {}", e.what());
            return false;
        }

        std::vector<bsoncxx::document::value> results;
        auto ir_output_handler = [&](std::filesystem::path const& src_ir_path,
                                     string const& orig_file_id,
                                     size_t begin_message_ix,
                                     size_t end_message_ix,
                                     bool is_last_chunk) {
            auto dest_ir_file_name = orig_file_id;
            dest_ir_file_name += "_" + std::to_string(begin_message_ix);
            dest_ir_file_name += "_" + std::to_string(end_message_ix);
            dest_ir_file_name += cIrFileExtension;

            auto const dest_ir_path = output_dir / dest_ir_file_name;
            try {
                std::filesystem::rename(src_ir_path, dest_ir_path);
            } catch (std::filesystem::filesystem_error const& e) {
                SPDLOG_ERROR(
                        "Failed to rename '{}' to '{}' - {}",
                        src_ir_path.string(),
                        dest_ir_path.string(),
                        e.what()
                );
                return false;
            }
            results.emplace_back(std::move(bsoncxx::builder::basic::make_document(
                    bsoncxx::builder::basic::kvp(
                            clp::clo::cResultsCacheKeys::IrOutput::Path,
                            dest_ir_file_name
                    ),
                    bsoncxx::builder::basic::kvp(
                            clp::clo::cResultsCacheKeys::IrOutput::StreamId,
                            orig_file_id
                    ),
                    bsoncxx::builder::basic::kvp(
                            clp::clo::cResultsCacheKeys::IrOutput::BeginMsgIx,
                            static_cast<int64_t>(begin_message_ix)
                    ),
                    bsoncxx::builder::basic::kvp(
                            clp::clo::cResultsCacheKeys::IrOutput::EndMsgIx,
                            static_cast<int64_t>(end_message_ix)
                    ),
                    bsoncxx::builder::basic::kvp(
                            clp::clo::cResultsCacheKeys::IrOutput::IsLastChunk,
                            is_last_chunk
                    )
            )));
            return true;
        };

        FileDecompressor file_decompressor;
        if (false
            == file_decompressor.decompress_to_ir(
                    archive_reader,
                    *file_metadata_ix_ptr,
                    command_line_args.get_ir_target_size(),
                    command_line_args.get_ir_output_dir(),
                    ir_output_handler
            ))
        {
            return false;
        }

        // Write the metadata into the results cache
        if (false == results.empty()) {
            try {
                collection.insert_many(results);
            } catch (mongocxx::exception const& e) {
                SPDLOG_ERROR("Failed to insert results into results cache - {}", e.what());
                return false;
            }
            results.clear();
        }

        file_metadata_ix_ptr.reset(nullptr);

        archive_reader.close();
    } catch (TraceableException& e) {
        auto error_code = e.get_error_code();
        if (ErrorCode_errno == error_code) {
            SPDLOG_ERROR(
                    "IR extraction failed: {}:{} {}, errno={}",
                    e.get_filename(),
                    e.get_line_number(),
                    e.what(),
                    errno
            );
        } else {
            SPDLOG_ERROR(
                    "IR extraction failed: {}:{} {}, error_code={}",
                    e.get_filename(),
                    e.get_line_number(),
                    e.what(),
                    error_code
            );
        }
        return false;
    }

    return true;
}

bool search(CommandLineArguments const& command_line_args) {
    std::unique_ptr<OutputHandler> output_handler;
    try {
        switch (command_line_args.get_output_handler_type()) {
            case CommandLineArguments::OutputHandlerType::Network:
                output_handler = std::make_unique<NetworkOutputHandler>(
                        command_line_args.get_network_dest_host(),
                        command_line_args.get_network_dest_port()
                );
                break;
            case CommandLineArguments::OutputHandlerType::Reducer: {
                auto const reducer_socket_fd = reducer::connect_to_reducer(
                        command_line_args.get_reducer_host(),
                        command_line_args.get_reducer_port(),
                        command_line_args.get_job_id()
                );
                if (-1 == reducer_socket_fd) {
                    SPDLOG_ERROR("Failed to connect to reducer");
                    return false;
                }

                if (command_line_args.do_count_results_aggregation()) {
                    output_handler = std::make_unique<CountOutputHandler>(reducer_socket_fd);
                } else if (command_line_args.do_count_by_time_aggregation()) {
                    output_handler = std::make_unique<CountByTimeOutputHandler>(
                            reducer_socket_fd,
                            command_line_args.get_count_by_time_bucket_size()
                    );
                } else {
                    SPDLOG_ERROR("Unhandled aggregation type.");
                    return false;
                }

                break;
            }
            case CommandLineArguments::OutputHandlerType::ResultsCache:
                output_handler = std::make_unique<ResultsCacheOutputHandler>(
                        command_line_args.get_mongodb_uri(),
                        command_line_args.get_mongodb_collection(),
                        command_line_args.get_batch_size(),
                        command_line_args.get_max_num_results()
                );
                break;
            default:
                SPDLOG_ERROR("Unhandled OutputHandlerType.");
                return false;
        }
    } catch (clp::TraceableException& e) {
        SPDLOG_ERROR("Failed to create output handler - {}", e.what());
        return false;
    }

    try {
        return search_archive(command_line_args, std::move(output_handler));
    } catch (TraceableException& e) {
        auto error_code = e.get_error_code();
        if (ErrorCode_errno == error_code) {
            SPDLOG_ERROR(
                    "Search failed: {}:{} {}, errno={}",
                    e.get_filename(),
                    e.get_line_number(),
                    e.what(),
                    errno
            );
        } else {
            SPDLOG_ERROR(
                    "Search failed: {}:{} {}, error_code={}",
                    e.get_filename(),
                    e.get_line_number(),
                    e.what(),
                    error_code
            );
        }
        return false;
    }
}

bool validate_archive_path(std::filesystem::path const& archive_path) {
    if (false == std::filesystem::exists(archive_path)) {
        SPDLOG_ERROR("Archive '{}' doesn't exist.", archive_path.string());
        return false;
    }
    auto const archive_metadata_file = archive_path / clp::streaming_archive::cMetadataFileName;
    if (false == std::filesystem::exists(archive_metadata_file)) {
        SPDLOG_ERROR(
                "Archive metadata file '{}' doesn't exist. '{}' may not be an archive.",
                archive_metadata_file.string(),
                archive_path.string()
        );
        return false;
    }
    return true;
}
}  // namespace

static SearchFilesResult search_file(
        Query& query,
        Archive& archive,
        MetadataDB::FileIterator& file_metadata_ix,
        std::unique_ptr<OutputHandler>& output_handler
) {
    File compressed_file;
    Message encoded_message;
    string decompressed_message;

    ErrorCode error_code = archive.open_file(compressed_file, file_metadata_ix);
    if (ErrorCode_Success != error_code) {
        string orig_path;
        file_metadata_ix.get_path(orig_path);
        if (ErrorCode_errno == error_code) {
            SPDLOG_ERROR("Failed to open {}, errno={}", orig_path.c_str(), errno);
        } else {
            SPDLOG_ERROR("Failed to open {}, error={}", orig_path.c_str(), error_code);
        }
        return SearchFilesResult::OpenFailure;
    }

    SearchFilesResult result = SearchFilesResult::Success;
    query.make_sub_queries_relevant_to_segment(compressed_file.get_segment_id());
    while (Grep::search_and_decompress(
            query,
            archive,
            compressed_file,
            encoded_message,
            decompressed_message
    ))
    {
        if (ErrorCode_Success
            != output_handler->add_result(
                    compressed_file.get_orig_path(),
                    compressed_file.get_orig_file_id_as_string(),
                    encoded_message,
                    decompressed_message
            ))
        {
            result = SearchFilesResult::ResultSendFailure;
            break;
        }
    }

    archive.close_file(compressed_file);
    return result;
}

void search_files(
        Query& query,
        Archive& archive,
        MetadataDB::FileIterator& file_metadata_ix,
        std::unique_ptr<OutputHandler>& output_handler,
        std::set<clp::segment_id_t> const& segments_to_search
) {
    if (query.contains_sub_queries()) {
        for (; file_metadata_ix.has_next(); file_metadata_ix.next()) {
            if (segments_to_search.count(file_metadata_ix.get_segment_id()) == 0) {
                continue;
            }

            if (output_handler->can_skip_file(file_metadata_ix)) {
                continue;
            }

            auto result = search_file(query, archive, file_metadata_ix, output_handler);
            if (SearchFilesResult::OpenFailure == result) {
                continue;
            }
            if (SearchFilesResult::ResultSendFailure == result) {
                break;
            }
        }
    } else {
        for (; file_metadata_ix.has_next(); file_metadata_ix.next()) {
            if (output_handler->can_skip_file(file_metadata_ix)) {
                continue;
            }

            auto result = search_file(query, archive, file_metadata_ix, output_handler);
            if (SearchFilesResult::OpenFailure == result) {
                continue;
            }
            if (SearchFilesResult::ResultSendFailure == result) {
                break;
            }
        }
    }
}

static bool search_archive(
        CommandLineArguments const& command_line_args,
        std::unique_ptr<OutputHandler> output_handler
) {
    std::filesystem::path const archive_path{command_line_args.get_archive_path()};
    if (false == validate_archive_path(archive_path)) {
        return false;
    }

    // Load lexers from schema file if it exists
    auto schema_file_path = archive_path / clp::streaming_archive::cSchemaFileName;
    unique_ptr<log_surgeon::lexers::ByteLexer> lexer;
    bool use_heuristic = true;
    if (std::filesystem::exists(schema_file_path)) {
        use_heuristic = false;
        // Create forward lexer
        lexer = std::make_unique<log_surgeon::lexers::ByteLexer>();
        load_lexer_from_file(schema_file_path.string(), false, *lexer);
    }

    Archive archive_reader;
    archive_reader.open(archive_path.string());
    archive_reader.refresh_dictionaries();

    auto search_begin_ts = command_line_args.get_search_begin_ts();
    auto search_end_ts = command_line_args.get_search_end_ts();

    auto query_processing_result = Grep::process_raw_query(
            archive_reader,
            command_line_args.get_search_string(),
            search_begin_ts,
            search_end_ts,
            command_line_args.ignore_case(),
            *lexer,
            use_heuristic
    );
    if (false == query_processing_result.has_value()) {
        return true;
    }

    auto& query = query_processing_result.value();
    // Get all segments potentially containing query results
    std::set<clp::segment_id_t> ids_of_segments_to_search;
    for (auto& sub_query : query.get_sub_queries()) {
        auto& ids_of_matching_segments = sub_query.get_ids_of_matching_segments();
        ids_of_segments_to_search.insert(
                ids_of_matching_segments.cbegin(),
                ids_of_matching_segments.cend()
        );
    }

    auto file_metadata_ix_ptr = archive_reader.get_file_iterator(
            search_begin_ts,
            search_end_ts,
            command_line_args.get_file_path(),
            true
    );
    auto& file_metadata_ix = *file_metadata_ix_ptr;
    search_files(
            query,
            archive_reader,
            file_metadata_ix,
            output_handler,
            ids_of_segments_to_search
    );
    file_metadata_ix_ptr.reset(nullptr);

    archive_reader.close();

    auto ecode = output_handler->flush();
    if (ErrorCode::ErrorCode_Success != ecode) {
        SPDLOG_ERROR(
                "Failed to flush output handler, error={}",
                clp::enum_to_underlying_type(ecode)
        );
        return false;
    }
    return true;
}

int main(int argc, char const* argv[]) {
    // Program-wide initialization
    try {
        auto stderr_logger = spdlog::stderr_logger_st("stderr");
        spdlog::set_default_logger(stderr_logger);
        spdlog::set_pattern("%Y-%m-%d %H:%M:%S,%e [%l] %v");
    } catch (std::exception& e) {
        // NOTE: We can't log an exception if the logger couldn't be constructed
        return -1;
    }
    clp::Profiler::init();
    clp::TimestampPattern::init();

    CommandLineArguments command_line_args("clo");
    auto parsing_result = command_line_args.parse_arguments(argc, argv);
    switch (parsing_result) {
        case CommandLineArgumentsBase::ParsingResult::Failure:
            return -1;
        case CommandLineArgumentsBase::ParsingResult::InfoCommand:
            return 0;
        case CommandLineArgumentsBase::ParsingResult::Success:
            // Continue processing
            break;
    }

    // mongocxx static init
    mongocxx::instance mongocxx_instance{};
    auto const& command = command_line_args.get_command();
    if (CommandLineArguments::Command::Search == command) {
        if (false == search(command_line_args)) {
            return -1;
        }
    } else if (CommandLineArguments::Command::ExtractIr == command) {
        if (false == extract_ir(command_line_args)) {
            return -1;
        }
    } else {
        SPDLOG_ERROR("Command {} not implemented.", clp::enum_to_underlying_type(command));
        return -1;
    }

    return 0;
}
