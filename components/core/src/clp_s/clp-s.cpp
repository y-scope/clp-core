#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <json/single_include/nlohmann/json.hpp>
#include <mongocxx/instance.hpp>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include "../clp/CurlGlobalInstance.hpp"
#include "../clp/GlobalMySQLMetadataDB.hpp"
#include "../clp/streaming_archive/ArchiveMetadata.hpp"
#include "../reducer/network_utils.hpp"
#include "CommandLineArguments.hpp"
#include "Defs.hpp"
#include "JsonConstructor.hpp"
#include "JsonParser.hpp"
#include "search/AddTimestampConditions.hpp"
#include "search/ConvertToExists.hpp"
#include "search/EmptyExpr.hpp"
#include "search/EvaluateTimestampIndex.hpp"
#include "search/Expression.hpp"
#include "search/kql/kql.hpp"
#include "search/NarrowTypes.hpp"
#include "search/OrOfAndForm.hpp"
#include "search/Output.hpp"
#include "search/OutputHandler.hpp"
#include "search/Projection.hpp"
#include "search/SchemaMatch.hpp"
#include "TimestampPattern.hpp"
#include "TraceableException.hpp"
#include "Utils.hpp"

using namespace clp_s::search;
using clp_s::cArchiveFormatDevelopmentVersionFlag;
using clp_s::cEpochTimeMax;
using clp_s::cEpochTimeMin;
using clp_s::CommandLineArguments;
using clp_s::StringUtils;

