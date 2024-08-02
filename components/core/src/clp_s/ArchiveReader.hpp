#ifndef CLP_S_ARCHIVEREADER_HPP
#define CLP_S_ARCHIVEREADER_HPP

#include <map>
#include <set>
#include <span>
#include <string_view>
#include <utility>

#include <boost/filesystem.hpp>

#include "DictionaryReader.hpp"
#include "ReaderUtils.hpp"
#include "SchemaReader.hpp"
#include "TableReader.hpp"
#include "TimestampDictionaryReader.hpp"
#include "Utils.hpp"

namespace clp_s {
class ArchiveReader {
public:
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}
    };

    // Constructor
    ArchiveReader() : m_is_open(false) {}

    /**
     * Opens an archive for reading.
     * @param archives_dir
     * @param archive_id
     */
    void open(std::string_view archives_dir, std::string_view archive_id);

    /**
     * Reads the dictionaries and metadata.
     */
    void read_dictionaries_and_metadata();

    /**
     * Reads the variable dictionary from the archive.
     * @param lazy
     * @return the variable dictionary reader
     */
    std::shared_ptr<VariableDictionaryReader> read_variable_dictionary(bool lazy = false) {
        m_var_dict->read_new_entries(lazy);
        return m_var_dict;
    }

    /**
     * Reads the log type dictionary from the archive.
     * @param lazy
     * @return the log type dictionary reader
     */
    std::shared_ptr<LogTypeDictionaryReader> read_log_type_dictionary(bool lazy = false) {
        m_log_dict->read_new_entries(lazy);
        return m_log_dict;
    }

    /**
     * Reads the array dictionary from the archive.
     * @param lazy
     * @return the array dictionary reader
     */
    std::shared_ptr<LogTypeDictionaryReader> read_array_dictionary(bool lazy = false) {
        m_array_dict->read_new_entries(lazy);
        return m_array_dict;
    }

    /**
     * Reads the metadata from the archive.
     */
    void read_metadata();

    /**
     * Reads the local timestamp dictionary from the archive.
     * @return the timestamp dictionary reader
     */
    std::shared_ptr<TimestampDictionaryReader> read_timestamp_dictionary() {
        m_timestamp_dict->read_new_entries();
        return m_timestamp_dict;
    }

    /**
     * Reads a table from the archive.
     * @param schema_id
     * @param should_extract_timestamp
     * @param should_marshal_records
     * @return the schema reader
     */
    SchemaReader& read_schema_table(
            int32_t schema_id,
            bool should_extract_timestamp,
            bool should_marshal_records
    );

    /**
     * Loads all of the tables in the archive and returns SchemaReaders for them.
     * @return the schema readers for every table in the archive
     */
    std::vector<std::shared_ptr<SchemaReader>> read_all_tables();

    std::string_view get_archive_id() { return m_archive_id; }

    std::shared_ptr<VariableDictionaryReader> get_variable_dictionary() { return m_var_dict; }

    std::shared_ptr<LogTypeDictionaryReader> get_log_type_dictionary() { return m_log_dict; }

    std::shared_ptr<LogTypeDictionaryReader> get_array_dictionary() { return m_array_dict; }

    std::shared_ptr<TimestampDictionaryReader> get_timestamp_dictionary() {
        return m_timestamp_dict;
    }

    std::shared_ptr<SchemaTree> get_schema_tree() { return m_schema_tree; }

    std::shared_ptr<ReaderUtils::SchemaMap> get_schema_map() { return m_schema_map; }

    /**
     * Writes decoded messages to a file.
     * @param writer
     */
    void store(FileWriter& writer);

    /**
     * Closes the archive.
     */
    void close();

    /**
     * @return The schema ids in the archive. It also defines the order that tables should be read
     * in to avoid seeking backwards.
     */
    [[nodiscard]] std::vector<int32_t> const& get_schema_ids() const { return m_schema_ids; }

private:
    /**
     * Initializes a schema reader passed by reference to become a reader for a given schema.
     * @param reader
     * @param schema_id
     * @param should_extract_timestamp
     * @param should_marshal_records
     */
    void initialize_schema_reader(
            SchemaReader& reader,
            int32_t schema_id,
            bool should_extract_timestamp,
            bool should_marshal_records
    );

    /**
     * Appends a column to the schema reader.
     * @param reader
     * @param column_id
     * @return a pointer to the newly appended column reader or nullptr if no column reader was
     * created
     */
    BaseColumnReader* append_reader_column(SchemaReader& reader, int32_t column_id);

    /**
     * Appends columns for the entire schema of an unordered object.
     * @param reader
     * @param mst_subtree_root_node_id
     * @param schema_ids
     * @param should_marshal_records
     */
    void append_unordered_reader_columns(
            SchemaReader& reader,
            int32_t mst_subtree_root_node_id,
            std::span<int32_t> schema_ids,
            bool should_marshal_records
    );

    /**
     * Reads a table with given ID from the table reader. If read_table is called multiple times in
     * a row for the same table_id a cached buffer is returned. This function allows the caller to
     * ask for the same buffer to be reused to read multiple different tables: this can save memory
     * allocations, but can only be used when tables are read one at a time.
     * @param table_id
     * @param reuse_buffer when true the same buffer is reused across invocations, overwriting data
     * returned previous calls to read_table
     * @return a buffer containing the decompressed table identified by table_id
     */
    std::shared_ptr<char[]> read_table(size_t table_id, bool reuse_buffer);

    bool m_is_open;
    std::string m_archive_id;
    std::shared_ptr<VariableDictionaryReader> m_var_dict;
    std::shared_ptr<LogTypeDictionaryReader> m_log_dict;
    std::shared_ptr<LogTypeDictionaryReader> m_array_dict;
    std::shared_ptr<TimestampDictionaryReader> m_timestamp_dict;

    std::shared_ptr<SchemaTree> m_schema_tree;
    std::shared_ptr<ReaderUtils::SchemaMap> m_schema_map;
    std::vector<int32_t> m_schema_ids;
    std::map<int32_t, SchemaReader::SchemaMetadata> m_id_to_schema_metadata;

    TableReader m_table_reader;
    FileReader m_table_metadata_file_reader;
    ZstdDecompressor m_table_metadata_decompressor;
    SchemaReader m_schema_reader;
    std::shared_ptr<char[]> m_table_buffer{};
    size_t m_table_buffer_size{0ULL};
    size_t m_cur_table_id{0ULL};
};
}  // namespace clp_s

#endif  // CLP_S_ARCHIVEREADER_HPP
