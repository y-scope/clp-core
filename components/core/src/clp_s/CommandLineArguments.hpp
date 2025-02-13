#ifndef CLP_S_COMMANDLINEARGUMENTS_HPP
#define CLP_S_COMMANDLINEARGUMENTS_HPP

#include <optional>
#include <string>
#include <vector>

#include <boost/program_options/option.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#include "../clp/GlobalMetadataDBConfig.hpp"
#include "../reducer/types.hpp"
#include "Defs.hpp"
#include "InputConfig.hpp"

namespace clp_s {
class CommandLineArguments {
public:
    // Types
    enum class ParsingResult {
        Success = 0,
        InfoCommand,
        Failure
    };

    enum class Command : char {
        Compress = 'c',
        Extract = 'x',
        Search = 's',
        JsonToIr = 'r'
    };

    enum class OutputHandlerType : uint8_t {
        Network = 0,
        Reducer,
        ResultsCache,
        Stdout,
    };

    enum class FileType : uint8_t {
        Json = 0,
        KeyValueIr
    };

    // Constructors
    explicit CommandLineArguments(std::string const& program_name) : m_program_name(program_name) {}

    // Methods
    ParsingResult parse_arguments(int argc, char const* argv[]);

    std::string const& get_program_name() const { return m_program_name; }

    Command get_command() const { return m_command; }

    std::vector<Path> const& get_input_paths() const { return m_input_paths; }

    NetworkAuthOption const& get_network_auth() const { return m_network_auth; }

    std::string const& get_archives_dir() const { return m_archives_dir; }

    std::string const& get_output_dir() const { return m_output_dir; }

    std::string const& get_timestamp_key() const { return m_timestamp_key; }

    int get_compression_level() const { return m_compression_level; }

    size_t get_target_encoded_size() const { return m_target_encoded_size; }

    size_t get_max_document_size() const { return m_max_document_size; }

    [[nodiscard]] auto get_max_ir_buffer_size() const -> size_t { return m_max_ir_buffer_size; }

    [[nodiscard]] auto get_encoding_type() const -> int { return m_encoding_type; }

    [[nodiscard]] bool print_archive_stats() const { return m_print_archive_stats; }

    [[nodiscard]] auto print_ordered_chunk_stats() const -> bool {
        return m_print_ordered_chunk_stats;
    }

    std::string const& get_mongodb_uri() const { return m_mongodb_uri; }

    std::string const& get_mongodb_collection() const { return m_mongodb_collection; }

    uint64_t get_batch_size() const { return m_batch_size; }

    uint64_t get_max_num_results() const { return m_max_num_results; }

    std::string const& get_network_dest_host() const { return m_network_dest_host; }

    int const& get_network_dest_port() const { return m_network_dest_port; }

    std::string const& get_query() const { return m_query; }

    std::optional<epochtime_t> get_search_begin_ts() const { return m_search_begin_ts; }

    std::optional<epochtime_t> get_search_end_ts() const { return m_search_end_ts; }

    bool get_ignore_case() const { return m_ignore_case; }

    std::optional<clp::GlobalMetadataDBConfig> const& get_metadata_db_config() const {
        return m_metadata_db_config;
    }

    std::string const& get_reducer_host() const { return m_reducer_host; }

    int get_reducer_port() const { return m_reducer_port; }

    reducer::job_id_t get_job_id() const { return m_job_id; }

    bool do_count_results_aggregation() const { return m_do_count_results_aggregation; }

    bool do_count_by_time_aggregation() const { return m_do_count_by_time_aggregation; }

    int64_t get_count_by_time_bucket_size() const { return m_count_by_time_bucket_size; }

    OutputHandlerType get_output_handler_type() const { return m_output_handler_type; }

    bool get_single_file_archive() const { return m_single_file_archive; }

    bool get_structurize_arrays() const { return m_structurize_arrays; }

    bool get_ordered_decompression() const { return m_ordered_decompression; }

    size_t get_target_ordered_chunk_size() const { return m_target_ordered_chunk_size; }

