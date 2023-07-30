#include "BufferedFileReader.hpp"

// Boost libraries
#include <boost/filesystem.hpp>

// C standard libraries
// C libraries
#include <sys/stat.h>
#include <fcntl.h>

// C++ standard libraries
#include <cerrno>

// Project headers
#include <spdlog/spdlog.h>

using std::make_unique;
using std::move;
using std::string;

static ErrorCode try_read_into_buffer(int fd, char* buffer, size_t num_bytes_to_read,
                                      size_t& num_bytes_read);

BufferedFileReader::BufferedFileReader () {
    m_file_pos = 0;
    m_fd = -1;
    m_checkpoint_pos.reset();
    if (auto error_code = set_buffer_size(cDefaultBufferSize);
        ErrorCode_Success != error_code) {
        SPDLOG_ERROR("Failed to init reader buffer size to be {}", cDefaultBufferSize);
        throw OperationFailed(error_code, __FILENAME__, __LINE__);
    }
    m_buffer = make_unique<char[]>(m_buffer_size);
}

BufferedFileReader::~BufferedFileReader () {
    close();
}

ErrorCode BufferedFileReader::try_get_pos (size_t& pos) {
    if (-1 == m_fd) {
        return ErrorCode_NotInit;
    }
    pos = m_file_pos;
    return ErrorCode_Success;
}

ErrorCode BufferedFileReader::try_seek_from_begin (size_t pos) {
    if (m_fd == -1) {
        return ErrorCode_NotInit;
    }
    if (pos == m_file_pos) {
        return ErrorCode_Success;
    }

    if (pos <= m_file_pos) {
        if (false == m_checkpoint_pos.has_value()) {
            SPDLOG_ERROR("Error: Seek back when checkpoint is not enabled");
            return ErrorCode_Failure;
        }
        if (pos < m_checkpoint_pos) {
            SPDLOG_ERROR("Error: trying to seek to {} which is ahead of checkpoint: {}",
                         pos, m_checkpoint_pos.value());
            return ErrorCode_Failure;
        }
        // adjust the buffer reader pos
        m_buffer_reader->seek_from_begin(get_equivalent_buffer_pos(pos));
    } else {
        if (ErrorCode_Success == m_buffer_reader->try_seek_from_begin(get_equivalent_buffer_pos(pos))) {
            m_file_pos = pos;
            highest_read_pos = std::max(highest_read_pos, m_file_pos);
            return ErrorCode_Success;
        }
        // Handle the case where buffer is empty or doesn't contain enough data for seek
        if (false == m_checkpoint_pos.has_value()) {
            // if checkpoint is not set, simply move the file_pos and invalidate the buffer reader
            auto offset = lseek(m_fd, pos, SEEK_SET);
            if (offset == -1) {
                return ErrorCode_errno;
            }
            m_buffer_reader.emplace(m_buffer.get(), 0);
            m_buffer_begin_pos = pos;
        } else {
            size_t num_bytes_to_refill = pos - (m_buffer_begin_pos + m_buffer_reader->get_buffer_size());

            size_t num_bytes_refilled {0};
            auto error_code = refill_reader_buffer(num_bytes_to_refill, num_bytes_refilled);
            if (ErrorCode_EndOfFile == error_code || num_bytes_refilled < num_bytes_to_refill) {
                SPDLOG_ERROR("not expecting to seek pass the Entire file");
                throw OperationFailed(ErrorCode_EndOfFile, __FILENAME__, __LINE__);
            }
            if (ErrorCode_Success != error_code) {
                return error_code;
            }
            m_buffer_reader->seek_from_begin(get_equivalent_buffer_pos(pos));
        }
    }
    m_file_pos = pos;
    highest_read_pos = std::max(highest_read_pos, m_file_pos);
    return ErrorCode_Success;
}

ErrorCode BufferedFileReader::try_read (char* buf, size_t num_bytes_to_read,
                                        size_t& num_bytes_read) {
    if (-1 == m_fd) {
        return ErrorCode_NotInit;
    }
    if (nullptr == buf) {
        return ErrorCode_BadParam;
    }

    num_bytes_read = 0;
    // keep reading until enough data is read or an eof is seen
    while (true) {
        size_t bytes_read {0};
        auto remaining_bytes_to_read = num_bytes_to_read - num_bytes_read;
        auto error_code = m_buffer_reader->try_read(buf + num_bytes_read, remaining_bytes_to_read, bytes_read);
        // here EOF is allowed because it simply means we have exhausted the
        // buffer, but not necessarily the file itself
        if (ErrorCode_Success != error_code && ErrorCode_EndOfFile != error_code) {
            return error_code;
        }
        num_bytes_read += bytes_read;
        m_file_pos += bytes_read;

        if (num_bytes_read == num_bytes_to_read) {
            break;
        }
        // refill the buffer if more bytes are to be read
        error_code = refill_reader_buffer(m_buffer_size);
        if (ErrorCode_EndOfFile == error_code) {
            break;
        } else if (ErrorCode_Success != error_code) {
            return error_code;
        }
    }
    if (num_bytes_read == 0) {
        return ErrorCode_EndOfFile;
    }
    highest_read_pos = std::max(highest_read_pos, m_file_pos);
    return ErrorCode_Success;
}

