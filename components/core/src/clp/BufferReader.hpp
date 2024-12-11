#ifndef CLP_BUFFERREADER_HPP
#define CLP_BUFFERREADER_HPP

#include "ReaderInterface.hpp"

namespace clp {
/**
 * Class for reading from a fixed-size in-memory buffer
 */
class BufferReader : public ReaderInterface {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        [[nodiscard]] auto what() const noexcept -> char const* override {
            return "BufferReader operation failed";
        }
    };

    // Constructors
    BufferReader() : BufferReader(nullptr, 0, 0) {}

    BufferReader(char const* data, size_t data_size) : BufferReader(data, data_size, 0) {}

    BufferReader(char const* data, size_t data_size, size_t pos);

    // Methods
    [[nodiscard]] auto get_buffer_size() const -> size_t { return m_internal_buf_size; }

    /**
     * @param buf Returns a pointer to the remaining content in the buffer
     * @param peek_size Returns the size of the remaining content in the buffer
     */
    auto peek_buffer(char const*& buf, size_t& peek_size) const -> void;

    /**
     * Tries to read up to an occurrence of the given delimiter
     * @param delim
     * @param keep_delimiter Whether to include the delimiter in the output string
     * @param str Returns the content read from the buffer
     * @param found_delim Whether a delimiter was found
     * @param num_bytes_read How many bytes were read from the buffer
     * @return ErrorCode_EndOfFile if the buffer doesn't contain any more data
     * @return ErrorCode_Success on success
     */
    auto try_read_to_delimiter(
            char delim,
            bool keep_delimiter,
            std::string& str,
            bool& found_delim,
            size_t& num_bytes_read
    ) -> ErrorCode;

    // Methods implementing the ReaderInterface
    /**
     * Tries to read up to a given number of bytes from the buffer
     * @param buf
     * @param num_bytes_to_read
     * @param num_bytes_read Returns the number of bytes read
     * @return ErrorCode_EndOfFile if the buffer doesn't contain any more data
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto
    try_read(char* buf, size_t num_bytes_to_read, size_t& num_bytes_read) -> ErrorCode override;

    /**
     * Tries to seek to the given position, relative to the beginning of the buffer
     * @param pos
     * @return ErrorCode_Truncated if \p pos > the buffer's size
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] auto try_seek_from_begin(size_t pos) -> ErrorCode override;

    /**
     * @param pos Returns the position of the read head in the buffer
     * @return ErrorCode_Success
     */
    [[nodiscard]] auto try_get_pos(size_t& pos) -> ErrorCode override;

    /**
     * Tries to read up to an occurrence of the given delimiter
     * @param delim
     * @param keep_delimiter Whether to include the delimiter in the output string
     * @param append Whether to append to the given string or replace its contents
     * @param str Returns the content read from the buffer
     * @return Same as BufferReader::try_read_to_delimiter(char, bool, std::string&, bool&, size_t&)
     */
    [[nodiscard]] auto try_read_to_delimiter(
            char delim,
            bool keep_delimiter,
            bool append,
            std::string& str
    ) -> ErrorCode override;

private:
    // Methods
    [[nodiscard]] auto get_remaining_data_size() const -> size_t {
        return m_internal_buf_size - m_internal_buf_pos;
    }

    // Variables
    char const* m_internal_buf;
    size_t m_internal_buf_size;
    size_t m_internal_buf_pos;
};
}  // namespace clp

#endif  // CLP_BUFFERREADER_HPP
