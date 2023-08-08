#ifndef LIBARCHIVEFILEREADER_HPP
#define LIBARCHIVEFILEREADER_HPP

// C++ standard libraries
#include <string>
#include <vector>

// libarchive
#include <archive.h>

// Project headers
#include "ErrorCode.hpp"
#include "ReaderInterface.hpp"
#include "TraceableException.hpp"

/**
 * Class for reading a file from an archive through libarchive
 */
class LibarchiveFileReader : public ReaderInterface {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed (ErrorCode error_code, const char* const filename, int line_number) : TraceableException (error_code, filename, line_number) {}

        // Methods
        const char* what () const noexcept override {
            return "LibarchiveFileReader operation failed";
        }
    };

    // Constructors
    LibarchiveFileReader () : m_archive(nullptr), m_archive_entry(nullptr), m_data_block(nullptr),
                              m_reached_eof(false), m_pos_in_file(0) {}

    // Methods implementing the ReaderInterface
    /**
     * Tries to get the current position of the read head in the file
     * @param pos Position of the read head in the file
     * @return ErrorCode_Success
     */
    ErrorCode try_get_pos (size_t &pos) override;
    /**
     * Unsupported method
     * @param pos
     * @return N/A
     */
    ErrorCode try_seek_from_begin (size_t pos) override;
    /**
     * Tries to read up to a given number of bytes from the file
     * @param buf
     * @param num_bytes_to_read The number of bytes to try and read
     * @param num_bytes_read The actual number of bytes read
     * @return ErrorCode_EndOfFile on EOF
     * @return ErrorCode_Failure on failure
     * @return ErrorCode_Success on success
     */
    ErrorCode try_read (char *buf, size_t num_bytes_to_read, size_t &num_bytes_read) override;

    // Methods overriding the ReaderInterface
    /**
     * Tries to read a string from the file until it reaches the specified delimiter
     * @param delim The delimiter to stop at
     * @param keep_delimiter Whether to include the delimiter in the output string or not
     * @param append Whether to append to the given string or replace its contents
     * @param str The string read
     * @return ErrorCode_EndOfFile on EOF
     * @return ErrorCode_Failure on failure
     * @return ErrorCode_Success on success
     */
    ErrorCode try_read_to_delimiter (char delim, bool keep_delimiter, bool append, std::string& str) override;

    /**
     * Tries to peek from the next data block and returns the available
     * data size
     * @param buf
     * @param buf_size Returns the number of bytes in the buffer
     * @return ErrorCode_EndOfFile on EOF
     * @return ErrorCode_Failure on failure
     * @return ErrorCode_Success on success
     */
    [[nodiscard]] ErrorCode try_peek_data_block(const char*&buf, size_t&buf_size);

    // Methods
    /**
     * Opens the file reader
     * @param archive
     * @param archive_entry
     */
    void open (struct archive* archive, struct archive_entry* archive_entry);
    /**
     * Closes the file reader
     */
    void close ();

private:
    // Methods
    /**
     * Reads next data block from the archive
     * @return ErrorCode_EndOfFile on EOF
     * @return ErrorCode_Failure on failure
     * @return ErrorCode_Success on success
     */
    ErrorCode read_next_data_block ();

    // Variables
    struct archive* m_archive;

    struct archive_entry* m_archive_entry;
    la_int64_t m_data_block_pos_in_file;
    const void* m_data_block;
    size_t m_data_block_length;
    la_int64_t m_pos_in_data_block;
    bool m_reached_eof;

    size_t m_pos_in_file;

    // vector to hold peeked data
    std::vector<char> m_data_for_peek;
};

#endif // LIBARCHIVEFILEREADER_HPP
