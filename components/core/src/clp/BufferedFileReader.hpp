#ifndef CLP_BUFFEREDFILEREADER_HPP
#define CLP_BUFFEREDFILEREADER_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "BufferReader.hpp"
#include "ErrorCode.hpp"
#include "ReaderInterface.hpp"
#include "TraceableException.hpp"

namespace clp {
/**
 * Class for performing buffered (in memory) reads from another ReaderInterface with control over
 * when and how much data is buffered. This allows us to support use cases where we want to perform
 * unordered reads from input which only support sequential access (e.g. files from block storage
 * like S3).
 *
 * To control how much data is buffered, we allow callers to set a checkpoint such that all reads
 * and seeks past the checkpoint will be buffered until the checkpoint is cleared. This allows
 * callers to perform random seeks and reads of any data after (and including) the checkpoint.
 * When no checkpoint is set, we maintain a fixed-size buffer.
 *
 * NOTE 1: Unless otherwise noted, the "file position" mentioned in docstrings is the position in
 * the buffered file, not the position in the original input file.
 *
 * NOTE 2: This class restricts the buffer size to a multiple of the page size and we avoid reading
 * anything less than a page to avoid multiple page faults.
 *
 * NOTE 3: Although the FILE stream interface provided by glibc also performs buffered reads, it
 * does not allow us to control the buffering.
 */
class BufferedFileReader : public ReaderInterface {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : OperationFailed(
                          error_code,
                          filename,
                          line_number,
                          "BufferedFileReader operation failed"
                  ) {}

        OperationFailed(
                ErrorCode error_code,
                char const* const filename,
                int line_number,
                std::string message
        )
                : TraceableException(error_code, filename, line_number),
                  m_message(std::move(message)) {}

        // Methods
        [[nodiscard]] auto what() const noexcept -> char const* override {
            return m_message.c_str();
        }

    private:
        std::string m_message;
    };

    // Constants
    static constexpr size_t cMinBufferSize = (1ULL << 12);

    // Constructors
    /**
     * @param reader_interface
     * @param base_buffer_size The size for the fixed-size buffer used when no checkpoint is set. It
     * must be a multiple of BufferedFileReader::cMinBufferSize.
     */
    explicit BufferedFileReader(
            std::unique_ptr<ReaderInterface> reader_interface,
            size_t base_buffer_size
    );

    explicit BufferedFileReader(std::unique_ptr<ReaderInterface> reader_interface)
            : BufferedFileReader{std::move(reader_interface), cDefaultBufferSize} {}

    // Destructor
    ~BufferedFileReader() override = default;

    // Disable copy/move construction/assignment
    BufferedFileReader(BufferedFileReader const&) = delete;
    BufferedFileReader(BufferedFileReader&&) = delete;
    auto operator=(BufferedFileReader) -> BufferedFileReader& = delete;
    auto operator=(BufferedFileReader&&) -> BufferedFileReader& = delete;

    // Methods
    /**
     * Tries to fill the internal buffer if it's empty
     * @return Same as refill_reader_buffer if it fails
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto try_refill_buffer_if_empty() -> ErrorCode;

    /**
     * Peeks the remaining buffered content without advancing the read head.
     *
     * NOTE: Any subsequent read or seek operations may invalidate the returned buffer.
     * @param buf Returns a pointer to the remaining content in the buffer
     * @param peek_size Returns the size of the remaining content in the buffer
     */
    void peek_buffered_data(char const*& buf, size_t& peek_size) const;

    /**
     * Sets a checkpoint at the current position in the file. If a checkpoint is already set, this
     * method will discard any buffered content from before the current checkpoint.
     *
     * NOTE: Setting a checkpoint may result in higher memory usage since the BufferedFileReader
     * needs to buffer all the data it reads after the checkpoint.
     * @return The current position in the file
     */
    auto set_checkpoint() -> size_t;

    /**
     * Clears the current checkpoint and moves the read head to the highest position that the caller
     * read/seeked to. This will shrink the buffer to its original size, discarding any excess data.
     */
    auto clear_checkpoint() -> void;

    // Methods implementing the ReaderInterface
    /**
     * @param pos Returns the position of the read head in the file
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto try_get_pos(size_t& pos) -> ErrorCode override;

    /**
     * Tries to seek to the given position relative to the beginning of the file. When no checkpoint
     * is set, callers can only seek forwards in the file; When a checkpoint is set, callers can
     * seek to any position in the file that's after and including the checkpoint.
     * @param pos
     * @return ErrorCode_Unsupported if a checkpoint is set and the requested position is less than
     * the checkpoint, or no checkpoint is set and the requested position is less the current read
     * head's position.
     * @return ErrorCode_Truncated if we reached the end of the file before we reached the given
     * position
     * @return Same as BufferReader::try_seek_from_begin if it fails
     * @return Same as ReaderInterface::try_seek_from_begin if it fails
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto try_seek_from_begin(size_t pos) -> ErrorCode override;

    /**
     * Tries to read up to a given number of bytes from the file
     * @param buf
     * @param num_bytes_to_read The number of bytes to try and read
     * @param num_bytes_read The actual number of bytes read
     * @return ErrorCode_BadParam if buf is null
     * @return ErrorCode_EndOfFile on EOF
     * @return Same as BufferReader::try_read if it fails
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto
    try_read(char* buf, size_t num_bytes_to_read, size_t& num_bytes_read) -> ErrorCode override;

    /**
     * Tries to read up to an occurrence of the given delimiter
     * @param delim
     * @param keep_delimiter Whether to include the delimiter in the output string
     * @param append Whether to append to the given string or replace its contents
     * @param str Returns the content read
     * @return ErrorCode_EndOfFile on EOF
     * @return Same as BufferReader::try_read_to_delimiter if it fails
     * @return Same as refill_reader_buffer if it fails
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto try_read_to_delimiter(
            char delim,
            bool keep_delimiter,
            bool append,
            std::string& str
    ) -> ErrorCode override;

private:
    // Methods
    /**
     * Refills the buffer with up to the given number of bytes from the underlying file.
     *
     * NOTE: Callers must ensure the current buffer has been exhausted before calling this method
     * (i.e., the read head is at the end of the buffer).
     * @param num_bytes_to_refill
     * @return Same as ReaderInterface::try_read
     */
    [[nodiscard]] auto refill_reader_buffer(size_t num_bytes_to_refill) -> ErrorCode;

    /**
     * Discards the data before the current position and resizes the buffer accordingly.
     */
    auto drop_content_before_current_pos() -> void;

    /**
     * @param file_pos
     * @return file_pos relative to the beginning of the buffer
     */
    [[nodiscard]] auto get_buffer_relative_pos(size_t file_pos) const -> size_t {
        return file_pos - m_buffer_begin_pos;
    }

    [[nodiscard]] auto get_buffer_end_pos() const -> size_t {
        return m_buffer_begin_pos + m_buffer_reader.get_buffer_size();
    }

    auto update_pos(size_t pos) -> void;

    // Constants
    static constexpr size_t cDefaultBufferSize = (16 * cMinBufferSize);

    // Variables
    size_t m_pos{0};
    std::unique_ptr<ReaderInterface> m_reader;

    // Buffer specific data
    std::vector<char> m_buffer;
    size_t m_base_buffer_size;
    BufferReader m_buffer_reader;
    size_t m_buffer_begin_pos{0};

    // Variables for checkpoint support
    std::optional<size_t> m_checkpoint_pos;
    size_t m_highest_read_pos{0};
};
}  // namespace clp

#endif  // CLP_BUFFEREDFILEREADER_HPP