namespace {
/**
 * Compresses the input files specified by the command line arguments into an archive.
 * @param command_line_arguments
 * @return Whether compression was successful
 */
bool compress(CommandLineArguments const& command_line_arguments);

/**
 * Decompresses the archive specified by the given JsonConstructorOption.
 * @param json_constructor_option
 */
void decompress_archive(clp_s::JsonConstructorOption const& json_constructor_option);

/**
 * Searches the given archive.
 * @param command_line_arguments
 * @param archive_reader
 * @param expr A copy of the search AST which may be modified
 * @param reducer_socket_fd
 * @return Whether the search succeeded
 */
bool search_archive(
        CommandLineArguments const& command_line_arguments,
        std::shared_ptr<clp_s::ArchiveReader> const& archive_reader,
        std::shared_ptr<Expression> expr,
        int reducer_socket_fd
);

bool compress(CommandLineArguments const& command_line_arguments) {
    auto archives_dir = std::filesystem::path(command_line_arguments.get_archives_dir());

    // Create output directory in case it doesn't exist
    try {
        std::filesystem::create_directory(archives_dir.string());
    } catch (std::exception& e) {
        SPDLOG_ERROR(
                "Failed to create archives directory {} - {}",
                archives_dir.string(),
                e.what()
        );
        return false;
    }

    clp_s::JsonParserOption option{};
    option.input_paths = command_line_arguments.get_input_paths();
    option.network_auth = command_line_arguments.get_network_auth();
    option.input_file_type = command_line_arguments.get_file_type();
    option.archives_dir = archives_dir.string();
    option.target_encoded_size = command_line_arguments.get_target_encoded_size();
    option.max_document_size = command_line_arguments.get_max_document_size();
    option.min_table_size = command_line_arguments.get_minimum_table_size();
    option.compression_level = command_line_arguments.get_compression_level();
    option.timestamp_key = command_line_arguments.get_timestamp_key();
    option.print_archive_stats = command_line_arguments.print_archive_stats();
    option.single_file_archive = command_line_arguments.get_single_file_archive();
    option.structurize_arrays = command_line_arguments.get_structurize_arrays();
    option.record_log_order = command_line_arguments.get_record_log_order();

    auto const& db_config_container = command_line_arguments.get_metadata_db_config();
    if (db_config_container.has_value()) {
        auto const& db_config = db_config_container.value();
        option.metadata_db = std::make_shared<clp::GlobalMySQLMetadataDB>(
                db_config.get_metadata_db_host(),
                db_config.get_metadata_db_port(),
                db_config.get_metadata_db_username(),
                db_config.get_metadata_db_password(),
                db_config.get_metadata_db_name(),
                db_config.get_metadata_table_prefix()
        );
    }

    clp_s::JsonParser parser(option);
    if (CommandLineArguments::FileType::KeyValueIr == option.input_file_type) {
        if (false == parser.parse_from_ir()) {
            SPDLOG_ERROR("Encountered error while parsing input");
            return false;
        }
    } else {
        if (false == parser.parse()) {
            SPDLOG_ERROR("Encountered error while parsing input");
            return false;
        }
    }
    parser.store();
    return true;
}

void decompress_archive(clp_s::JsonConstructorOption const& json_constructor_option) {
    clp_s::JsonConstructor constructor(json_constructor_option);
    constructor.store();
}

bool search_archive(
        CommandLineArguments const& command_line_arguments,
        std::shared_ptr<clp_s::ArchiveReader> const& archive_reader,
        std::shared_ptr<Expression> expr,
        int reducer_socket_fd
) {
    auto const& query = command_line_arguments.get_query();

    auto timestamp_dict = archive_reader->get_timestamp_dictionary();
    AddTimestampConditions add_timestamp_conditions(
            timestamp_dict->get_authoritative_timestamp_tokenized_column(),
            command_line_arguments.get_search_begin_ts(),
            command_line_arguments.get_search_end_ts()
    );
    if (expr = add_timestamp_conditions.run(expr); std::dynamic_pointer_cast<EmptyExpr>(expr)) {
        SPDLOG_ERROR(
                "Query '{}' specified timestamp filters tge {} tle {}, but no authoritative "
                "timestamp column was found for this archive",
                query,
                command_line_arguments.get_search_begin_ts().value_or(cEpochTimeMin),
                command_line_arguments.get_search_end_ts().value_or(cEpochTimeMax)
        );
        return false;
    }

    OrOfAndForm standardize_pass;
    if (expr = standardize_pass.run(expr); std::dynamic_pointer_cast<EmptyExpr>(expr)) {
        SPDLOG_ERROR("Query '{}' is logically false", query);
        return false;
    }

    NarrowTypes narrow_pass;
    if (expr = narrow_pass.run(expr); std::dynamic_pointer_cast<EmptyExpr>(expr)) {
        SPDLOG_ERROR("Query '{}' is logically false", query);
        return false;
    }

    ConvertToExists convert_pass;
    if (expr = convert_pass.run(expr); std::dynamic_pointer_cast<EmptyExpr>(expr)) {
        SPDLOG_ERROR("Query '{}' is logically false", query);
        return false;
    }

    // skip decompressing the archive if we won't match based on
    // the timestamp index
    EvaluateTimestampIndex timestamp_index(timestamp_dict);
    if (clp_s::EvaluatedValue::False == timestamp_index.run(expr)) {
        SPDLOG_INFO("No matching timestamp ranges for query '{}'", query);
        return true;
    }

    // Narrow against schemas
    SchemaMatch match_pass(archive_reader->get_schema_tree(), archive_reader->get_schema_map());
    if (expr = match_pass.run(expr); std::dynamic_pointer_cast<EmptyExpr>(expr)) {
        SPDLOG_INFO("No matching schemas for query '{}'", query);
        return true;
    }

    // Populate projection
    auto projection = std::make_shared<Projection>(
            command_line_arguments.get_projection_columns().empty()
                    ? ProjectionMode::ReturnAllColumns
                    : ProjectionMode::ReturnSelectedColumns
    );
    try {
        for (auto const& column : command_line_arguments.get_projection_columns()) {
            std::vector<std::string> descriptor_tokens;
            if (false == StringUtils::tokenize_column_descriptor(column, descriptor_tokens)) {
                SPDLOG_ERROR("Can not tokenize invalid column: \"{}\"", column);
                return false;
            }
            projection->add_column(ColumnDescriptor::create_from_escaped_tokens(descriptor_tokens));
        }
    } catch (clp_s::TraceableException& e) {
        SPDLOG_ERROR("{}", e.what());
        return false;
    }
    projection->resolve_columns(archive_reader->get_schema_tree());
    archive_reader->set_projection(projection);

    std::unique_ptr<OutputHandler> output_handler;
    try {
        switch (command_line_arguments.get_output_handler_type()) {
            case CommandLineArguments::OutputHandlerType::Network:
                output_handler = std::make_unique<NetworkOutputHandler>(
                        command_line_arguments.get_network_dest_host(),
                        command_line_arguments.get_network_dest_port()
                );
                break;
            case CommandLineArguments::OutputHandlerType::Reducer:
                if (command_line_arguments.do_count_results_aggregation()) {
                    output_handler = std::make_unique<CountOutputHandler>(reducer_socket_fd);
                } else if (command_line_arguments.do_count_by_time_aggregation()) {
                    output_handler = std::make_unique<CountByTimeOutputHandler>(
                            reducer_socket_fd,
                            command_line_arguments.get_count_by_time_bucket_size()
                    );
                } else {
                    SPDLOG_ERROR("Unhandled aggregation type.");
                    return false;
                }
                break;
            case CommandLineArguments::OutputHandlerType::ResultsCache:
                output_handler = std::make_unique<ResultsCacheOutputHandler>(
                        command_line_arguments.get_mongodb_uri(),
                        command_line_arguments.get_mongodb_collection(),
                        command_line_arguments.get_batch_size(),
                        command_line_arguments.get_max_num_results()
                );
                break;
            case CommandLineArguments::OutputHandlerType::Stdout:
                output_handler = std::make_unique<StandardOutputHandler>();
                break;
            default:
                SPDLOG_ERROR("Unhandled OutputHandlerType.");
                return false;
        }
    } catch (clp_s::TraceableException& e) {
        SPDLOG_ERROR("Failed to create output handler - {}", e.what());
        return false;
    }

    // output result
    Output output(
            match_pass,
            expr,
            archive_reader,
            timestamp_dict,
            std::move(output_handler),
            command_line_arguments.get_ignore_case()
    );
    return output.filter();
}
}  // namespace

