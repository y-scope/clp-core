#ifndef CLP_STRINGREADER_HPP
#define CLP_STRINGREADER_HPP

#include <cstdio>
#include <string>

#include "Defs.h"
#include "ErrorCode.hpp"
#include "ReaderInterface.hpp"
#include "TraceableException.hpp"

namespace clp {
class StringReader : public ReaderInterface {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        char const* what() const noexcept override { return "StringReader operation failed"; }
    };

    StringReader() = default;

    ~StringReader();

    // Methods implementing the ReaderInterface
    /**
     * Tries to get the current position of the read head in the file
     * @param pos Position of the read head in the file
     * @return ErrorCode_NotInit if the file is not open
     * @return ErrorCode_errno on error
     * @return ErrorCode_Success on success
     */
    ErrorCode try_get_pos(size_t& pos) override;
    /**
     * Tries to seek from the beginning of the file to the given position
     * @param pos
     * @return ErrorCode_NotInit if the file is not open
     * @return ErrorCode_errno on error
     * @return ErrorCode_Success on success
     */
    ErrorCode try_seek_from_begin(size_t pos) override;

    /**
     * Tries to read up to a given number of bytes from the file
     * @param buf
     * @param num_bytes_to_read The number of bytes to try and read
     * @param num_bytes_read The actual number of bytes read
     * @return ErrorCode_NotInit if the file is not open
     * @return ErrorCode_BadParam if buf is invalid
     * @return ErrorCode_errno on error
     * @return ErrorCode_EndOfFile on EOF
     * @return ErrorCode_Success on success
     */
    ErrorCode try_read(char* buf, size_t num_bytes_to_read, size_t& num_bytes_read) override;

    // Methods
    bool is_open() const { return m_string_is_set; }

    /**
     * Tries to open a file
     * @param path
     * @return ErrorCode_Success on success
     * @return ErrorCode_FileNotFound if the file was not found
     * @return ErrorCode_errno otherwise
     */
    ErrorCode try_open(std::string const& input_string);
    /**
     * Opens a file
     * @param path
     * @throw StringReader::OperationFailed on failure
     */
    void open(std::string const& input_string);
    /**
     * Closes the file if it's open
     */
    void close();

private:
    std::string m_input_string;
    uint32_t m_pos{0};
    bool m_string_is_set{false};
};
}  // namespace clp

#endif  // CLP_STRINGREADER_HPP
