#include "LibarchiveFileReader.hpp"

// C++ standard libraries
#include <cstring>

// spdlog
#include <spdlog/spdlog.h>

ErrorCode LibarchiveFileReader::try_get_pos (size_t& pos) {
    if (nullptr == m_archive) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }
    pos = m_pos_in_file;
    return ErrorCode_Success;
}

ErrorCode LibarchiveFileReader::try_seek_from_begin (size_t pos) {
    if (nullptr == m_archive) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }

    throw OperationFailed(ErrorCode_Unsupported, __FILENAME__, __LINE__);
}

ErrorCode LibarchiveFileReader::try_read (char* buf, size_t num_bytes_to_read, size_t& num_bytes_read) {
    if (nullptr == m_archive) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }
    if (nullptr == m_archive_entry) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }

    if (m_reached_eof) {
        return ErrorCode_EndOfFile;
    }

    num_bytes_read = 0;
    while (true) {
        // Read a data block if necessary
        if (nullptr == m_data_block) {
            auto error_code = read_next_data_block();
            if (ErrorCode_Success != error_code) {
                if (ErrorCode_EndOfFile == error_code && num_bytes_read > 0) {
                    return ErrorCode_Success;
                }
                return error_code;
            }
        }

        // Simulate reading '\0' before the start of the data block
        if (m_pos_in_file < m_data_block_pos_in_file) {
            size_t num_zeros_to_append = std::min((size_t)(m_data_block_pos_in_file - m_pos_in_file), num_bytes_to_read - num_bytes_read);
            memset(&buf[num_bytes_read], '\0', num_zeros_to_append);
            num_bytes_read += num_zeros_to_append;
            m_pos_in_file += num_zeros_to_append;

            if (num_bytes_read == num_bytes_to_read) {
                return ErrorCode_Success;
            }
        }

        // Read from data block
        if (m_pos_in_data_block < m_data_block_length) {
            const char* data = reinterpret_cast<const char*>(m_data_block) + m_pos_in_data_block;
            size_t data_length = m_data_block_length - m_pos_in_data_block;

            size_t num_bytes_to_append = std::min(data_length, num_bytes_to_read - num_bytes_read);
            memcpy(&buf[num_bytes_read], data, num_bytes_to_append);
            num_bytes_read += num_bytes_to_append;
            m_pos_in_data_block += num_bytes_to_append;
            m_pos_in_file += num_bytes_to_append;

            if (m_pos_in_data_block == m_data_block_length) {
                // Finished reading data block
                m_data_block = nullptr;
            }

            if (num_bytes_read == num_bytes_to_read) {
                return ErrorCode_Success;
            }
        }
    }
}

ErrorCode LibarchiveFileReader::try_read_to_delimiter (char delim, bool keep_delimiter, bool append, std::string& str) {
    if (nullptr == m_archive) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }
    if (nullptr == m_archive_entry) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }

    if (m_reached_eof) {
        return ErrorCode_EndOfFile;
    }

    if (false == append) {
        str.clear();
    }

    size_t original_str_length = str.length();

    while (true) {
        // Read a data block if necessary
        if (nullptr == m_data_block) {
            auto error_code = read_next_data_block();
            if (ErrorCode_Success != error_code) {
                if (ErrorCode_EndOfFile == error_code && str.length() > original_str_length) {
                    // NOTE: At this point, we haven't found delim, so return directly without breaking to add delim
                    return ErrorCode_Success;
                }
                return error_code;
            }
        }

        // Simulate reading '\0' before the start of the data block
        if (m_pos_in_file < m_data_block_pos_in_file) {
            if ('\0' != delim) {
                // Fill with zeros
                size_t num_zeros_to_append = m_data_block_pos_in_file - m_pos_in_file;
                str.append(num_zeros_to_append, '\0');
                m_pos_in_file += num_zeros_to_append;
            } else {
                ++m_pos_in_file;
                // Found delimiter, so break
                break;
            }
        }

        // Read from data block
        if (m_pos_in_data_block < m_data_block_length) {
            const char* data = reinterpret_cast<const char*>(m_data_block) + m_pos_in_data_block;
            size_t data_length = m_data_block_length - m_pos_in_data_block;

            const char* delim_ptr = reinterpret_cast<const char*>(memchr(data, delim, data_length));
            if (nullptr == delim_ptr) {
                // Add the remaining data to the string
                str.append(data, data_length);
                m_pos_in_data_block += data_length;
                m_pos_in_file += data_length;

                m_data_block = nullptr;
            } else {
                data_length = delim_ptr - data;
                str.append(data, data_length);

                // Add 1 for the delimiter
                ++data_length;

                m_pos_in_data_block += data_length;
                m_pos_in_file += data_length;

                if (m_pos_in_data_block == m_data_block_length) {
                    // Finished reading data block
                    m_data_block = nullptr;
                }

                // Found delimiter, so break
                break;
            }
        }
    }

    if (keep_delimiter) {
        str += delim;
    }
    return ErrorCode_Success;
}

