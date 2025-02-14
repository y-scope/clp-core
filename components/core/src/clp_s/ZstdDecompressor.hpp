// Code from CLP

#ifndef CLP_S_ZSTDDECOMPRESSOR_HPP
#define CLP_S_ZSTDDECOMPRESSOR_HPP

#include <memory>
#include <string>

#include <boost/iostreams/device/mapped_file.hpp>
#include <zstd.h>

#include "../clp/FileReader.hpp"
#include "../clp/ReaderInterface.hpp"
#include "Decompressor.hpp"
#include "TraceableException.hpp"

namespace clp_s {
class ZstdDecompressor : public Decompressor {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}
    };

    // Constructor
    /**
     * @throw Decompressor::OperationFailed if zstd decompressor stream cannot be initialized
     */
    ZstdDecompressor();

    // Destructor
    ~ZstdDecompressor();

    // Explicitly disable copy and move constructor/assignment
    ZstdDecompressor(ZstdDecompressor const&) = delete;

    ZstdDecompressor& operator=(ZstdDecompressor const&) = delete;

    // Methods implementing the Decompressor interface
    void open(char const* compressed_data_buf, size_t compressed_data_buf_size) override;

    void open(clp::FileReader& file_reader, size_t file_read_buffer_capacity) override;

    void open(clp::ReaderInterface& reader, size_t file_read_buffer_capacity) override;

    void close() override;

    /**
     * Closes this ZstdDecompressor with a hint that the ZstdDecompressor will be reused. This
     * makes the ZstdDecompressor hold onto its internal buffer, avoiding reallocations.
     */
    void close_for_reuse();

    // Methods
    /***
     * Initialize streaming decompressor to decompress from a compressed file specified by the given
     * path
     * @param compressed_file_path
     * @param decompressed_stream_block_size
     * @return ErrorCodeFailure if the provided path cannot be memory mapped
     * @return ErrorCodeSuccess on success
     */
    ErrorCode open(std::string const& compressed_file_path);

    // Methods implementing the ReaderInterface
    /**
     * Tries to read up to a given number of bytes from the decompressor
     * @param buf
     * @param num_bytes_to_read The number of bytes to try and read
     * @param num_bytes_read The actual number of bytes read
     * @return Same as FileReader::try_read if the decompressor is attached to a file
     * @return ErrorCodeNotInit if the decompressor is not open
     * @return ErrorCodeBadParam if buf is invalid
     * @return ErrorCodeEndOfFile on EOF
     * @return ErrorCodeFailure on decompression failure
     * @return ErrorCodeSuccess on success
     */
    ErrorCode try_read(char const* buf, size_t num_bytes_to_read, size_t& num_bytes_read);

    /**
     * Tries to read a number of bytes
     * @param buf
     * @param num_bytes Number of bytes to read
     * @return Same as the underlying medium's try_read method
     * @return ErrorCodeTruncated if 0 < # bytes read < num_bytes
     */
    ErrorCode try_read_exact_length(char* buf, size_t num_bytes);

    /**
     * Tries to read a numeric value
     * @tparam ValueType
     * @param value
     * @return Same as the underlying medium's try_read_exact_length method
     */
    template <typename ValueType>
    ErrorCode try_read_numeric_value(ValueType& value);

    /**
     * Tries to read a string
     * @param str_length length of the string to read
     * @param str
     * @return Same as the underlying medium's try_read_exact_length method
     */
    ErrorCode try_read_string(size_t str_length, std::string& str);

private:
    // Enum class
    enum class InputType {
        NotInitialized,  // Note: do nothing but generate an error to prevent this required
                         // parameter is not initialized properly
        CompressedDataBuf,
        MemoryMappedCompressedFile,
        File,
        ClpReader
    };

    // Methods
    /**
     * Reset streaming decompression state so it will start decompressing from the beginning of the
     * stream afterwards
     */
    void reset_stream();

    // Variables
    InputType m_input_type;

    // Compressed stream variables
    ZSTD_DStream* m_decompression_stream;

    boost::iostreams::mapped_file_source m_memory_mapped_compressed_file;
    clp::FileReader* m_file_reader;
    clp::ReaderInterface* m_reader;
    size_t m_file_reader_initial_pos;
    std::unique_ptr<char[]> m_file_read_buffer;
    size_t m_file_read_buffer_length;
    size_t m_file_read_buffer_capacity;

    ZSTD_inBuffer m_compressed_stream_block{};

    size_t m_decompressed_stream_pos;
    size_t m_unused_decompressed_stream_block_size;
    std::unique_ptr<char[]> m_unused_decompressed_stream_block_buffer;
};

template <typename ValueType>
ErrorCode ZstdDecompressor::try_read_numeric_value(ValueType& value) {
    return try_read_exact_length(reinterpret_cast<char*>(&value), sizeof(value));
}
}  // namespace clp_s

#endif  // CLP_S_ZSTDDECOMPRESSOR_HPP
