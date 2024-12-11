#include "StringReader.hpp"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>

#include <boost/filesystem.hpp>

using std::string;

namespace clp {
StringReader::~StringReader() {
    close();
}

ErrorCode StringReader::try_read(char* buf, size_t num_bytes_to_read, size_t& num_bytes_read) {
    if (m_input_string.empty()) {
        return ErrorCode_NotInit;
    }
    if (nullptr == buf) {
        return ErrorCode_BadParam;
    }

    if (m_pos == m_input_string.size()) {
        return ErrorCode_EndOfFile;
    }

    if (m_pos + num_bytes_to_read > m_input_string.size()) {
        num_bytes_to_read = m_input_string.size() - m_pos;
    }
    for (int i = 0; i < num_bytes_to_read; i++) {
        buf[i] = m_input_string[i + m_pos];
    }
    num_bytes_read = num_bytes_to_read;
    m_pos += num_bytes_read;
    return ErrorCode_Success;
}

ErrorCode StringReader::try_seek_from_begin(size_t pos) {
    m_pos = pos;
    return ErrorCode_Success;
}

ErrorCode StringReader::try_get_pos(size_t& pos) {
    pos = m_pos;
    return ErrorCode_Success;
}

ErrorCode StringReader::try_open(string const& input_string) {
    m_input_string = input_string;
    m_string_is_set = true;
    return ErrorCode_Success;
}

void StringReader::open(string const& input_string) {
    try_open(input_string);
}

void StringReader::close() {
    m_input_string.clear();
    m_string_is_set = false;
    m_pos = 0;
}
}  // namespace clp
