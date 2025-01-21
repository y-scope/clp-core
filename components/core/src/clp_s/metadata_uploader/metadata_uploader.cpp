#include <filesystem>

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include "../FileReader.hpp"
#include "../ReaderUtils.hpp"
#include "../ZstdDecompressor.hpp"
#include "CommandLineArguments.hpp"
#include "MySQLTableMetadataDB.hpp"
#include "TableMetadataManager.hpp"

using clp_s::metadata_uploader::CommandLineArguments;

int main(int argc, char const* argv[]) {
    try {
        auto stderr_logger = spdlog::stderr_logger_st("stderr");
        spdlog::set_default_logger(stderr_logger);
        spdlog::set_pattern("%Y-%m-%dT%H:%M:%S.%e%z [%l] %v");
    } catch (std::exception& e) {
        return 1;
    }

    CommandLineArguments command_line_arguments("metadata-uploader");
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

    clp_s::metadata_uploader::TableMetadataManager table_metadata_manager(
            command_line_arguments.get_db_config()
    );
    table_metadata_manager.update_metadata(
            command_line_arguments.get_archive_dir(),
            command_line_arguments.get_archive_id()
    );
}