    size_t get_minimum_table_size() const { return m_minimum_table_size; }

    std::vector<std::string> const& get_projection_columns() const { return m_projection_columns; }

    bool get_record_log_order() const { return false == m_disable_log_order; }

    [[nodiscard]] auto get_file_type() const -> FileType { return m_file_type; }

private:
    // Methods
    /**
     * Validates output options related to the Network Destination output handler.
     * @param options_description
     * @param options Vector of options previously parsed by boost::program_options and which may
     * contain options that have the unrecognized flag set
     * @param parsed_options Returns any parsed options that were newly recognized
     */
    void parse_network_dest_output_handler_options(
            boost::program_options::options_description const& options_description,
            std::vector<boost::program_options::option> const& options,
            boost::program_options::variables_map& parsed_options
    );

    /**
     * Validates output options related to the Reducer output handler.
     * @param options_description
     * @param options Vector of options previously parsed by boost::program_options and which may
     * contain options that have the unrecognized flag set
     * @param parsed_options Returns any parsed options that were newly recognized
     */
    void parse_reducer_output_handler_options(
            boost::program_options::options_description const& options_description,
            std::vector<boost::program_options::option> const& options,
            boost::program_options::variables_map& parsed_options
    );

    /**
     * Validates output options related to the Results Cache output handler.
     * @param options_description
     * @param options Vector of options previously parsed by boost::program_options and which may
     * contain options that have the unrecognized flag set
     * @param parsed_options Returns any parsed options that were newly recognized
     */
    void parse_results_cache_output_handler_options(
            boost::program_options::options_description const& options_description,
            std::vector<boost::program_options::option> const& options,
            boost::program_options::variables_map& parsed_options
    );

    void print_basic_usage() const;

    void print_compression_usage() const;

    void print_decompression_usage() const;

    void print_json_to_ir_usage() const;

    void print_search_usage() const;

    // Variables
    std::string m_program_name;
    Command m_command;

    // Compression and decompression variables
    std::vector<Path> m_input_paths;
    NetworkAuthOption m_network_auth{};
    std::string m_archives_dir;
    std::string m_output_dir;
    std::string m_timestamp_key;
    int m_compression_level{3};
    size_t m_target_encoded_size{8ULL * 1024 * 1024 * 1024};  // 8 GiB
    bool m_print_archive_stats{false};
    size_t m_max_document_size{512ULL * 1024 * 1024};  // 512 MB
    bool m_single_file_archive{false};
    bool m_structurize_arrays{false};
    bool m_ordered_decompression{false};
    size_t m_target_ordered_chunk_size{};
    bool m_print_ordered_chunk_stats{false};
    size_t m_minimum_table_size{1ULL * 1024 * 1024};  // 1 MB
    bool m_disable_log_order{false};
    FileType m_file_type{FileType::Json};
    int m_encoding_type{8};
    size_t m_max_ir_buffer_size{512ULL * 1024 * 1024};

    // Metadata db variables
    std::optional<clp::GlobalMetadataDBConfig> m_metadata_db_config;

    // MongoDB configuration variables
    std::string m_mongodb_uri;
    std::string m_mongodb_collection;
    uint64_t m_batch_size{1000};
    uint64_t m_max_num_results{1000};

    // Network configuration variables
    std::string m_network_dest_host;
    int m_network_dest_port;

    // Search variables
    std::string m_query;
    std::optional<epochtime_t> m_search_begin_ts;
    std::optional<epochtime_t> m_search_end_ts;
    bool m_ignore_case{false};
    std::vector<std::string> m_projection_columns;

    // Search aggregation variables
    std::string m_reducer_host;
    int m_reducer_port{-1};
    reducer::job_id_t m_job_id{-1};
    bool m_do_count_results_aggregation{false};
    bool m_do_count_by_time_aggregation{false};
    int64_t m_count_by_time_bucket_size{0};  // Milliseconds

    OutputHandlerType m_output_handler_type{OutputHandlerType::Stdout};
};
}  // namespace clp_s

#endif  // CLP_S_COMMANDLINEARGUMENTS_HPP
