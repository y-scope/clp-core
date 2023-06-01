#include "IrMessageParser.hpp"

// C standard libraries

// C++ standard libraries

// Project headers
#include "BufferReader.hpp"
#include "EncodedVariableInterpreter.hpp"
#include "ffi/encoding_methods.hpp"
#include "ffi/ir_stream/protocol_constants.hpp"

// spdlog
#include "spdlog/spdlog.h"

// json
#include "../../../submodules/json/single_include/nlohmann/json.hpp"

using ffi::cVariablePlaceholderEscapeCharacter;
using ffi::four_byte_encoded_variable_t;
using ffi::eight_byte_encoded_variable_t;
using ffi::ir_stream::cProtocol::MagicNumberLength;
using ffi::ir_stream::IRErrorCode;
using ffi::VariablePlaceholder;
using std::string;
using std::vector;

IrMessageParser::IrMessageParser (ReaderInterface& reader) : m_reader(reader) {

    if (false == is_ir_encoded(m_reader, m_is_four_bytes_encoded)) {
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }

    string json_metadata;
    if (false == decode_json_preamble(json_metadata)) {
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }

    const string mocked_ts_pattern = "%Y-%m-%dT%H:%M:%S.%3";
    try {
        auto metadata_json = nlohmann::json::parse(json_metadata);
        string version = metadata_json.at(ffi::ir_stream::cProtocol::Metadata::VersionKey);
        if (version != ffi::ir_stream::cProtocol::Metadata::VersionValue) {
            SPDLOG_ERROR("Unsupported version");
            throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
        }

        // For now, use a fixed timestamp pattern
        m_ts_pattern = TimestampPattern(0, mocked_ts_pattern);

        if (m_is_four_bytes_encoded) {
            m_reference_timestamp = std::stoll(metadata_json.at(
                    ffi::ir_stream::cProtocol::Metadata::ReferenceTimestampKey).get<string>());
            m_msg.set_ts(m_reference_timestamp);
        }

    } catch (const nlohmann::json::parse_error& e) {
        SPDLOG_ERROR("Failed to parse json metadata");
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }
    m_msg.set_ts_pattern(&m_ts_pattern);
}

/**
 * TBD
 * @tparam ir_encoded_variable_t
 * @tparam ConstantHandler
 * @tparam EncodedIntHandler
 * @tparam EncodedFloatHandler
 * @tparam DictVarHandler
 * @param logtype
 * @param encoded_vars
 * @param dict_vars
 * @param constant_handler
 * @param encoded_int_handler
 * @param encoded_float_handler
 * @param dict_var_handler
 * @return
 */
template <typename ir_encoded_variable_t, typename ConstantHandler,
          typename EncodedIntHandler, typename EncodedFloatHandler, typename DictVarHandler>
