#include "ArchiveWriter.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>

#include <json/single_include/nlohmann/json.hpp>

#include "archive_constants.hpp"
#include "Defs.hpp"
#include "SchemaTree.hpp"

namespace clp_s {
void ArchiveWriter::open(ArchiveWriterOption const& option) {
    m_id = boost::uuids::to_string(option.id);
    m_compression_level = option.compression_level;
    m_print_archive_stats = option.print_archive_stats;
    m_single_file_archive = option.single_file_archive;
    m_min_table_size = option.min_table_size;
    m_archives_dir = option.archives_dir;
    std::string working_dir_name = m_id;
    if (option.single_file_archive) {
        working_dir_name += constants::cTmpPostfix;
    }
    auto archive_path = std::filesystem::path(option.archives_dir) / working_dir_name;

    std::error_code ec;
    if (std::filesystem::exists(archive_path, ec)) {
        SPDLOG_ERROR("Archive path already exists: {}", archive_path.c_str());
        throw OperationFailed(ErrorCodeUnsupported, __FILENAME__, __LINE__);
    }

    m_archive_path = archive_path.string();
    if (false == std::filesystem::create_directory(m_archive_path, ec)) {
        SPDLOG_ERROR(
                "Failed to create archive directory \"{}\" - ({}) {}",
                m_archive_path,
                ec.value(),
                ec.message()
        );
        throw OperationFailed(ErrorCodeFailure, __FILENAME__, __LINE__);
    }

    std::string var_dict_path = m_archive_path + constants::cArchiveVarDictFile;
    m_var_dict = std::make_shared<VariableDictionaryWriter>();
    m_var_dict->open(var_dict_path, m_compression_level, UINT64_MAX);

    std::string log_dict_path = m_archive_path + constants::cArchiveLogDictFile;
    m_log_dict = std::make_shared<LogTypeDictionaryWriter>();
    m_log_dict->open(log_dict_path, m_compression_level, UINT64_MAX);

    std::string array_dict_path = m_archive_path + constants::cArchiveArrayDictFile;
    m_array_dict = std::make_shared<LogTypeDictionaryWriter>();
    m_array_dict->open(array_dict_path, m_compression_level, UINT64_MAX);
}

void ArchiveWriter::close() {
    auto var_dict_compressed_size = m_var_dict->close();
    auto log_dict_compressed_size = m_log_dict->close();
    auto array_dict_compressed_size = m_array_dict->close();
    auto schema_tree_compressed_size = m_schema_tree.store(m_archive_path, m_compression_level);
    auto schema_map_compressed_size = m_schema_map.store(m_archive_path, m_compression_level);
    auto [table_metadata_compressed_size, table_compressed_size] = store_tables();

    std::vector<ArchiveFileInfo> files{
            {constants::cArchiveSchemaTreeFile, schema_tree_compressed_size},
            {constants::cArchiveSchemaMapFile, schema_map_compressed_size},
            {constants::cArchiveTableMetadataFile, table_metadata_compressed_size},
            {constants::cArchiveVarDictFile, var_dict_compressed_size},
            {constants::cArchiveLogDictFile, log_dict_compressed_size},
            {constants::cArchiveArrayDictFile, array_dict_compressed_size},
            {constants::cArchiveTablesFile, table_compressed_size}
    };
    uint64_t offset = 0;
    for (auto& file : files) {
        uint64_t original_size = file.o;
        file.o = offset;
        offset += original_size;
    }

    if (m_single_file_archive) {
        write_single_file_archive(files);
    } else {
        FileWriter header_and_metadata_writer;
        header_and_metadata_writer.open(
                m_archive_path + constants::cArchiveHeaderFile,
                FileWriter::OpenMode::CreateForWriting
        );
        write_archive_metadata(header_and_metadata_writer, files);
        size_t metadata_size = header_and_metadata_writer.get_pos() - sizeof(ArchiveHeader);

        m_compressed_size
                = var_dict_compressed_size + log_dict_compressed_size + array_dict_compressed_size
                  + metadata_size + schema_tree_compressed_size + schema_map_compressed_size
                  + table_metadata_compressed_size + table_compressed_size + sizeof(ArchiveHeader);

        write_archive_header(header_and_metadata_writer, metadata_size);
        header_and_metadata_writer.close();
    }

    if (m_metadata_db) {
        update_metadata_db();
    }

    if (m_print_archive_stats) {
        print_archive_stats();
    }

    m_id_to_schema_writer.clear();
    m_schema_tree.clear();
    m_schema_map.clear();
    m_timestamp_dict.clear();
    m_encoded_message_size = 0UL;
    m_uncompressed_size = 0UL;
    m_compressed_size = 0UL;
    m_next_log_event_id = 0;
}

void ArchiveWriter::write_single_file_archive(std::vector<ArchiveFileInfo> const& files) {
    std::string single_file_archive_path = (std::filesystem::path(m_archives_dir) / m_id).string();
    FileWriter archive_writer;
    archive_writer.open(single_file_archive_path, FileWriter::OpenMode::CreateForWriting);

    write_archive_metadata(archive_writer, files);
    size_t metadata_section_size = archive_writer.get_pos() - sizeof(ArchiveHeader);
    write_archive_files(archive_writer, files);
    m_compressed_size = archive_writer.get_pos();
    write_archive_header(archive_writer, metadata_section_size);

    archive_writer.close();
    std::error_code ec;
    if (false == std::filesystem::remove(m_archive_path, ec)) {
        throw OperationFailed(ErrorCodeFileExists, __FILENAME__, __LINE__);
    }
}

void ArchiveWriter::write_archive_metadata(
        FileWriter& archive_writer,
        std::vector<ArchiveFileInfo> const& files
) {
    archive_writer.seek_from_begin(sizeof(ArchiveHeader));

    ZstdCompressor compressor;
    compressor.open(archive_writer, m_compression_level);
    uint8_t const num_packets{3U};
    compressor.write_numeric_value(num_packets);

    // Write archive info
    ArchiveInfoPacket archive_info{.num_segments = 1};
    std::stringstream msgpack_buffer;
    msgpack::pack(msgpack_buffer, archive_info);
    std::string archive_info_str = msgpack_buffer.str();
    compressor.write_numeric_value(ArchiveMetadataPacketType::ArchiveInfo);
    compressor.write_numeric_value(static_cast<uint32_t>(archive_info_str.size()));
    compressor.write_string(archive_info_str);

    ArchiveFileInfoPacket archive_file_info{.files{files}};
    msgpack_buffer = std::stringstream{};
    msgpack::pack(msgpack_buffer, archive_file_info);
    std::string archive_file_info_str = msgpack_buffer.str();
    compressor.write_numeric_value(ArchiveMetadataPacketType::ArchiveFileInfo);
    compressor.write_numeric_value(static_cast<uint32_t>(archive_file_info_str.size()));
    compressor.write_string(archive_file_info_str);

    // Write timestamp dictionary
    compressor.write_numeric_value(ArchiveMetadataPacketType::TimestampDictionary);
    std::stringstream timestamp_dict_stream;
    m_timestamp_dict.write(timestamp_dict_stream);
    std::string encoded_timestamp_dict = timestamp_dict_stream.str();
    compressor.write_numeric_value(static_cast<uint32_t>(encoded_timestamp_dict.size()));
    compressor.write(encoded_timestamp_dict.data(), encoded_timestamp_dict.size());

    compressor.close();
}

void ArchiveWriter::write_archive_files(
        FileWriter& archive_writer,
        std::vector<ArchiveFileInfo> const& files
) {
    for (auto const& file : files) {
        std::string file_path = m_archive_path + file.n;
        clp::FileReader reader(file_path);
        char read_buffer[cReadBlockSize];
        while (true) {
            size_t num_bytes_read{0};
            clp::ErrorCode const error_code
                    = reader.try_read(read_buffer, cReadBlockSize, num_bytes_read);
            if (clp::ErrorCode::ErrorCode_EndOfFile == error_code) {
                break;
            } else if (clp::ErrorCode::ErrorCode_Success != error_code) {
                throw OperationFailed(static_cast<ErrorCode>(error_code), __FILENAME__, __LINE__);
            }
            archive_writer.write(read_buffer, num_bytes_read);
        }
        if (false == std::filesystem::remove(file_path)) {
            throw OperationFailed(ErrorCodeFileExists, __FILENAME__, __LINE__);
        }
    }
}

void ArchiveWriter::write_archive_header(FileWriter& archive_writer, size_t metadata_section_size) {
    ArchiveHeader header{
            .magic_number{0},
            .version
            = (cArchiveMajorVersion << 24) | (cArchiveMinorVersion << 16) | cArchivePatchVersion,
            .uncompressed_size = m_uncompressed_size,
            .compressed_size = m_compressed_size,
            .reserved_padding{0},
            .metadata_section_size = static_cast<uint32_t>(metadata_section_size),
            .compression_type = static_cast<uint16_t>(ArchiveCompressionType::Zstd),
            .padding = 0
    };
    std::memcpy(&header.magic_number, cStructuredSFAMagicNumber, sizeof(header.magic_number));
    archive_writer.seek_from_begin(0);
    archive_writer.write(reinterpret_cast<char const*>(&header), sizeof(header));
}

void
ArchiveWriter::append_message(int32_t schema_id, Schema const& schema, ParsedMessage& message) {
    SchemaWriter* schema_writer;
    auto it = m_id_to_schema_writer.find(schema_id);
    if (it != m_id_to_schema_writer.end()) {
        schema_writer = it->second;
    } else {
        schema_writer = new SchemaWriter();
        initialize_schema_writer(schema_writer, schema);
        m_id_to_schema_writer[schema_id] = schema_writer;
    }

    m_encoded_message_size += schema_writer->append_message(message);
    ++m_next_log_event_id;
}

size_t ArchiveWriter::get_data_size() {
    return m_log_dict->get_data_size() + m_var_dict->get_data_size() + m_array_dict->get_data_size()
           + m_encoded_message_size;
}

void ArchiveWriter::initialize_schema_writer(SchemaWriter* writer, Schema const& schema) {
    for (int32_t id : schema) {
        if (Schema::schema_entry_is_unordered_object(id)) {
            continue;
        }
        auto const& node = m_schema_tree.get_node(id);
        switch (node.get_type()) {
            case NodeType::Integer:
                writer->append_column(new Int64ColumnWriter(id));
                break;
            case NodeType::Float:
                writer->append_column(new FloatColumnWriter(id));
                break;
            case NodeType::ClpString:
                writer->append_column(new ClpStringColumnWriter(id, m_var_dict, m_log_dict));
                break;
            case NodeType::VarString:
                writer->append_column(new VariableStringColumnWriter(id, m_var_dict));
                break;
            case NodeType::Boolean:
                writer->append_column(new BooleanColumnWriter(id));
                break;
            case NodeType::UnstructuredArray:
                writer->append_column(new ClpStringColumnWriter(id, m_var_dict, m_array_dict));
                break;
            case NodeType::DateString:
                writer->append_column(new DateStringColumnWriter(id));
                break;
            case NodeType::Metadata:
            case NodeType::NullValue:
            case NodeType::Object:
            case NodeType::StructuredArray:
            case NodeType::Unknown:
                break;
        }
    }
}

std::pair<size_t, size_t> ArchiveWriter::store_tables() {
    m_tables_file_writer.open(
            m_archive_path + constants::cArchiveTablesFile,
            FileWriter::OpenMode::CreateForWriting
    );
    m_table_metadata_file_writer.open(
            m_archive_path + constants::cArchiveTableMetadataFile,
            FileWriter::OpenMode::CreateForWriting
    );
    m_table_metadata_compressor.open(m_table_metadata_file_writer, m_compression_level);

    /**
     * Packed stream metadata schema
     * ------------------------------
     * Schema tables are packed into a series of compression streams. Each of those compression
     * streams is identified by a 64 bit stream id. In the first half of the metadata we identify
     * how many streams there are, and the offset into the file where each compression stream can
     * be found. In the second half of the metadata we record how many schema tables there are,
     * which compression stream they belong to, the offset into that compression stream where
     * they can be found, and how many messages that schema table contains.
     *
     * Section 1: Compression Streams Metadata
     * - Contains metadata about each compression stream.
     * - Structure:
     *   - Number of packed streams: <64-bit integer>
     *   - For each stream:
     *     - Offset into the file: <64-bit integer>
     *     - Uncompressed size: <64-bit integer>
     *   - Number of separate column schemas: <64-bit integer>
     *     It is always 0 in the current implementation.
     *   - Undefined section for separate column schemas, reserved for future support.
     *
     * Section 2: Schema Tables Metadata
     * - Contains metadata about schema tables associated with each compression stream.
     * - Structure:
     *   - Number of schema tables: <64-bit integer>
     *   - For each schema table:
     *     - Stream ID: <64-bit integer>
     *     - Offset into the stream: <64-bit integer>
     *     - Schema ID: <32-bit integer>
     *     - Number of messages: <64-bit integer>
     *
     * We buffer the first half of the metadata in the "stream_metadata" vector, and the second half
     * of the metadata in the "schema_metadata" vector as we compress the tables. The metadata is
     * flushed once all of the schema tables have been compressed.
     */
    using schema_map_it = decltype(m_id_to_schema_writer)::iterator;
    std::vector<schema_map_it> schemas;
    std::vector<StreamMetadata> stream_metadata;
    std::vector<SchemaMetadata> schema_metadata;

    schema_metadata.reserve(m_id_to_schema_writer.size());
    schemas.reserve(m_id_to_schema_writer.size());
    for (auto it = m_id_to_schema_writer.begin(); it != m_id_to_schema_writer.end(); ++it) {
        schemas.push_back(it);
    }
    auto comp = [](schema_map_it const& lhs, schema_map_it const& rhs) -> bool {
        return lhs->second->get_total_uncompressed_size()
               > rhs->second->get_total_uncompressed_size();
    };
    std::sort(schemas.begin(), schemas.end(), comp);

    uint64_t current_stream_offset = 0;
    uint64_t current_stream_id = 0;
    uint64_t current_table_file_offset = 0;
    m_tables_compressor.open(m_tables_file_writer, m_compression_level);
    for (auto it : schemas) {
        it->second->store(m_tables_compressor);
        schema_metadata.emplace_back(
                current_stream_id,
                current_stream_offset,
                it->first,
                it->second->get_num_messages()
        );
        current_stream_offset += it->second->get_total_uncompressed_size();
        delete it->second;

        if (current_stream_offset > m_min_table_size || schemas.size() == schema_metadata.size()) {
            stream_metadata.emplace_back(current_table_file_offset, current_stream_offset);
            m_tables_compressor.close();
            current_stream_offset = 0;
            ++current_stream_id;
            current_table_file_offset = m_tables_file_writer.get_pos();

            if (schemas.size() != schema_metadata.size()) {
                m_tables_compressor.open(m_tables_file_writer, m_compression_level);
            }
        }
    }

    m_table_metadata_compressor.write_numeric_value(stream_metadata.size());
    for (auto& stream : stream_metadata) {
        m_table_metadata_compressor.write_numeric_value(stream.file_offset);
        m_table_metadata_compressor.write_numeric_value(stream.uncompressed_size);
    }

    // The current implementation doesn't store large tables as separate columns, so this is always
    // zero.
    size_t const num_separate_column_schemas{0};
    m_table_metadata_compressor.write_numeric_value(num_separate_column_schemas);

    m_table_metadata_compressor.write_numeric_value(schema_metadata.size());
    for (auto& schema : schema_metadata) {
        m_table_metadata_compressor.write_numeric_value(schema.stream_id);
        m_table_metadata_compressor.write_numeric_value(schema.stream_offset);
        m_table_metadata_compressor.write_numeric_value(schema.schema_id);
        m_table_metadata_compressor.write_numeric_value(schema.num_messages);
    }
    m_table_metadata_compressor.close();

    auto table_metadata_compressed_size = m_table_metadata_file_writer.get_pos();
    auto table_compressed_size = m_tables_file_writer.get_pos();

    m_table_metadata_file_writer.close();
    m_tables_file_writer.close();

    return {table_metadata_compressed_size, table_compressed_size};
}

void ArchiveWriter::update_metadata_db() {
    m_metadata_db->open();
    clp::streaming_archive::ArchiveMetadata metadata(
            cArchiveFormatDevelopmentVersionFlag,
            "",
            0ULL
    );
    metadata.increment_static_compressed_size(m_compressed_size);
    metadata.increment_static_uncompressed_size(m_uncompressed_size);
    metadata.expand_time_range(
            m_timestamp_dict.get_begin_timestamp(),
            m_timestamp_dict.get_end_timestamp()
    );

    m_metadata_db->add_archive(m_id, metadata);
    m_metadata_db->close();
}

void ArchiveWriter::print_archive_stats() {
    nlohmann::json json_msg;
    json_msg["id"] = m_id;
    json_msg["uncompressed_size"] = m_uncompressed_size;
    json_msg["size"] = m_compressed_size;
    std::cout << json_msg.dump(-1, ' ', true, nlohmann::json::error_handler_t::ignore) << std::endl;
}
}  // namespace clp_s
