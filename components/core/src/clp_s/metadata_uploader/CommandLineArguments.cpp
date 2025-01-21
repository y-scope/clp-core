#include "CommandLineArguments.hpp"

#include <iostream>

#include <boost/program_options.hpp>
#include <spdlog/spdlog.h>

namespace po = boost::program_options;

namespace clp_s::metadata_uploader {
CommandLineArguments::ParsingResult
CommandLineArguments::parse_arguments(int argc, char const** argv) {
    // Print out basic usage if user doesn't specify any options
    if (1 == argc) {
        print_basic_usage();
        return ParsingResult::Failure;
    }

    // Define general options
    po::options_description general_options("General Options");
    general_options.add_options()("help,h", "Print help");

    // Define output options
    po::options_description output_options("Output Options");
    std::string metadata_db_config_file_path;
    // clang-format off
    output_options.add_options()(
            "db-config-file",
            po::value<std::string>(&metadata_db_config_file_path)->value_name("FILE")
                ->default_value(metadata_db_config_file_path),
            "Table metadata DB YAML config"
    );
    // clang-format on

    // Define visible options
    po::options_description visible_options;
    visible_options.add(general_options);
    visible_options.add(output_options);

    // Define hidden positional options (not shown in Boost's program options help message)
    po::options_description positional_options;
    // clang-format off
    positional_options.add_options()
            ("archive-dir", po::value<std::string>(&m_archive_dir))
            ("archive-id", po::value<std::string>(&m_archive_id));
    // clang-format on
    po::positional_options_description positional_options_description;
    positional_options_description.add("archive-dir", 1);
    positional_options_description.add("archive-id", 1);

    // Aggregate all options
    po::options_description all_options;
    all_options.add(general_options);
    all_options.add(output_options);
    all_options.add(positional_options);

    // Parse options
    try {
        // Parse options specified on the command line
        po::parsed_options parsed = po::command_line_parser(argc, argv)
                                            .options(all_options)
                                            .positional(positional_options_description)
                                            .run();
        po::variables_map parsed_command_line_options;
        store(parsed, parsed_command_line_options);

        notify(parsed_command_line_options);

        // Handle --help
        if (parsed_command_line_options.count("help")) {
            if (argc > 2) {
                SPDLOG_WARN("Ignoring all options besides --help.");
            }

            print_basic_usage();

            std::cerr << visible_options << std::endl;
            return ParsingResult::InfoCommand;
        }

        // Validate required parameters
        if (m_archive_dir.empty()) {
            throw std::invalid_argument("ARCHIVE_DIR not specified or empty.");
        }
        if (m_archive_id.empty()) {
            throw std::invalid_argument("ARCHIVE_ID not specified or empty.");
        }
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
    } catch (std::exception& e) {
        SPDLOG_ERROR("{}", e.what());
        print_basic_usage();
        return ParsingResult::Failure;
    }

    return ParsingResult::Success;
}

void CommandLineArguments::print_basic_usage() const {
    std::cerr << "Usage: " << get_program_name() << " [OPTIONS] ARCHIVE_DIR ARCHIVE_ID"
              << std::endl;
}
}  // namespace clp_s::metadata_uploader
