#ifndef CLP_S_SCHEMAREADER_HPP
#define CLP_S_SCHEMAREADER_HPP

#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "ColumnReader.hpp"
#include "JsonSerializer.hpp"
#include "SchemaTree.hpp"
#include "search/Projection.hpp"
#include "ZstdDecompressor.hpp"

namespace clp_s {
class SchemaReader;

class FilterClass {
public:
    /**
     * Initializes the filter
     * @param reader
     * @param schema_id
     * @param column_readers
     */
    virtual void init(
            SchemaReader* reader,
            int32_t schema_id,
            std::vector<BaseColumnReader*> const& column_readers
    ) = 0;

    /**
     * Initializes the filter with a column map.
     * Note: the column map only contains the ordered columns in a schema.
     * @param reader
     * @param schema_id
     * @param column_map
     */
    virtual void init(
            SchemaReader* reader,
            int32_t schema_id,
            std::unordered_map<int32_t, BaseColumnReader*> const& column_map
    ) {}

    /**
     * Filters the message
     * @param cur_message
     * @return true if the message is accepted
     */
    virtual bool filter(uint64_t cur_message) = 0;
};

class SchemaReader {
public:
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}
    };

    struct SchemaMetadata {
        uint64_t stream_id;
        uint64_t stream_offset;
        uint64_t num_messages;
        uint64_t uncompressed_size;
    };

    // Constructor
    SchemaReader() {}

    // Destructor
    ~SchemaReader() { delete_columns(); }

    void delete_columns() {
        for (auto& i : m_columns) {
            delete i;
        }
    }

    /**
     * Resets the contents of this SchemaReader and prepares it to become a SchemaReader with a new
     * schema id, schema tree, and other parameters. After this call the SchemaReader is prepared
     * to accept append_column calls for the new schema.
     *
     * @param schema_tree
     * @param projection
     * @param schema_id
     * @param ordered_schema
     * @param num_messages
     * @param should_marshal_records
     */
    void reset(
            std::shared_ptr<SchemaTree> schema_tree,
            std::shared_ptr<search::Projection> projection,
            int32_t schema_id,
            std::span<int32_t> ordered_schema,
            uint64_t num_messages,
            bool should_marshal_records
    ) {
        m_schema_id = schema_id;
        m_num_messages = num_messages;
        m_cur_message = 0;
        m_serializer_initialized = false;
        m_ordered_schema = ordered_schema;
        delete_columns();
        m_column_map.clear();
        m_columns.clear();
        m_reordered_columns.clear();
        m_timestamp_column = nullptr;
        m_get_timestamp = []() -> epochtime_t { return 0; };
        m_log_event_idx_column = nullptr;
        m_local_id_to_global_id.clear();
        m_global_id_to_local_id.clear();
        m_global_id_to_unordered_object.clear();
        m_local_schema_tree.clear();
        m_json_serializer.clear();
        m_global_schema_tree = std::move(schema_tree);
        m_projection = std::move(projection);
        m_should_marshal_records = should_marshal_records;
    }

    /**
     * Appends a column to the schema reader
     * @param column_reader
     */
    void append_column(BaseColumnReader* column_reader);

    /**
     * Appends an unordered column to the schema reader
     * @param column_reader
     */
    void append_unordered_column(BaseColumnReader* column_reader);

    size_t get_column_size() { return m_columns.size(); }

    /**
     * Marks an unordered object for the purpose of marshalling records.
     * @param column_reader_start,
     * @param mst_subtree_root,
     * @param schema
     */
    void mark_unordered_object(
            size_t column_reader_start,
            int32_t mst_subtree_root,
            std::span<int32_t> schema
    );

    /**
     * Loads the encoded messages from a shared buffer starting at a given offset
     * @param stream_buffer
     * @param offset
     * @param uncompressed_size
     */
    void load(std::shared_ptr<char[]> stream_buffer, size_t offset, size_t uncompressed_size);

    /**
     * @return the number of messages in the schema
     */
    uint64_t get_num_messages() const { return m_num_messages; }

    /**
     * Gets next message
     * @param message
     * @return true if there is a next message
     */
    bool get_next_message(std::string& message);

    /**
     * Gets the next message matching a filter
     * @param message
     * @param filter
     * @return true if there is a next message
     */
    bool get_next_message(std::string& message, FilterClass* filter);

    /**
     * Gets the next message matching a filter as well as its timestamp and log event index.
     * @param message
     * @param timestamp
     * @param log_event_idx
     * @param filter
     * @return true if there is a next message
     */
    bool get_next_message_with_metadata(
            std::string& message,
            epochtime_t& timestamp,
            int64_t& log_event_idx,
            FilterClass* filter
    );

    /**
     * Initializes the filter
     * @param filter
     */
    void initialize_filter(FilterClass* filter);

    /**
     * Initializes the filter with a column map.
     * Note: the column map only contains the ordered columns in a schema.
     * @param filter
     */
    void initialize_filter_with_column_map(FilterClass* filter);

    /**
     * Marks a column as timestamp
     * @param column_reader
     */
    void mark_column_as_timestamp(BaseColumnReader* column_reader);

    /**
     * Marks a column as the log_event_idx column.
     */
    void mark_column_as_log_event_idx(Int64ColumnReader* column_reader) {
        m_log_event_idx_column = column_reader;
    }

    int32_t get_schema_id() const { return m_schema_id; }

    /**
     * @param schema
     * @return the first column ID found in the given schema, or -1 if the schema contains no
     * columns
     */
    static int32_t get_first_column_in_span(std::span<int32_t> schema);

    /**
     * @return the timestamp found in the row pointed to by m_cur_message
     */
    epochtime_t get_next_timestamp() const { return m_get_timestamp(); }

    /**
     * @return the log_event_idx in the row pointed to by m_cur_message or 0 if there is no
     * log_event_idx in this table.
     */
    int64_t get_next_log_event_idx() const;

    /**
     * @return true if all records in this table have been iterated over, false otherwise
     */
    bool done() const { return m_cur_message >= m_num_messages; }