static bool generic_parse_next_msg(const string& logtype,
                                   const vector<ir_encoded_variable_t>& encoded_vars,
                                   const vector<string>& dict_vars,
                                   ConstantHandler constant_handler,
                                   EncodedIntHandler encoded_int_handler,
                                   EncodedFloatHandler encoded_float_handler,
                                   DictVarHandler dict_var_handler)
{
    size_t encoded_vars_length = encoded_vars.size();
    size_t dict_vars_length = dict_vars.size();
    size_t next_static_text_begin_pos = 0;

    size_t dictionary_vars_ix = 0;
    size_t encoded_vars_ix = 0;
    for (size_t cur_pos = 0; cur_pos < logtype.size(); ++cur_pos) {
        auto c = logtype[cur_pos];
        switch(c) {
            case enum_to_underlying_type(VariablePlaceholder::Float): {
                constant_handler(logtype, next_static_text_begin_pos,
                                 cur_pos - next_static_text_begin_pos);
                next_static_text_begin_pos = cur_pos + 1;
                if (encoded_vars_ix >= encoded_vars_length) {

                    return false;
                }

                auto encoded_float = encoded_vars[encoded_vars_ix];
                encoded_float_handler(encoded_float);
                auto decoded_float = ffi::decode_float_var(encoded_float);

                ++encoded_vars_ix;
                break;
            }

            case enum_to_underlying_type(VariablePlaceholder::Integer): {
                constant_handler(logtype, next_static_text_begin_pos,
                                 cur_pos - next_static_text_begin_pos);
                next_static_text_begin_pos = cur_pos + 1;
                if (encoded_vars_ix >= encoded_vars_length) {
                    SPDLOG_ERROR("Some error message");
                    return false;
                }

                // handle integer
                encoded_int_handler(encoded_vars[encoded_vars_ix]);

                ++encoded_vars_ix;
                break;
            }

            case enum_to_underlying_type(VariablePlaceholder::Dictionary): {
                constant_handler(logtype, next_static_text_begin_pos,
                                 cur_pos - next_static_text_begin_pos);
                next_static_text_begin_pos = cur_pos + 1;
                if (dictionary_vars_ix >= dict_vars_length) {
                    SPDLOG_ERROR("Some error message");
                    return false;
                }
                dict_var_handler(dict_vars[dictionary_vars_ix]);
                ++dictionary_vars_ix;

                break;
            }

            case cVariablePlaceholderEscapeCharacter: {
                // Ensure the escape character is followed by a
                // character that's being escaped
                if (cur_pos == logtype.size() - 1) {
                    SPDLOG_ERROR("Some error message");
                }
                constant_handler(logtype, next_static_text_begin_pos,
                                 cur_pos - next_static_text_begin_pos);

                // Skip the escape character
                next_static_text_begin_pos = cur_pos + 1;
                // The character after the escape character is static text
                // (regardless of whether it is a variable placeholder), so
                // increment cur_pos by 1 to ensure we don't process the
                // next character in any of the other cases (instead it will
                // be added to the message).
                ++cur_pos;

                break;
            }
        }
    }
    // Add remainder
    if (next_static_text_begin_pos < logtype.size()) {
        constant_handler(logtype, next_static_text_begin_pos,
                         logtype.size() - next_static_text_begin_pos);
    }

    return true;
}

bool IrMessageParser::parse_next_encoded_message () {
    if (m_is_four_bytes_encoded) {
        return parse_next_four_bytes_message();
    }
    return parse_next_eight_bytes_message();
}

bool IrMessageParser::parse_next_eight_bytes_message () {
    m_msg.clear();

    epochtime_t ts;
    vector<eight_byte_encoded_variable_t> encoded_vars;
    vector<string> dict_vars;
    string logtype;

    auto error_code = ffi::ir_stream::generic_parse_tokens(
            m_reader, logtype, encoded_vars, dict_vars, ts
    );

    if (IRErrorCode::IRErrorCode_Success != error_code) {
        if (IRErrorCode::IRErrorCode_Eof != error_code) {
            SPDLOG_ERROR("Corrupted IR with error code {}", error_code);
        }
        return false;
    }

    auto constant_handler = [this] (const std::string& value, size_t begin_pos, size_t length) {
        m_msg.append_to_logtype(value, begin_pos, length);
    };

    auto encoded_int_handler = [this] (eight_byte_encoded_variable_t value) {
        auto decoded_int = ffi::decode_integer_var(value);
        m_msg.add_encoded_integer(value, decoded_int.length());
    };

    auto encoded_float_handler = [this] (eight_byte_encoded_variable_t encoded_float) {
        auto decoded_float = ffi::decode_float_var(encoded_float);
        m_msg.add_encoded_float(encoded_float, decoded_float.size());
    };

    auto dict_var_handler = [this] (const string& dict_var) {
        m_msg.add_dictionary_var(dict_var);
    };

    m_msg.set_ts(ts);
    return generic_parse_next_msg(logtype, encoded_vars, dict_vars,
                                  constant_handler, encoded_int_handler,
                                  encoded_float_handler, dict_var_handler);

}

