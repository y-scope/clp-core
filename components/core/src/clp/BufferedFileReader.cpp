#include "BufferedFileReader.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "BufferReader.hpp"
#include "ErrorCode.hpp"
#include "math_utils.hpp"
#include "ReaderInterface.hpp"

using std::span;
using std::string;
using std::unique_ptr;

namespace clp {
BufferedFileReader::BufferedFileReader(
        std::unique_ptr<ReaderInterface> reader_interface,
        size_t base_buffer_size
)
        : m_reader(std::move(reader_interface)) {
    if (nullptr == m_reader) {
        throw OperationFailed(
                ErrorCode_BadParam,
                __FILENAME__,
                __LINE__,
                "reader_interface cannot be null"
        );
    }
    if (base_buffer_size % cMinBufferSize != 0) {
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__);
    }
    m_base_buffer_size = base_buffer_size;
    m_buffer.resize(m_base_buffer_size);

    m_pos = m_reader->get_pos();
    m_buffer_begin_pos = 0;
    m_buffer_reader = BufferReader{m_buffer.data(), 0};
    m_highest_read_pos = m_pos;
}

auto BufferedFileReader::try_refill_buffer_if_empty() -> ErrorCode {
    if (m_buffer_reader.get_buffer_size() > 0) {
        return ErrorCode_Success;
    }
    return refill_reader_buffer(m_base_buffer_size);
}

void BufferedFileReader::peek_buffered_data(char const*& buf, size_t& peek_size) const {
    m_buffer_reader.peek_buffer(buf, peek_size);
}

auto BufferedFileReader::set_checkpoint() -> size_t {
    if (m_checkpoint_pos.has_value() && m_checkpoint_pos < m_pos
        && m_buffer_reader.get_buffer_size() != m_base_buffer_size)
    {
        drop_content_before_current_pos();
    }
    m_checkpoint_pos = m_pos;
    return m_pos;
}

auto BufferedFileReader::clear_checkpoint() -> void {
    if (false == m_checkpoint_pos.has_value()) {
        return;
    }

    auto error_code = try_seek_from_begin(m_highest_read_pos);
    if (ErrorCode_Success != error_code) {
        // Should never happen
        throw OperationFailed(error_code, __FILENAME__, __LINE__);
    }
    drop_content_before_current_pos();
    m_checkpoint_pos.reset();
}

auto BufferedFileReader::try_get_pos(size_t& pos) -> ErrorCode {
    pos = m_pos;
    return ErrorCode_Success;
}

auto BufferedFileReader::try_seek_from_begin(size_t pos) -> ErrorCode {
    if (pos == m_pos) {
        return ErrorCode_Success;
    }

    auto seek_lower_bound = m_checkpoint_pos.has_value() ? m_checkpoint_pos.value() : m_pos;
    if (pos < seek_lower_bound) {
        return ErrorCode_Unsupported;
    }

    auto error_code = m_buffer_reader.try_seek_from_begin(get_buffer_relative_pos(pos));
    if (ErrorCode_Truncated == error_code) {
        if (false == m_checkpoint_pos.has_value()) {
            // If checkpoint is not set, simply move the file_pos and invalidate
            // the buffer reader
            if (auto const error_code = m_reader->try_seek_from_begin(pos);
                error_code != ErrorCode_Success)
            {
                return error_code;
            }
            m_buffer_reader = BufferReader{m_buffer.data(), 0};
            m_buffer_begin_pos = pos;
        } else {
            auto const num_bytes_to_refill = pos - get_buffer_end_pos();
            error_code = refill_reader_buffer(num_bytes_to_refill);
            if (ErrorCode_EndOfFile == error_code) {
                return ErrorCode_Truncated;
            }
            if (ErrorCode_Success != error_code) {
                return error_code;
            }
            error_code = m_buffer_reader.try_seek_from_begin(get_buffer_relative_pos(pos));
            if (ErrorCode_Success != error_code) {
                return error_code;
            }
        }
    } else if (ErrorCode_Success != error_code) {
        return error_code;
    }
    update_pos(pos);
    return ErrorCode_Success;
}