ErrorCode BufferedFileReader::try_read_to_delimiter (char delim, bool keep_delimiter,
                                                     bool append, string& str) {
    if (-1 == m_fd) {
        return ErrorCode_NotInit;
    }

    bool found_delim {false};
    size_t total_append_length {0};
    while (false == found_delim) {
        size_t length {0};
        if (ErrorCode_Success == m_buffer_reader->try_read_to_delimiter(delim, keep_delimiter, append, str, length)) {
            found_delim = true;
        }
        m_file_pos += length;
        total_append_length += length;

        if (false == found_delim) {
            auto error_code = refill_reader_buffer(m_buffer_size);
            if (ErrorCode_EndOfFile == error_code) {
                if (total_append_length == 0) {
                    return ErrorCode_EndOfFile;
                }
                return ErrorCode_Success;
            } else if (ErrorCode_Success != error_code) {
                return error_code;
            }
        }
    }
    highest_read_pos = std::max(highest_read_pos, m_file_pos);
    return ErrorCode_Success;
}

ErrorCode BufferedFileReader::try_open (const string& path) {
    // Cleanup in case caller forgot to call close before calling this function
    close();

    m_fd = ::open(path.c_str(), O_RDONLY);
    if (-1 == m_fd) {
        if (ENOENT == errno) {
            return ErrorCode_FileNotFound;
        }
        return ErrorCode_errno;
    }
    m_path = path;
    m_file_pos = 0;
    m_buffer_begin_pos = 0;
    m_buffer_reader.emplace(m_buffer.get(), 0);
    return ErrorCode_Success;
}

void BufferedFileReader::open (const string& path) {
    ErrorCode error_code = try_open(path);
    if (ErrorCode_Success != error_code) {
        if (ErrorCode_FileNotFound == error_code) {
            SPDLOG_ERROR("File not found: {}", boost::filesystem::weakly_canonical(path).string());
            throw OperationFailed(error_code, __FILENAME__, __LINE__);
        } else {
            throw OperationFailed(error_code, __FILENAME__, __LINE__);
        }
    }
}

void BufferedFileReader::close () {
    if (-1 != m_fd) {
        // NOTE: We don't check errors for fclose since it seems
        // the only reason it could fail is if it was interrupted by a signal
        ::close(m_fd);
        m_fd = -1;

        if (m_checkpoint_pos.has_value()) {
            m_buffer = make_unique<char[]>(m_buffer_size);
            m_checkpoint_pos.reset();
        }
    }
}

ErrorCode BufferedFileReader::try_fstat (struct stat& stat_buffer) const {
    if (-1 == m_fd) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }

    auto return_value = fstat(m_fd, &stat_buffer);
    if (0 != return_value) {
        return ErrorCode_errno;
    }
    return ErrorCode_Success;
}

void BufferedFileReader::resize_buffer_from_pos (size_t pos) {

    const auto copy_size = m_buffer_reader->get_buffer_size() - pos;
    // Use a quantized size for the underlying buffer size
    auto new_buffer_size = quantize_to_buffer_size(copy_size);

    auto new_buffer = make_unique<char[]>(new_buffer_size);
    memcpy(new_buffer.get(), &m_buffer[pos], copy_size);
    m_buffer = std::move(new_buffer);
    m_buffer_begin_pos += pos;

    m_buffer_reader.emplace(m_buffer.get(), copy_size);
}

size_t BufferedFileReader::set_checkpoint() {
    if (m_checkpoint_pos.has_value()) {
        if (m_checkpoint_pos > m_file_pos) {
            throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
        } else if (m_checkpoint_pos < m_file_pos) {
            if (m_buffer_reader->get_buffer_size() != m_buffer_size) {
                // allocate new buffer for buffered data starting from pos
                resize_buffer_from_pos(m_buffer_reader->get_pos());
                m_buffer_reader->seek_from_begin(get_equivalent_buffer_pos(m_file_pos));
            }
        }
    }
    m_checkpoint_pos = m_file_pos;
    return m_file_pos;
}

void BufferedFileReader::clear_checkpoint () {
    if (false == m_checkpoint_pos.has_value()) {
        return;
    }
    const auto buffer_end_file_pos = m_buffer_reader->get_buffer_size() + m_buffer_begin_pos;
    if (buffer_end_file_pos <= highest_read_pos || buffer_end_file_pos - highest_read_pos > m_buffer_size) {
        throw OperationFailed(ErrorCode_Corrupt, __FILENAME__, __LINE__);
    }

    m_file_pos = highest_read_pos;
    resize_buffer_from_pos(get_equivalent_buffer_pos(m_file_pos));
    m_checkpoint_pos.reset();
}

