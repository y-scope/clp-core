#ifndef FFI_IR_STREAM_DECODING_METHODS_HPP
#define FFI_IR_STREAM_DECODING_METHODS_HPP

// C++ standard libraries
#include <string_view>
#include <vector>

// Project headers
#include "../encoding_methods.hpp"

namespace ffi::ir_stream {
    using encoded_tag_t = uint8_t;

    /**
     * Class representing an IR buffer that the decoder sequentially reads from.
     * The class maintains an internal cursor such that every successful read
     * increments the cursor.
     */
    class IrBuffer {
    public:
        IrBuffer (const int8_t* data, size_t size) :
                m_data(data),
                m_size(size),
                m_cursor_pos(0),
                m_internal_cursor_pos(0) {}

        [[nodiscard]] size_t get_cursor_pos () const { return m_cursor_pos; }
        void set_cursor_pos (size_t cursor_pos) { m_cursor_pos = cursor_pos; }

        // The following methods should only be used by the decoder
        void init_internal_pos () { m_internal_cursor_pos = m_cursor_pos; }
        void commit_internal_pos () { m_cursor_pos = m_internal_cursor_pos; }

        /**
         * Tries reading a string view of size = read_size from the ir_buf.
         * On success, returns the string as string_view by reference.
         * @param str_view
         * @param read_size
         * @return true on success, false if the ir_buf doesn't contain enough
         * data to decode
         **/
        [[nodiscard]] bool try_read (std::string_view& str_view, size_t read_size);

        /**
         * Tries reading an integer of size = sizeof(integer_t) from the ir_buf
         * @tparam integer_t
         * @param data Returns the integer
         * @return true on success, false if the ir_buf doesn't contain enough
         * data to decode
         */
        template <typename integer_t>
        [[nodiscard]] bool try_read (integer_t& data);

        /**
         * Tries reading data of size = read_size from the ir_buf. On success,
         * stores the data into dest.
         * @param dest
         * @param read_size
         * @return true on success, false if the ir_buf doesn't contain enough
         * data to decode
         */
        [[nodiscard]] bool try_read (void* dest, size_t read_size);

    private:
        /**
         * @param read_size
         * @return Whether a read of the given size will exceed the size of the
         * buffer
         */
        [[nodiscard]] bool read_will_overflow (size_t read_size) const {
            return (m_internal_cursor_pos + read_size) > m_size;
        }

        const int8_t* const m_data;
        const size_t m_size;
        size_t m_cursor_pos;
        // Internal cursor position to help restore cursor pos if/when decoding
        // fails
        size_t m_internal_cursor_pos;
    };

    /**
     * Struct to hold the timestamp info from the IR stream's metadata
     */
    struct TimestampInfo {
        std::string timestamp_pattern;
        std::string timestamp_pattern_syntax;
        std::string time_zone_id;
    };

    typedef enum {
        IRErrorCode_Success,
        IRErrorCode_Decode_Error,
        IRErrorCode_Eof,
        IRErrorCode_Corrupted_IR,
        IRErrorCode_Corrupted_Metadata,
        IRErrorCode_Incomplete_IR,
        IRErrorCode_Unsupported_Version,
    } IRErrorCode;

    /**
     * Decodes the encoding type for the encoded IR stream
     * @param ir_buf
     * @param is_four_bytes_encoding Returns the encoding type
     * @return ErrorCode_Success on success
     * @return ErrorCode_Corrupted_IR if ir_buf contains invalid IR
     * @return ErrorCode_Incomplete_IR if ir_buf doesn't contain enough data to
     * decode
     */
    IRErrorCode get_encoding_type (IrBuffer & ir_buf, bool & is_four_bytes_encoding);

    namespace eight_byte_encoding {
        /**
         * Decodes the preamble for the eight-byte encoding IR stream.
         * @param ir_buf
         * @param ts_info Returns the timestamp info on success
         * @return IRErrorCode_Success on success
         * @return IRErrorCode_Corrupted_IR if ir_buf contains invalid IR
         * @return IRErrorCode_Incomplete_IR if ir_buf doesn't contain enough
         * data to decode
         * @return IRErrorCode_Unsupported_Version if the IR uses an unsupported
         * version
         * @return IRErrorCode_Corrupted_Metadata if the metadata cannot be
         * decoded
         */
        IRErrorCode decode_preamble (IrBuffer & ir_buf, TimestampInfo & ts_info);

        /**
         * Decodes the next message for the eight-byte encoding IR stream.
         * On success, returns message and timestamp by reference
         * @param ir_buf
         * @param message
         * @param timestamp
         * @return ErrorCode_Success on success
         * @return ErrorCode_Corrupted_IR if ir_buf contains invalid IR
         * @return ErrorCode_Decode_Error if the encoded message cannot be
         * properly decoded
         * @return ErrorCode_Incomplete_IR if ir_buf doesn't contain enough data
         * to decode
         * @return ErrorCode_End_of_IR if the IR ends
         */
        IRErrorCode decode_next_message (IrBuffer & ir_buf, std::string & message,
                                         epoch_time_ms_t& timestamp);
    }

    namespace four_byte_encoding {
        /**
         * Decodes the preamble for the four-byte encoding IR stream.
         * On success, returns ts_info and reference_ts by reference
         * @param ir_buf
         * @param ts_info
         * @param reference_ts
         * @return IRErrorCode_Success on success
         * @return IRErrorCode_Corrupted_IR if ir_buf contains invalid IR
         * @return IRErrorCode_Incomplete_IR if ir_buf doesn't contain enough
         * data to decode
         * @return IRErrorCode_Unsupported_Version if the IR has a version that
         * is not supported
         * @return IRErrorCode_Corrupted_Metadata if the metadata cannot be
         * decoded
         */
        IRErrorCode decode_preamble (IrBuffer & ir_buf,
                                     TimestampInfo& ts_info,
                                     epoch_time_ms_t& reference_ts);

        /**
         * Decodes the next message for the four-byte encoding IR stream.
         * On success, returns message and ts_delta by reference and sets
         * get_cursor_pos in ir_buf to be the end of decoded message
         * @param ir_buf
         * @param message
         * @param timestamp_delta
         * @return ErrorCode_Success on success
         * @return ErrorCode_Corrupted_IR if ir_buf contains invalid IR
         * @return ErrorCode_Decode_Error if the encoded message cannot be
         * properly decoded
         * @return ErrorCode_Incomplete_IR if ir_buf doesn't contain enough data
         * to decode
         * @return ErrorCode_End_of_IR if the IR ends
         */
        IRErrorCode decode_next_message (IrBuffer & ir_buf, std::string & message,
                                         epoch_time_ms_t& timestamp_delta);
    }
}

#endif //FFI_IR_STREAM_DECODING_METHODS_HPP