auto BufferedFileReader::try_read(char* buf, size_t num_bytes_to_read, size_t& num_bytes_read)
        -> ErrorCode {
    if (nullptr == buf) {
        return ErrorCode_BadParam;
    }
    if (num_bytes_to_read == 0) {
        return ErrorCode_Success;
    }

    span dst_view{buf, num_bytes_to_read};
    num_bytes_read = 0;
    while (false == dst_view.empty()) {
        size_t bytes_read{0};
        auto error_code = m_buffer_reader.try_read(dst_view.data(), dst_view.size(), bytes_read);
        if (ErrorCode_Success == error_code) {
            dst_view = dst_view.subspan(bytes_read);
            num_bytes_read += bytes_read;
            update_pos(m_pos + bytes_read);
        } else if (ErrorCode_EndOfFile == error_code) {
            error_code = refill_reader_buffer(m_base_buffer_size);
            if (ErrorCode_EndOfFile == error_code) {
                break;
            }
            if (ErrorCode_Success != error_code) {
                return error_code;
            }
        } else {
            return error_code;
        }
    }
    if (dst_view.size() == num_bytes_to_read) {
        return ErrorCode_EndOfFile;
    }
    return ErrorCode_Success;
}

auto BufferedFileReader::try_read_to_delimiter(
        char delim,
        bool keep_delimiter,
        bool append,
        string& str
) -> ErrorCode {
    if (false == append) {
        str.clear();
    }
    bool found_delim{false};
    size_t total_num_bytes_read{0};
    while (true) {
        size_t num_bytes_read{0};
        if (auto ret_code = m_buffer_reader.try_read_to_delimiter(
                    delim,
                    keep_delimiter,
                    str,
                    found_delim,
                    num_bytes_read
            );
            ret_code != ErrorCode_Success && ret_code != ErrorCode_EndOfFile)
        {
            return ret_code;
        }
        update_pos(m_pos + num_bytes_read);
        total_num_bytes_read += num_bytes_read;
        if (found_delim) {
            break;
        }

        auto error_code = refill_reader_buffer(m_base_buffer_size);
        if (ErrorCode_EndOfFile == error_code) {
            if (0 == total_num_bytes_read) {
                return ErrorCode_EndOfFile;
            }
            break;
        }
        if (ErrorCode_Success != error_code) {
            return error_code;
        }
    }
    return ErrorCode_Success;
}

auto BufferedFileReader::refill_reader_buffer(size_t num_bytes_to_refill) -> ErrorCode {
    auto const buffer_end_pos = get_buffer_end_pos();
    auto const data_size = m_buffer_reader.get_buffer_size();
    auto const available_buffer_space = m_buffer.size() - data_size;

    size_t num_bytes_to_read{0};
    size_t next_buffer_pos{0};
    auto next_buffer_begin_pos = m_buffer_begin_pos;
    if (m_checkpoint_pos.has_value()) {
        num_bytes_to_read = int_round_up_to_multiple(
                buffer_end_pos + num_bytes_to_refill,
                m_base_buffer_size
        );
        // Grow the buffer if necessary
        if (num_bytes_to_read > available_buffer_space) {
            m_buffer.resize(data_size + num_bytes_to_read);
        }
        next_buffer_pos = data_size;
    } else {
        num_bytes_to_read = m_base_buffer_size - (buffer_end_pos % m_base_buffer_size);
        if (num_bytes_to_read > available_buffer_space) {
            // Advance the entire buffer since we don't grow the buffer if there's no checkpoint
            next_buffer_pos = 0;
            next_buffer_begin_pos = buffer_end_pos;
        } else {
            next_buffer_pos = data_size;
        }
    }

    size_t num_bytes_read{0};
    auto const error_code
            = m_reader->try_read(&m_buffer[next_buffer_pos], num_bytes_to_read, num_bytes_read);
    if (error_code != ErrorCode_Success && ErrorCode_EndOfFile != error_code) {
        return error_code;
    }
    // NOTE: We still want to set the buffer reader if no bytes were read on EOF
    m_buffer_reader
            = BufferReader{m_buffer.data(), next_buffer_pos + num_bytes_read, next_buffer_pos};
    m_buffer_begin_pos = next_buffer_begin_pos;
    return error_code;
}

auto BufferedFileReader::drop_content_before_current_pos() -> void {
    auto const buffer_reader_pos = m_buffer_reader.get_pos();
    auto const new_data_size = m_buffer_reader.get_buffer_size() - buffer_reader_pos;
    auto const new_buffer_size = int_round_up_to_multiple(new_data_size, m_base_buffer_size);

    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + buffer_reader_pos);
    m_buffer.resize(new_buffer_size);
    m_buffer_begin_pos += buffer_reader_pos;

    m_buffer_reader = BufferReader{m_buffer.data(), new_data_size};
}

auto BufferedFileReader::update_pos(size_t pos) -> void {
    m_pos = pos;
    m_highest_read_pos = std::max(m_pos, m_highest_read_pos);
}
}  // namespace clp