private:
    /**
     * Merges the current local schema tree with the section of the global schema tree corresponding
     * to the path from the root of the global schema tree to the node matching the global MPT node
     * id passed to this function.
     * @param global_id
     */
    void generate_local_tree(int32_t global_id);

    /**
     * Generates a json template
     * @param id
     */
    void generate_json_template(int32_t id);

    /**
     * Generates a json template for a structured array
     * @param id
     * @param column_start the index of the first reader in m_columns belonging to this array
     * @param schema
     * @return the index of the next reader in m_columns after those consumed by this array
     */
    size_t
    generate_structured_array_template(int32_t id, size_t column_start, std::span<int32_t> schema);

    /**
     * Generates a json template for a structured object
     * @param id
     * @param column_start the index of the first reader in m_columns belonging to this object
     * @param schema
     * @return the index of the next reader in m_columns after those consumed by this object
     */
    size_t
    generate_structured_object_template(int32_t id, size_t column_start, std::span<int32_t> schema);

    /**
     * Finds the common root of the subtree containing cur_root and next_root, and adds brackets
     * and keys to m_json_serializer as necessary so that the json object is correct between the
     * previous field which is a child of cur_root, and the next field which is a child of
     * next_root.
     *
     * For example for the object {"a": {"b": "c"}, "d": {"e": {"f": "g"}}} after appending "b"
     * cur_root would be "a", and next_root would be "e". (since it is the parent of the next field
     * "f"). The current state of the object would look like "a":{"b":"c" -- to prepare for "f" we
     * would add },"d":{"e":{ or in other words close one bracket, add "d" and open bracket, add "e"
     * and open bracket. After adding field "f" the current root is "e", and the next root is the
     * original object which is the parent of "a" so we add }}.
     *
     * This works by tracing the path between both cur_root and next_root to their nearest common
     * ancestor. For every step cur_root takes towards this common ancestor we must close a bracket,
     * and for every step on the path from next_root a key must be added and a bracket must be
     * opened. The parameter `path_to_intersection` is used as a buffer to store the path from
     * next_root to this intersection so that the keys can be added to m_json_serializer in the
     * correct order.
     * @param cur_root
     * @param next_root
     * @param path_to_intersection
     */
    void find_intersection_and_fix_brackets(
            int32_t cur_root,
            int32_t next_root,
            std::vector<int32_t>& path_to_intersection
    );

    /**
     * Generates a json string from the extracted values
     */
    void generate_json_string();

    /**
     * Initializes all internal data structured required to serialize records.
     */
    void initialize_serializer();

    int32_t m_schema_id;
    uint64_t m_num_messages;
    uint64_t m_cur_message;
    std::span<int32_t> m_ordered_schema;

    std::unordered_map<int32_t, BaseColumnReader*> m_column_map;
    std::vector<BaseColumnReader*> m_columns;
    std::vector<BaseColumnReader*> m_reordered_columns;
    std::shared_ptr<char[]> m_stream_buffer;

    BaseColumnReader* m_timestamp_column;
    std::function<epochtime_t()> m_get_timestamp;
    Int64ColumnReader* m_log_event_idx_column{nullptr};

    std::shared_ptr<SchemaTree> m_global_schema_tree;
    SchemaTree m_local_schema_tree;
    std::unordered_map<int32_t, int32_t> m_global_id_to_local_id;
    std::unordered_map<int32_t, int32_t> m_local_id_to_global_id;

    JsonSerializer m_json_serializer;
    bool m_should_marshal_records{true};
    bool m_serializer_initialized{false};
    std::shared_ptr<search::Projection> m_projection;

    std::map<int32_t, std::pair<size_t, std::span<int32_t>>> m_global_id_to_unordered_object;
};
}  // namespace clp_s

#endif  // CLP_S_SCHEMAREADER_HPP
