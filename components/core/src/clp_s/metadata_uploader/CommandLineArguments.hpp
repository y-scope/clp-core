#ifndef CLP_S_METADATA_UPLOADER_COMMANDLINEARGUMENTS_HPP
#define CLP_S_METADATA_UPLOADER_COMMANDLINEARGUMENTS_HPP

#include <string>
#include <optional>

#include "../../clp/GlobalMetadataDBConfig.hpp"

namespace clp_s::metadata_uploader {
/**
 * Class to parse command line arguments
 */
class CommandLineArguments {
public:
    // Types
    enum class ParsingResult {
        Success = 0,
        InfoCommand,
        Failure
    };

    // Constructors
    explicit CommandLineArguments(std::string const& program_name) : m_program_name(program_name) {}

    // Methods
    ParsingResult parse_arguments(int argc, char const* argv[]);

    std::string const& get_program_name() const { return m_program_name; }

    std::string const& get_archive_dir() const { return m_archive_dir; }

    std::string const& get_archive_id() const { return m_archive_id; }

    std::optional<clp::GlobalMetadataDBConfig> const& get_db_config() const {
        return m_metadata_db_config;
    }

private:
    // Methods
    void print_basic_usage() const;

    // Variables
    std::string m_program_name;
    std::string m_archive_dir;
    std::string m_archive_id;

    std::optional<clp::GlobalMetadataDBConfig> m_metadata_db_config;
};
}  // namespace clp_s::metadata_uploader

#endif  // CLP_S_METADATA_UPLOADER_COMMANDLINEARGUMENTS_HPP
