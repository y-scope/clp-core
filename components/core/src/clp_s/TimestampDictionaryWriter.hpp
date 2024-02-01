#ifndef CLP_S_TIMESTAMPDICTIONARYWRITER_HPP
#define CLP_S_TIMESTAMPDICTIONARYWRITER_HPP

#include <string>
#include <unordered_map>
#include <utility>

#include "FileWriter.hpp"
#include "SchemaTree.hpp"
#include "TimestampEntry.hpp"
#include "TimestampPattern.hpp"
#include "ZstdCompressor.hpp"

namespace clp_s {
class TimestampDictionaryWriter {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}
    };

    // Constructors
    TimestampDictionaryWriter(std::shared_ptr<SchemaTree> schema_tree)
            : m_is_open(false),
              m_is_open_local(false) {}

    /**
     * Opens the global timestamp dictionary for writing
     * @param dictionary_path
     * @param compression_level
     */
    void open(std::string const& dictionary_path, int compression_level);

    /**
     * Opens a local timestamp dictionary for writing
     * @param dictionary_path
     * @param compression_level
     */
    void open_local(std::string const& dictionary_path, int compression_level);

    /**
     * Closes the global timestamp dictionary
     */
    void close();

    /**
     * Closes the local timestamp dictionary
     */
    void close_local();

    /**
     * Writes the global timestamp dictionary to disk
     */
    void write_and_flush_to_disk();

    /**
     * Writes the local timestamp dictionary to disk
     */
    void write_local_and_flush_to_disk();

    /**
     * Gets the pattern id for a given pattern
     * @param pattern
     * @return the pattern id
     */
    uint64_t get_pattern_id(TimestampPattern const* pattern);

    /**
     * Ingests a timestamp entry
     * @param key
     * @param node_id
     * @param timestamp
     * @param pattern_id
     * @return the epoch time corresponding to the string timestamp
     */
    epochtime_t ingest_entry(
            std::string const& key,
            int32_t node_id,
            std::string const& timestamp,
            uint64_t& pattern_id
    );

    /**
     * Ingests a timestamp entry
     * @param column_key
     * @param node_id
     * @param timestamp
     */
    void ingest_entry(std::string const& key, int32_t node_id, double timestamp);

    void ingest_entry(std::string const& key, int32_t node_id, int64_t timestamp);

private:
    /**
     * Merges the local timestamp ranges into the global timestamp ranges
     */
    void merge_local_range();

    /**
     * Writes the timestamp entries to the disk
     * @param ranges
     * @param compressor
     */
    void write_timestamp_entries(
            std::map<std::string, TimestampEntry> const& ranges,
            ZstdCompressor& compressor
    );

    typedef std::unordered_map<TimestampPattern const*, uint64_t> pattern_to_id_t;

    // Variables
    bool m_is_open;
    bool m_is_open_local;

    // Variables related to on-disk storage
    FileWriter m_dictionary_file_writer;
    ZstdCompressor m_dictionary_compressor;
    FileWriter m_dictionary_file_writer_local;
    ZstdCompressor m_dictionary_compressor_local;

    pattern_to_id_t m_pattern_to_id;
    uint64_t m_next_id{};

    std::map<std::string, TimestampEntry> m_global_column_key_to_range;
    std::map<std::string, TimestampEntry> m_local_column_key_to_range;
    std::unordered_map<int32_t, TimestampEntry> m_local_column_id_to_range;
};
}  // namespace clp_s

#endif  // CLP_S_TIMESTAMPDICTIONARYWRITER_HPP
