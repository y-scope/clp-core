#include "BufferReader.hpp"

// C++ standard libraries
#include <cstring>

// Project headers
#include "spdlog/spdlog.h"

using std::string_view;

[[nodiscard]] ErrorCode BufferReader::try_get_pos (size_t& pos) {
    if (nullptr == m_buffer || 0 == m_size) {
        return ErrorCode_NotInit;
    }
    pos = m_cursor_pos;
    return ErrorCode_Success;
}

[[nodiscard]] ErrorCode BufferReader::try_seek_from_begin (size_t pos) {
    if (nullptr == m_buffer || 0 == m_size) {
        return ErrorCode_NotInit;
    }
    if (pos > m_size) {
        return ErrorCode_OutOfBounds;
    }
    m_cursor_pos = pos;
    return ErrorCode_Success;
}

ErrorCode BufferReader::try_read (char* buf, size_t num_bytes_to_read, size_t& num_bytes_read) {
    // this is not defined by specifications,
    // but we need this strong behavior for the upper class
    num_bytes_read = 0;

    if (nullptr == m_buffer) {
        return ErrorCode_NotInit;
    }
    if (nullptr == buf) {
        return ErrorCode_BadParam;
    }

    if (m_cursor_pos >= m_size) {
        return ErrorCode_EndOfFile;
    }

    num_bytes_read = std::min(m_size - m_cursor_pos, num_bytes_to_read);
    memcpy(buf, m_buffer + m_cursor_pos, num_bytes_read);
    m_cursor_pos += num_bytes_read;
    return ErrorCode_Success;
}

bool BufferReader::try_read_string_view (string_view& str_view, size_t read_size) {
    if ((m_cursor_pos + read_size) > m_size) {
        return false;
    }
    str_view = string_view(reinterpret_cast<const char*>(m_buffer + m_cursor_pos),
                           read_size);
    m_cursor_pos += read_size;
    return true;
}