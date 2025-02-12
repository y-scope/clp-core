#ifndef CLP_CLP_COMMANDLINEARGUMENTS_HPP
#define CLP_CLP_COMMANDLINEARGUMENTS_HPP

#include <string>
#include <vector>

#include <boost/asio.hpp>

#include "../CommandLineArgumentsBase.hpp"
#include "../GlobalMetadataDBConfig.hpp"

namespace clp::clp {
class CommandLineArguments : public CommandLineArgumentsBase {
public:
    // Types
    enum class Command : char {
        Compress = 'c',
        Extract = 'x',
        ExtractIr = 'i'
    };

    // Constructors
    explicit CommandLineArguments(std::string const& program_name)
            : CommandLineArgumentsBase(program_name),
              m_show_progress(false),
              m_single_file_archive(false),
              m_sort_input_files(true),
              m_print_archive_stats_progress(false),
              m_target_segment_uncompressed_size(1L * 1024 * 1024 * 1024),
              m_target_encoded_file_size(512L * 1024 * 1024),
              m_target_data_size_of_dictionaries(100L * 1024 * 1024),
              m_compression_level(3) {}

    // Methods
    ParsingResult parse_arguments(int argc, char const* argv[]) override;

    std::string const& get_path_list_path() const { return m_path_list_path; }

    std::string const& get_path_prefix_to_remove() const { return m_path_prefix_to_remove; }

    std::string const& get_output_dir() const { return m_output_dir; }

    std::string const& get_schema_file_path() const { return m_schema_file_path; }

    bool get_use_heuristic() const { return (m_schema_file_path.empty()); }

    bool show_progress() const { return m_show_progress; }

    [[nodiscard]] auto single_file_archive() const -> bool { return m_single_file_archive; }

    bool sort_input_files() const { return m_sort_input_files; }

    bool print_archive_stats_progress() const { return m_print_archive_stats_progress; }

    size_t get_target_encoded_file_size() const { return m_target_encoded_file_size; }

    size_t get_target_segment_uncompressed_size() const {
        return m_target_segment_uncompressed_size;
    }

    size_t get_target_data_size_of_dictionaries() const {
        return m_target_data_size_of_dictionaries;
    }

    int get_compression_level() const { return m_compression_level; }

    Command get_command() const { return m_command; }

    std::string const& get_archives_dir() const { return m_archives_dir; }

    std::vector<std::string> const& get_input_paths() const { return m_input_paths; }

    std::string const& get_orig_file_id() const { return m_orig_file_id; }

    size_t get_ir_msg_ix() const { return m_ir_msg_ix; }

    size_t get_ir_target_size() const { return m_ir_target_size; }

    GlobalMetadataDBConfig const& get_metadata_db_config() const { return m_metadata_db_config; }

private:
    // Methods
    void print_basic_usage() const override;
    void print_compression_basic_usage() const;
    void print_extraction_basic_usage() const;
    void print_ir_basic_usage() const;

    // Variables
    std::string m_path_list_path;
    std::string m_path_prefix_to_remove;
    std::string m_orig_file_id;
    size_t m_ir_msg_ix{0};
    size_t m_ir_target_size{128ULL * 1024 * 1024};
    bool m_sort_input_files;
    std::string m_output_dir;
    std::string m_schema_file_path;
    bool m_show_progress;
    bool m_single_file_archive;
    bool m_print_archive_stats_progress;
    size_t m_target_encoded_file_size;
    size_t m_target_segment_uncompressed_size;
    size_t m_target_data_size_of_dictionaries;
    int m_compression_level;
    Command m_command;
    std::string m_archives_dir;
    std::vector<std::string> m_input_paths;
    GlobalMetadataDBConfig m_metadata_db_config;
};
}  // namespace clp::clp

#endif  // CLP_CLP_COMMANDLINEARGUMENTS_HPP