ErrorCode BufferedFileReader::set_buffer_size (size_t buffer_size) {
    if (m_fd != -1) {
        SPDLOG_ERROR("Buffer size can not be changed when the file is open");
        return ErrorCode_Failure;
    }
    if (buffer_size == 0) {
        SPDLOG_ERROR("Buffer size can not be set to 0");
        return ErrorCode_BadParam;
    }
    if (buffer_size % 4096 != 0) {
        SPDLOG_ERROR("Buffer size {} is not a multiple of page size", buffer_size);
        return ErrorCode_BadParam;
    }
    // fast calculation to check if buffer_size is a power of 2 leveraged
    // from https://stackoverflow.com/questions/51094594/
    // how-to-check-if-exactly-one-bit-is-set-in-an-int
    if (false == (!(buffer_size & (buffer_size-1)))) {
        SPDLOG_ERROR("Buffer size {} is not a power of 2", buffer_size);
        return ErrorCode_BadParam;
    }

    m_buffer_exp = static_cast<size_t>(log2(static_cast<double>(buffer_size)));
    m_buffer_size = buffer_size;
    return ErrorCode_Success;
}

ErrorCode BufferedFileReader::peek_buffered_data (size_t size_to_peek, const char*& data_ptr,
                                                  size_t& peek_size) {
    if (-1 == m_fd) {
        return ErrorCode_NotInit;
    }
    // Refill the buffer if it is not loaded yet
    if (false == m_buffer_reader.has_value()) {
        auto error_code = refill_reader_buffer(m_buffer_size);
        if (ErrorCode_Success != error_code) {
            data_ptr = nullptr;
            peek_size = 0;
            return error_code;
        }
    }
    m_buffer_reader->peek_buffer(size_to_peek, data_ptr, peek_size);
    return ErrorCode_Success;
}

size_t BufferedFileReader::quantize_to_buffer_size (size_t size) {
    return (1 + ((size - 1) >> m_buffer_exp)) << m_buffer_exp;
}

ErrorCode BufferedFileReader::refill_reader_buffer (size_t refill_size) {
    size_t num_bytes_refilled;
    return refill_reader_buffer (refill_size, num_bytes_refilled);
}

ErrorCode BufferedFileReader::refill_reader_buffer (size_t num_bytes_to_refill,
                                                    size_t& num_bytes_refilled) {
    num_bytes_refilled = 0;
    const auto quantized_refill_size = quantize_to_buffer_size(num_bytes_to_refill);
    if (false == m_checkpoint_pos.has_value()) {
        auto error_code = try_read_into_buffer(m_fd, m_buffer.get(),
                                               quantized_refill_size, num_bytes_refilled);
        if (error_code != ErrorCode_Success) {
            return error_code;
        }
        m_buffer_reader.emplace(m_buffer.get(), num_bytes_refilled);
        m_buffer_begin_pos = m_file_pos;
    } else {
        // Messy way of copying data from old buffer to new buffer
        auto data_size = m_buffer_reader->get_buffer_size();
        auto new_buffer = make_unique<char[]>(data_size + quantized_refill_size);
        memcpy(new_buffer.get(), m_buffer.get(), data_size);

        // Read data to the new buffer, with offset = data_size
        auto error_code = try_read_into_buffer(m_fd, &new_buffer[data_size], quantized_refill_size,
                                               num_bytes_refilled);
        if (error_code != ErrorCode_Success) {
            return error_code;
        }
        m_buffer = std::move(new_buffer);
        m_buffer_reader.emplace(m_buffer.get(), data_size + num_bytes_refilled);
        m_buffer_reader->seek_from_begin(get_equivalent_buffer_pos(m_file_pos));
    }
    return ErrorCode_Success;
}

static ErrorCode try_read_into_buffer(int fd, char* buffer, size_t num_bytes_to_read,
                                      size_t& num_bytes_read) {
    num_bytes_read = 0;
    // keep reading from the fd until enough bytes are read
    while (true) {
        auto remaining_bytes_to_read = num_bytes_to_read - num_bytes_read;
        auto bytes_read = ::read(fd, buffer + num_bytes_read, remaining_bytes_to_read);
        if (bytes_read == -1) {
            return ErrorCode_errno;
        }
        if (bytes_read == 0) {
            break;
        }
        num_bytes_read += bytes_read;
        if (num_bytes_read == num_bytes_to_read) {
            return ErrorCode_Success;
        }
    }
    if (num_bytes_read == 0) {
        return ErrorCode_EndOfFile;
    }
    return ErrorCode_Success;
}
