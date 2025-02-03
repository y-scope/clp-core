#ifndef CLP_STREAMING_COMPRESSION_ZSTD_COMPRESSOR_HPP
#define CLP_STREAMING_COMPRESSION_ZSTD_COMPRESSOR_HPP

#include <cstddef>
#include <ystdlib/container/Array.hpp>

#include <zstd.h>

#include "../../ErrorCode.hpp"
#include "../../TraceableException.hpp"
#include "../../WriterInterface.hpp"
#include "../Compressor.hpp"
#include "Constants.hpp"

namespace clp::streaming_compression::zstd {
class Compressor : public ::clp::streaming_compression::Compressor {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException{error_code, filename, line_number} {}

        // Methods
        [[nodiscard]] auto what() const noexcept -> char const* override {
            return "streaming_compression::zstd::Compressor "
                   "operation failed";
        }
    };

    // Constructor
    Compressor();

    // Destructor
    ~Compressor() override;

    // Delete copy constructor and assignment operator
    Compressor(Compressor const&) = delete;
    auto operator=(Compressor const&) -> Compressor& = delete;

    // Default move constructor and assignment operator
    Compressor(Compressor&&) noexcept = default;
    auto operator=(Compressor&&) noexcept -> Compressor& = default;

    // Methods implementing the WriterInterface
    /**
     * Writes the given data to the compressor
     * @param data
     * @param data_length
     */
    auto write(char const* data, size_t data_length) -> void override;

    /**
     * Writes any internally buffered data to file and ends the current frame
     */
    auto flush() -> void override;

    /**
     * Tries to get the current position of the write head
     * @param pos Position of the write head
     * @return ErrorCode_NotInit if the compressor is not open
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto try_get_pos(size_t& pos) const -> ErrorCode override;

    // Methods implementing the Compressor interface
    /**
     * Closes the compressor
     */
    auto close() -> void override;

    /**
     * Initializes the compression stream with the default compression level
     * @param writer
     */
    auto open(WriterInterface& writer) -> void override {
        this->open(writer, cDefaultCompressionLevel);
    }

    /**
     * Initializes the compression stream with the given compression level
     * @param writer
     * @param compression_level
     */
    auto open(WriterInterface& writer, int compression_level) -> void;

    /**
     * Flushes the stream without ending the current frame
     */
    auto flush_without_ending_frame() -> void;

private:
    // Variables
    WriterInterface* m_compressed_stream_writer{nullptr};

    // Compressed stream variables
    ZSTD_CStream* m_compression_stream{ZSTD_createCStream()};
    bool m_compression_stream_contains_data{false};

    ystdlib::container::Array<char> m_compressed_stream_block_buffer{ZSTD_CStreamOutSize()};
    ZSTD_outBuffer m_compressed_stream_block;

    size_t m_uncompressed_stream_pos{0};
};
}  // namespace clp::streaming_compression::zstd

#endif  // CLP_STREAMING_COMPRESSION_ZSTD_COMPRESSOR_HPP