ErrorCode LibarchiveFileReader::peek_data_block (size_t size_to_peek, const char*& data_ptr,
                                                 size_t& peek_size) {
    if (nullptr == m_archive) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }
    if (nullptr == m_archive_entry) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }

    // Read a data block if necessary
    if (nullptr == m_data_block) {
        auto error_code = read_next_data_block();
        if (ErrorCode_Success != error_code) {
            data_ptr = nullptr;
            peek_size = 0;
            return error_code;
        }
    }

    // If we don't need to simulate reading '\0' before the start of the data block
    // simply return a const& to the current data block
    if (m_data_block_pos_in_file <= m_pos_in_file) {
        peek_size = std::min(size_to_peek, m_data_block_length - m_pos_in_data_block);
        data_ptr = reinterpret_cast<const char*>(m_data_block);
        return ErrorCode_Success;
    }

    // If there are sparse bytes before the data block, the pos in data block
    // must be 0
    assert(m_pos_in_data_block != 0);

    auto num_sparse_bytes = m_data_block_pos_in_file - m_pos_in_file;
    peek_size = std::min(num_sparse_bytes + m_data_block_length, size_to_peek);

    // resize the local buffer is necessary
    if (m_data_for_peek.size() < peek_size) {
        m_data_for_peek.resize(peek_size);
    }
    data_ptr = reinterpret_cast<const char*>(m_data_for_peek.data());

    if (size_to_peek < num_sparse_bytes) {
        memset(m_data_for_peek.data(), '\0', size_to_peek);
        return ErrorCode_Success;
    }

    // if size to peek is greater than number of sparse bytes,
    // copy over the data from data_block to the peek data buffer
    memset(m_data_for_peek.data(), '\0', num_sparse_bytes);
    size_t remaining_bytes_to_peek = peek_size - num_sparse_bytes;
    const char* data = reinterpret_cast<const char*>(m_data_block);
    memcpy(&m_data_for_peek[num_sparse_bytes], data, remaining_bytes_to_peek);

    return ErrorCode_Success;
}

void LibarchiveFileReader::open (struct archive* archive, struct archive_entry* archive_entry) {
    if (nullptr == archive) {
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__);
    }
    if (nullptr == archive_entry) {
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__);
    }
    if (nullptr != m_archive) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }

    m_archive = archive;
    m_archive_entry = archive_entry;
}

void LibarchiveFileReader::close () {
    if (nullptr == m_archive) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }

    m_archive = nullptr;
    m_archive_entry = nullptr;

    m_data_block = nullptr;
    m_reached_eof = false;

    m_pos_in_file = 0;
}

ErrorCode LibarchiveFileReader::read_next_data_block () {
    auto return_value = archive_read_data_block(m_archive, &m_data_block, &m_data_block_length, &m_data_block_pos_in_file);
    if (ARCHIVE_OK != return_value) {
        if (ARCHIVE_EOF == return_value) {
            m_reached_eof = true;
            m_data_block = nullptr;
            return ErrorCode_EndOfFile;
        } else {
            SPDLOG_DEBUG("Failed to read data block from libarchive - {}", archive_error_string(m_archive));
            return ErrorCode_Failure;
        }
    }

    m_pos_in_data_block = 0;

    return ErrorCode_Success;
}