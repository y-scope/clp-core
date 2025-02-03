#ifndef CLP_STREAMING_COMPRESSION_ZSTD_DECOMPRESSOR_HPP
#define CLP_STREAMING_COMPRESSION_ZSTD_DECOMPRESSOR_HPP

#include <memory>
#include <optional>
#include <string>
#include <ystdlib/container/Array.hpp>

#include <zstd.h>

#include "../../ReaderInterface.hpp"
#include "../../ReadOnlyMemoryMappedFile.hpp"
#include "../../TraceableException.hpp"
#include "../Decompressor.hpp"

namespace clp::streaming_compression::zstd {
class Decompressor : public ::clp::streaming_compression::Decompressor {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        [[nodiscard]] auto what() const noexcept -> char const* override {
            return "streaming_compression::zstd::Decompressor operation failed";
        }
    };

    // Constructor
    /**
     * @throw Decompressor::OperationFailed if zstd decompressor stream
     * cannot be initialized
     */
    Decompressor();

    // Destructor
    ~Decompressor() override;

    // Delete copy constructor and assignment operator
    Decompressor(Decompressor const&) = delete;
    auto operator=(Decompressor const&) -> Decompressor& = delete;

    // Default move constructor and assignment operator
    Decompressor(Decompressor&&) noexcept = default;
    auto operator=(Decompressor&&) noexcept -> Decompressor& = default;

    // Methods implementing the ReaderInterface
    /**
     * Tries to read up to a given number of bytes from the decompressor
     * @param buf
     * @param num_bytes_to_read The number of bytes to try and read
     * @param num_bytes_read The actual number of bytes read
     * @return Same as FileReader::try_read if the decompressor is attached to a file
     * @return ErrorCode_NotInit if the decompressor is not open
     * @return ErrorCode_BadParam if buf is invalid
     * @return ErrorCode_EndOfFile on EOF
     * @return ErrorCode_Failure on decompression failure
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto try_read(char* buf, size_t num_bytes_to_read, size_t& num_bytes_read)
            -> ErrorCode override;
    /**
     * Tries to seek from the beginning to the given position
     * @param pos
     * @return ErrorCode_NotInit if the decompressor is not open
     * @return Same as ReaderInterface::try_read_exact_length
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto try_seek_from_begin(size_t pos) -> ErrorCode override;
    /**
     * Tries to get the current position of the read head
     * @param pos Position of the read head in the file
     * @return ErrorCode_NotInit if the decompressor is not open
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto try_get_pos(size_t& pos) -> ErrorCode override;

    // Methods implementing the Decompressor interface
    auto open(char const* compressed_data_buf, size_t compressed_data_buf_size) -> void override;
    auto open(ReaderInterface& reader, size_t read_buffer_capacity) -> void override;
    auto close() -> void override;
    /**
     * Decompresses and copies the range of uncompressed data described by
     * decompressed_stream_pos and extraction_len into extraction_buf
     * @param decompressed_stream_pos
     * @param extraction_buf
     * @param extraction_len
     * @return Same as streaming_compression::zstd::Decompressor::try_seek_from_begin
     * @return Same as ReaderInterface::try_read_exact_length
     */
    [[nodiscard]] auto get_decompressed_stream_region(
            size_t decompressed_stream_pos,
            char* extraction_buf,
            size_t extraction_len
    ) -> ErrorCode override;

    // Methods
    /***
     * Initialize streaming decompressor to decompress from a compressed file specified by the
     * given path
     * @param compressed_file_path
     * @param decompressed_stream_block_size
     * @return ErrorCode_Failure if the provided path cannot be memory mapped
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto open(std::string const& compressed_file_path) -> ErrorCode;

private:
    // Enum class
    enum class InputType {
        // Note: do nothing but generate an error to prevent this required
        // parameter is not initialized properly
        NotInitialized,
        CompressedDataBuf,
        MemoryMappedCompressedFile,
        ReaderInterface
    };

    // Methods
    /**
     * Reset streaming decompression state so it will start decompressing from the beginning of
     * the stream afterwards
     */
    void reset_stream();

    // Variables
    InputType m_input_type{InputType::NotInitialized};

    // Compressed stream variables
    ZSTD_DStream* m_decompression_stream{nullptr};

    std::unique_ptr<ReadOnlyMemoryMappedFile> m_memory_mapped_file;
    ReaderInterface* m_reader{nullptr};
    size_t m_reader_initial_pos{0ULL};

    std::optional<ystdlib::container::Array<char>> m_read_buffer;
    size_t m_read_buffer_length{0ULL};

    ZSTD_inBuffer m_compressed_stream_block{};

    size_t m_decompressed_stream_pos{0ULL};

    ystdlib::container::Array<char> m_unused_decompressed_stream_block_buffer;
};
}  // namespace clp::streaming_compression::zstd
#endif  // CLP_STREAMING_COMPRESSION_ZSTD_DECOMPRESSOR_HPP
