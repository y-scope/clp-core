#ifndef CLP_STREAMING_COMPRESSION_LZMA_COMPRESSOR_HPP
#define CLP_STREAMING_COMPRESSION_LZMA_COMPRESSOR_HPP

#include <cstddef>
#include <cstdint>
#include <ystdlib/container/Array.hpp>

#include <lzma.h>

#include "../../ErrorCode.hpp"
#include "../../TraceableException.hpp"
#include "../../WriterInterface.hpp"
#include "../Compressor.hpp"
#include "Constants.hpp"

namespace clp::streaming_compression::lzma {
/**
 * Implements a LZMA compressor that compresses byte input data to a `clp::WriterInterface`
 * instance.
 */
class Compressor : public ::clp::streaming_compression::Compressor {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        [[nodiscard]] auto what() const noexcept -> char const* override {
            return "streaming_compression::lzma::Compressor operation failed";
        }
    };

    // Constructors
    Compressor() : Compressor{cDefaultCompressionLevel, cDefaultDictionarySize, LZMA_CHECK_CRC64} {}

    Compressor(int compression_level, size_t dict_size, lzma_check check)
            : m_lzma_stream{compression_level, dict_size, check} {}

    // Destructor
    ~Compressor() override = default;

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
     * Writes any internally buffered data to the underlying `clp::WriterInterface` instance and
     * ends the current frame.
     *
     * Forces all the encoded data buffered by LZMA to be available at output
     */
    auto flush() -> void override;

    /**
     * Tries to get the current position of the write head
     * @param pos Position of the write head
     * @return ErrorCode_NotInit if the compressor is not open
     * @return ErrorCode_Success on success
     */
    auto try_get_pos(size_t& pos) const -> ErrorCode override;

    // Methods implementing the Compressor interface
    /**
     * Closes the compressor
     */
    auto close() -> void override;

    /**
     * Opens the compression stream for encoding to the writer.
     *
     * @param writer
     */
    auto open(WriterInterface& writer) -> void override;

private:
    /**
     * Wrapper class around lzma_stream providing easier usage.
     */
    class LzmaStream {
    public:
        /**
         * Initializes an LZMA compression encoder and its streams.
         *
         * @param compression_level Compression preset level in the range [0-9] where the higher
         * numbers use increasingly more memory for greater compression ratios.
         * @param dict_size Max amount of recently processed uncompressed bytes to keep in the
         * memory.
         * @param check Type of check to verify the integrity of the uncompressed data.
         * LZMA_CHECK_CRC64 is the default in the xz command line tool. If the .xz file needs to be
         * decompressed with XZ-Embedded, use LZMA_CHECK_CRC32 instead.
         *
         * @throw `OperationFailed` `ErrorCode_BadParam` if the LZMA options are invalid or the
         * encoder fails to initialize.
         */
        LzmaStream(int compression_level, size_t dict_size, lzma_check check);

        // Destructor
        ~LzmaStream() = default;

        // Delete copy constructor and assignment operator
        LzmaStream(LzmaStream const&) = delete;
        auto operator=(LzmaStream const&) -> LzmaStream& = delete;

        // Default move constructor and assignment operator
        LzmaStream(LzmaStream&&) noexcept = default;
        auto operator=(LzmaStream&&) noexcept -> LzmaStream& = default;

        /**
         * Attaches a pre-allocated block buffer to the encoder's input stream.
         *
         * @return false if the data buffer is null.
         * @return true on success.
         */
        [[nodiscard]] auto attach_input(uint8_t const* data_ptr, size_t data_length) -> bool {
            if (nullptr == data_ptr) {
                return false;
            }
            m_stream.next_in = data_ptr;
            m_stream.avail_in = data_length;
            return true;
        }

        /**
         * Attaches a pre-allocated block buffer to the encoder's output stream.
         *
         * @return false if the data buffer is null or empty.
         * @return true on success.
         */
        [[nodiscard]] auto attach_output(uint8_t* data_ptr, size_t data_length) -> bool {
            if (nullptr == data_ptr || 0 == data_length) {
                return false;
            }
            m_stream.next_out = data_ptr;
            m_stream.avail_out = data_length;
            return true;
        }

        [[nodiscard]] auto avail_in() const -> size_t { return m_stream.avail_in; }

        [[nodiscard]] auto avail_out() const -> size_t { return m_stream.avail_out; }

        /**
         * Unset the internal fields of the encoder's input stream.
         */
        auto detach_input() -> void {
            m_stream.next_in = nullptr;
            m_stream.avail_in = 0;
        }

        /**
         * End the LZMA stream and unset the internal fields of the encoder's output stream.
         */
        auto end_and_detach_output() -> void {
            lzma_end(&m_stream);
            m_stream.next_out = nullptr;
            m_stream.avail_out = 0;
        }

        [[nodiscard]] static auto is_flush_action(lzma_action action) -> bool {
            return LZMA_SYNC_FLUSH == action || LZMA_FULL_FLUSH == action
                   || LZMA_FULL_BARRIER == action || LZMA_FINISH == action;
        }

        [[nodiscard]] auto lzma_code(lzma_action action) -> lzma_ret {
            return ::lzma_code(&m_stream, action);
        }

    private:
        lzma_stream m_stream = LZMA_STREAM_INIT;
    };

    static constexpr size_t cCompressedStreamBlockBufferSize{4096};  // 4KiB

    /**
     * Invokes lzma_code() repeatedly with LZMA_RUN until the input is exhausted
     *
     * At the end of the workflow, the last bytes of encoded data may still be buffered in the LZMA
     * stream and thus not immediately available at the output block buffer.
     *
     * Assumes input stream and output block buffer are both in valid states.
     * @throw `OperationFailed` if LZMA returns an unexpected error value
     */
    auto encode_lzma() -> void;

    /**
     * Invokes lzma_code() repeatedly with the given flushing action until all encoded data is made
     * available at the output block buffer
     *
     * Once flushing starts, the workflow action needs to stay the same until flushing is signaled
     * complete by LZMA (aka LZMA_STREAM_END is reached).
     * See also: https://github.com/tukaani-project/xz/blob/master/src/liblzma/api/lzma/base.h#L274
     *
     * Assumes input stream and output block buffer are both in valid states.
     * @param flush_action
     * @throw `OperationFailed` if the provided action is not an LZMA flush
     *        action, or if LZMA returns an unexpected error value
     */
    auto flush_lzma(lzma_action flush_action) -> void;

    /**
     * Flushes the current compressed data in the output block buffer to the underlying
     * `clp::WriterInterface` instance.
     *
     * Also resets the output block buffer to receive new data.
     */
    auto flush_stream_output_block_buffer() -> void;

    // Variables
    WriterInterface* m_compressed_stream_writer{nullptr};

    // Compressed stream variables
    ystdlib::container::Array<uint8_t> m_compressed_stream_block_buffer{cCompressedStreamBlockBufferSize};
    LzmaStream m_lzma_stream;
    size_t m_uncompressed_stream_pos{0};
};
}  // namespace clp::streaming_compression::lzma

#endif  // CLP_STREAMING_COMPRESSION_LZMA_COMPRESSOR_HPP