bool IrMessageParser::parse_next_four_bytes_message () {
    m_msg.clear();

    epochtime_t ts;
    vector<four_byte_encoded_variable_t> encoded_vars;
    vector<string> dict_vars;
    string logtype;

    auto error_code = ffi::ir_stream::generic_parse_tokens(
            m_reader, logtype, encoded_vars, dict_vars, ts
    );

    if (IRErrorCode::IRErrorCode_Success != error_code) {
        if (IRErrorCode::IRErrorCode_Eof != error_code) {
            SPDLOG_ERROR("Corrupted IR with error code {}", error_code);
        }
        return false;
    }

    auto constant_handler = [this] (const std::string& value, size_t begin_pos, size_t length) {
        m_msg.append_to_logtype(value, begin_pos, length);
    };

    auto encoded_int_handler = [this] (four_byte_encoded_variable_t value) {
        // assume that we need the actual size
        auto decoded_int = ffi::decode_integer_var(value);
        m_msg.add_encoded_integer(value, decoded_int.length());
    };

    auto encoded_float_handler = [this] (four_byte_encoded_variable_t encoded_float) {
        auto decoded_float = ffi::decode_float_var(encoded_float);
        auto converted_float = EncodedVariableInterpreter::convert_four_bytes_float_to_clp_encoded_float(encoded_float);
        m_msg.add_encoded_float(converted_float, decoded_float.size());
    };

    auto dict_var_handler = [this] (const string& dict_var) {
        encoded_variable_t converted_var;
        if (EncodedVariableInterpreter::convert_string_to_representable_integer_var(dict_var, converted_var)) {
            m_msg.add_encoded_integer(converted_var, dict_var.size());
        } else if (EncodedVariableInterpreter::convert_string_to_representable_float_var(dict_var, converted_var)) {
            m_msg.add_encoded_float(converted_var, dict_var.size());
        } else {
            m_msg.add_dictionary_var(dict_var);
        }
    };

    m_reference_timestamp += ts;
    m_msg.set_ts(m_reference_timestamp);
    return generic_parse_next_msg(logtype, encoded_vars, dict_vars,
                                  constant_handler, encoded_int_handler,
                                  encoded_float_handler, dict_var_handler);
}

bool IrMessageParser::is_ir_encoded (ReaderInterface& reader, bool& is_four_bytes_encoded) {

    // Note. currently this method doesn't recover file pos.
    if (ffi::ir_stream::IRErrorCode_Success !=
        ffi::ir_stream::get_encoding_type(reader, is_four_bytes_encoded)) {
        return false;
    }
    return true;
}

bool IrMessageParser::is_ir_encoded (size_t sequence_length, const char* data) {
    if (sequence_length < MagicNumberLength) {
        return false;
    }
    bool is_four_bytes_encoded;
    BufferReader encoding_data (data, MagicNumberLength);
    // Note. currently this method doesn't recover file pos.
    if (ffi::ir_stream::IRErrorCode_Success !=
        ffi::ir_stream::get_encoding_type(encoding_data, is_four_bytes_encoded)) {
        return false;
    }
    return true;
}

bool IrMessageParser::decode_json_preamble (std::string& json_metadata) {
    // Decode and parse metadata
    ffi::ir_stream::encoded_tag_t metadata_type;
    std::vector<int8_t> metadata_vec;

    if (ffi::ir_stream::IRErrorCode_Success !=
        ffi::ir_stream::decode_preamble(m_reader, metadata_type, metadata_vec)) {
        SPDLOG_ERROR("Failed to parse metadata");
        return false;
    }

    if (ffi::ir_stream::cProtocol::Metadata::EncodingJson != metadata_type) {
        SPDLOG_ERROR("Unexpected metadata type");
        return false;
    }

    json_metadata.assign(reinterpret_cast<const char*>(metadata_vec.data()),
                         metadata_vec.size());

    return true;
}