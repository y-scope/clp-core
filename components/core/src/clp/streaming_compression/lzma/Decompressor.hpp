#ifndef CLP_STREAMING_COMPRESSION_LZMA_DECOMPRESSOR_HPP
#define CLP_STREAMING_COMPRESSION_LZMA_DECOMPRESSOR_HPP

#include <cstddef>

#include "../../ErrorCode.hpp"
#include "../../ReaderInterface.hpp"
#include "../../TraceableException.hpp"
#include "../Constants.hpp"
#include "../Decompressor.hpp"

namespace clp::streaming_compression::lzma {
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
            return "streaming_compression::lzma::Decompressor operation failed";
        }
    };

    // Constructor
    Decompressor() : clp::streaming_compression::Decompressor{CompressorType::LZMA} {}

    // Destructor
    ~Decompressor() override = default;

    // Delete copy constructor and assignment operator
    Decompressor(Decompressor const&) = delete;
    auto operator=(Decompressor const&) -> Decompressor& = delete;

    // Delete move constructor and assignment operator
    // TODO: Change to default when the base decompressor class has been updated.
    Decompressor(Decompressor&&) noexcept = delete;
    auto operator=(Decompressor&&) noexcept -> Decompressor& = delete;

    // Methods implementing the ReaderInterface
    /**
     * Tries to read up to a given number of bytes from the decompressor.
     * @param buf
     * @param num_bytes_to_read The number of bytes to try reading.
     * @param num_bytes_read Returns the actual number of bytes read.
     * @return ErrorCode_Unsupported
     */
    [[nodiscard]] auto try_read(char* buf, size_t num_bytes_to_read, size_t& num_bytes_read)
            -> ErrorCode override;

    /**
     * Tries to seek from the beginning to the given position.
     * @param pos
     * @return ErrorCode_Unsupported
     */
    [[nodiscard]] auto try_seek_from_begin(size_t pos) -> ErrorCode override;

    /**
     * Tries to get the current position of the read head.
     * @param pos Returns the position of the read head in the file.
     * @return ErrorCode_Unsupported
     */
    [[nodiscard]] auto try_get_pos(size_t& pos) -> ErrorCode override;

    // Methods implementing the Decompressor interface
    /***
     * Initialize streaming decompressor to decompress from the specified compressed data buffer.
     * @param compressed_data_buffer
     * @param compressed_data_buffer_size
     * @throw clp::streaming_compression::lzma::Decompressor::OperationFailed if unsupported.
     */
    auto open(char const* compressed_data_buffer, size_t compressed_data_buffer_size)
            -> void override;

    /**
     * Initializes the decompressor to decompress from a reader interface.
     * @param reader
     * @param read_buffer_capacity The maximum amount of data to read from a reader at a time.
     * @throw clp::streaming_compression::lzma::Decompressor::OperationFailed if unsupported.
     */
    auto open(ReaderInterface& reader, size_t read_buffer_capacity) -> void override;

    /**
     * @throw clp::streaming_compression::lzma::Decompressor::OperationFailed if unsupported.
     */
    auto close() -> void override;

    /**
     * Decompresses and copies the range of uncompressed data described by `decompressed_stream_pos`
     * and `extraction_len` into `extraction_buf`.
     * @param decompressed_stream_pos
     * @param extraction_buf
     * @param extraction_len
     * @return ErrorCode_Unsupported
     */
    [[nodiscard]] auto get_decompressed_stream_region(
            size_t decompressed_stream_pos,
            char* extraction_buf,
            size_t extraction_len
    ) -> ErrorCode override;
};
}  // namespace clp::streaming_compression::lzma
#endif  // CLP_STREAMING_COMPRESSION_LZMA_DECOMPRESSOR_HPP