int main(int argc, char const* argv[]) {
    try {
        auto stderr_logger = spdlog::stderr_logger_st("stderr");
        spdlog::set_default_logger(stderr_logger);
        spdlog::set_pattern("%Y-%m-%dT%H:%M:%S.%e%z [%l] %v");
    } catch (std::exception& e) {
        // NOTE: We can't log an exception if the logger couldn't be constructed
        return 1;
    }

    clp_s::TimestampPattern::init();
    mongocxx::instance const mongocxx_instance{};
    clp::CurlGlobalInstance const curl_instance{};

    CommandLineArguments command_line_arguments("clp-s");
    auto parsing_result = command_line_arguments.parse_arguments(argc, argv);
    switch (parsing_result) {
        case CommandLineArguments::ParsingResult::Failure:
            return 1;
        case CommandLineArguments::ParsingResult::InfoCommand:
            return 0;
        case CommandLineArguments::ParsingResult::Success:
            // Continue processing
            break;
    }

    if (CommandLineArguments::Command::Compress == command_line_arguments.get_command()) {
        try {
            if (false == compress(command_line_arguments)) {
                return 1;
            }
        } catch (std::exception const& e) {
            SPDLOG_ERROR("Encountered error during compression - {}", e.what());
            return 1;
        }
    } else if (CommandLineArguments::Command::Extract == command_line_arguments.get_command()) {
        clp_s::JsonConstructorOption option{};
        option.output_dir = command_line_arguments.get_output_dir();
        option.ordered = command_line_arguments.get_ordered_decompression();
        option.target_ordered_chunk_size = command_line_arguments.get_target_ordered_chunk_size();
        option.print_ordered_chunk_stats = command_line_arguments.print_ordered_chunk_stats();
        option.network_auth = command_line_arguments.get_network_auth();
        if (false == command_line_arguments.get_mongodb_uri().empty()) {
            option.metadata_db
                    = {command_line_arguments.get_mongodb_uri(),
                       command_line_arguments.get_mongodb_collection()};
        }

        try {
            for (auto const& archive_path : command_line_arguments.get_input_paths()) {
                option.archive_path = archive_path;
                decompress_archive(option);
            }
        } catch (std::exception const& e) {
            SPDLOG_ERROR("Encountered error during decompression - {}", e.what());
            return 1;
        }
    } else {
        auto const& query = command_line_arguments.get_query();
        auto query_stream = std::istringstream(query);
        auto expr = kql::parse_kql_expression(query_stream);
        if (nullptr == expr) {
            return 1;
        }

        if (std::dynamic_pointer_cast<EmptyExpr>(expr)) {
            SPDLOG_ERROR("Query '{}' is logically false", query);
            return 1;
        }

        int reducer_socket_fd{-1};
        if (command_line_arguments.get_output_handler_type()
            == CommandLineArguments::OutputHandlerType::Reducer)
        {
            reducer_socket_fd = reducer::connect_to_reducer(
                    command_line_arguments.get_reducer_host(),
                    command_line_arguments.get_reducer_port(),
                    command_line_arguments.get_job_id()
            );
            if (-1 == reducer_socket_fd) {
                SPDLOG_ERROR("Failed to connect to reducer");
                return 1;
            }
        }

        auto archive_reader = std::make_shared<clp_s::ArchiveReader>();
        for (auto const& archive_path : command_line_arguments.get_input_paths()) {
            try {
                archive_reader->open(archive_path, command_line_arguments.get_network_auth());
            } catch (std::exception const& e) {
                SPDLOG_ERROR("Failed to open archive - {}", e.what());
                return 1;
            }
            if (false
                == search_archive(
                        command_line_arguments,
                        archive_reader,
                        expr->copy(),
                        reducer_socket_fd
                ))
            {
                return 1;
            }
            archive_reader->close();
        }
    }

    return 0;
}
