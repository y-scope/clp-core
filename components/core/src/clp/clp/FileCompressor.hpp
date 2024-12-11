#ifndef CLP_CLP_FILECOMPRESSOR_HPP
#define CLP_CLP_FILECOMPRESSOR_HPP

#include <system_error>

#include <boost/uuid/random_generator.hpp>
#include <log_surgeon/LogEvent.hpp>
#include <log_surgeon/ReaderParser.hpp>

#include "../BufferedFileReader.hpp"
#include "../ir/LogEventDeserializer.hpp"
#include "../LibarchiveFileReader.hpp"
#include "../LibarchiveReader.hpp"
#include "../MessageParser.hpp"
#include "../ParsedMessage.hpp"
#include "../streaming_archive/writer/Archive.hpp"
#include "FileToCompress.hpp"

namespace clp::clp {
/**
 * Class to parse and compress a file into a streaming archive
 */
class FileCompressor {
public:
    // Constructors
    FileCompressor(
            boost::uuids::random_generator& uuid_generator,
            std::unique_ptr<log_surgeon::ReaderParser> reader_parser
    )
            : m_uuid_generator(uuid_generator),
              m_reader_parser(std::move(reader_parser)) {}

    // Methods
    /**
     * Compresses a file with the given path into the archive
     * @param target_data_size_of_dicts
     * @param archive_user_config
     * @param target_encoded_file_size
     * @param file_to_compress
     * @param archive_writer
     * @return true if the file was compressed successfully, false otherwise
     */
    bool compress_file(
            size_t target_data_size_of_dicts,
            streaming_archive::writer::Archive::UserConfig& archive_user_config,
            size_t target_encoded_file_size,
            FileToCompress const& file_to_compress,
            streaming_archive::writer::Archive& archive_writer,
            bool use_heuristic
    );

private:
    // Constants
    static constexpr size_t cUtfMaxValidationLen = 4096;

    // Methods
    /**
     * Parses and encodes content from the given reader into the given archive_writer.
     * @param target_data_size_of_dicts
     * @param archive_user_config
     * @param target_encoded_file_size
     * @param path_for_compression
     * @param group_id
     * @param archive_writer
     * @param reader
     * @param use_heuristic
     */
    auto parse_and_encode(
            size_t target_data_size_of_dicts,
            streaming_archive::writer::Archive::UserConfig& archive_user_config,
            size_t target_encoded_file_size,
            std::string const& path_for_compression,
            group_id_t group_id,
            streaming_archive::writer::Archive& archive_writer,
            ReaderInterface& reader,
            bool use_heuristic
    ) -> void;

    void parse_and_encode_with_library(
            size_t target_data_size_of_dicts,
            streaming_archive::writer::Archive::UserConfig& archive_user_config,
            size_t target_encoded_file_size,
            std::string const& path_for_compression,
            group_id_t group_id,
            streaming_archive::writer::Archive& archive_writer,
            ReaderInterface& reader
    );

    void parse_and_encode_with_heuristic(
            size_t target_data_size_of_dicts,
            streaming_archive::writer::Archive::UserConfig& archive_user_config,
            size_t target_encoded_file_size,
            std::string const& path_for_compression,
            group_id_t group_id,
            streaming_archive::writer::Archive& archive_writer,
            ReaderInterface& reader
    );

    /**
     * Tries to compress the given file as if it were a generic archive_writer
     * @param target_data_size_of_dicts
     * @param archive_user_config
     * @param target_encoded_file_size
     * @param file_to_compress
     * @param archive_writer
     * @param file_reader
     * @param use_heuristic
     * @return true if all files were compressed successfully, false otherwise
     */
    bool try_compressing_as_archive(
            size_t target_data_size_of_dicts,
            streaming_archive::writer::Archive::UserConfig& archive_user_config,
            size_t target_encoded_file_size,
            FileToCompress const& file_to_compress,
            streaming_archive::writer::Archive& archive_writer,
            ReaderInterface& file_reader,
            bool use_heuristic
    );

    /**
     * Compresses the IR stream from the given reader into the archive
     * @param target_data_size_of_dicts
     * @param archive_user_config
     * @param target_encoded_file_size
     * @param path
     * @param group_id
     * @param archive_writer
     * @param reader
     * @return Whether the IR stream was compressed successfully
     */
    bool compress_ir_stream(
            size_t target_data_size_of_dicts,
            streaming_archive::writer::Archive::UserConfig& archive_user_config,
            size_t target_encoded_file_size,
            std::string const& path,
            group_id_t group_id,
            streaming_archive::writer::Archive& archive_writer,
            ReaderInterface& reader
    );

    /**
     * Compresses an IR stream using the eight-byte or four-byte encoding based on the given
     * template parameter.
     * @tparam encoded_variable_t
     * @param target_data_size_of_dicts
     * @param archive_user_config
     * @param target_encoded_file_size
     * @param path
     * @param group_id
     * @param archive
     * @param log_event_deserializer
     * @return An error code
     */
    template <typename encoded_variable_t>
    std::error_code compress_ir_stream_by_encoding(
            size_t target_data_size_of_dicts,
            streaming_archive::writer::Archive::UserConfig& archive_user_config,
            size_t target_encoded_file_size,
            std::string const& path,
            group_id_t group_id,
            streaming_archive::writer::Archive& archive,
            ir::LogEventDeserializer<encoded_variable_t>& log_event_deserializer
    );

    // Variables
    boost::uuids::random_generator& m_uuid_generator;
    LibarchiveReader m_libarchive_reader;
    LibarchiveFileReader m_libarchive_file_reader;
    MessageParser m_message_parser;
    ParsedMessage m_parsed_message;
    std::unique_ptr<log_surgeon::ReaderParser> m_reader_parser;
};
}  // namespace clp::clp

#endif  // CLP_CLP_FILECOMPRESSOR_HPP
