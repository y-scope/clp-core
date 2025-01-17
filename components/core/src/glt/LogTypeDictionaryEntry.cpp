#include "LogTypeDictionaryEntry.hpp"

#include "ir/parsing.hpp"
#include "ir/types.hpp"
#include "type_utils.hpp"
#include "Utils.hpp"

using glt::ir::VariablePlaceholder;
using std::string;
using std::string_view;

namespace glt {
size_t LogTypeDictionaryEntry::get_placeholder_info(
        size_t placeholder_ix,
        VariablePlaceholder& placeholder
) const {
    if (placeholder_ix >= m_placeholder_positions.size()) {
        return SIZE_MAX;
    }

    auto var_position = m_placeholder_positions[placeholder_ix];
    placeholder = static_cast<VariablePlaceholder>(m_value[var_position]);

    return m_placeholder_positions[placeholder_ix];
}

size_t LogTypeDictionaryEntry::get_variable_info(
        size_t var_ix,
        ir::VariablePlaceholder& placeholder
) const {
    if (var_ix >= m_variable_positions.size()) {
        return SIZE_MAX;
    }

    auto var_position = m_variable_positions[var_ix];
    placeholder = static_cast<VariablePlaceholder>(m_value[var_position]);

    return var_position;
}

size_t LogTypeDictionaryEntry::get_data_size() const {
    // NOTE: sizeof(vector[0]) is executed at compile time so there's no risk of an exception at
    // runtime
    return sizeof(m_id) + m_value.length()
           + m_placeholder_positions.size() * sizeof(m_placeholder_positions[0])
           + m_variable_positions.size() * sizeof(m_variable_positions[0])
           + m_ids_of_segments_containing_entry.size() * sizeof(segment_id_t);
}

void LogTypeDictionaryEntry::add_constant(
        string const& value_containing_constant,
        size_t begin_pos,
        size_t length
) {
    m_value.append(value_containing_constant, begin_pos, length);
}

void LogTypeDictionaryEntry::add_dictionary_var() {
    m_placeholder_positions.push_back(m_value.length());
    add_dict_var(m_value);
}

void LogTypeDictionaryEntry::add_int_var() {
    m_placeholder_positions.push_back(m_value.length());
    add_int_var(m_value);
}

void LogTypeDictionaryEntry::add_float_var() {
    m_placeholder_positions.push_back(m_value.length());
    add_float_var(m_value);
}

void LogTypeDictionaryEntry::add_escape() {
    m_placeholder_positions.push_back(m_value.length());
    add_escape(m_value);
    ++m_num_escaped_placeholders;
}

bool LogTypeDictionaryEntry::parse_next_var(
        string const& msg,
        size_t& var_begin_pos,
        size_t& var_end_pos,
        string& var
) {
    auto last_var_end_pos = var_end_pos;
    // clang-format off
    auto escape_handler = [&](
            [[maybe_unused]] string_view constant,
            [[maybe_unused]] size_t char_to_escape_pos,
            string& logtype
    ) -> void {
        m_placeholder_positions.push_back(logtype.size());
        ++m_num_escaped_placeholders;
        logtype += enum_to_underlying_type(VariablePlaceholder::Escape);
    };
    // clang-format on
    if (ir::get_bounds_of_next_var(msg, var_begin_pos, var_end_pos)) {
        // Append to log type: from end of last variable to start of current variable
        auto constant = static_cast<string_view>(msg).substr(
                last_var_end_pos,
                var_begin_pos - last_var_end_pos
        );
        ir::append_constant_to_logtype(constant, escape_handler, m_value);

        var.assign(msg, var_begin_pos, var_end_pos - var_begin_pos);
        return true;
    }
    if (last_var_end_pos < msg.length()) {
        // Append to log type: from end of last variable to end
        auto constant = static_cast<string_view>(msg).substr(
                last_var_end_pos,
                msg.length() - last_var_end_pos
        );
        ir::append_constant_to_logtype(constant, escape_handler, m_value);
    }

    return false;
}

void LogTypeDictionaryEntry::clear() {
    m_value.clear();
    m_placeholder_positions.clear();
    m_variable_positions.clear();
    m_num_escaped_placeholders = 0;
}

void LogTypeDictionaryEntry::write_to_file(streaming_compression::Compressor& compressor) const {
    compressor.write_numeric_value(m_id);

    compressor.write_numeric_value<uint64_t>(m_value.length());
    compressor.write_string(m_value);
}

ErrorCode LogTypeDictionaryEntry::try_read_from_file(
        streaming_compression::Decompressor& decompressor
) {
    clear();

    ErrorCode error_code;

    error_code = decompressor.try_read_numeric_value<logtype_dictionary_id_t>(m_id);
    if (ErrorCode_Success != error_code) {
        return error_code;
    }

    uint64_t escaped_value_length;
    error_code = decompressor.try_read_numeric_value(escaped_value_length);
    if (ErrorCode_Success != error_code) {
        return error_code;
    }
    string escaped_value;
    error_code = decompressor.try_read_string(escaped_value_length, escaped_value);
    if (ErrorCode_Success != error_code) {
        return error_code;
    }

    // Decode encoded logtype
    bool is_escaped = false;
    string constant;
    for (size_t i = 0; i < escaped_value_length; ++i) {
        char c = escaped_value[i];

        if (is_escaped) {
            constant += c;
            is_escaped = false;
        } else if (enum_to_underlying_type(VariablePlaceholder::Escape) == c) {
            is_escaped = true;
            add_constant(constant, 0, constant.length());
            constant.clear();
            add_escape();
        } else {
            if (enum_to_underlying_type(VariablePlaceholder::Integer) == c) {
                add_constant(constant, 0, constant.length());
                constant.clear();
                m_variable_positions.push_back(m_value.length());
                add_int_var();
            } else if (enum_to_underlying_type(VariablePlaceholder::Float) == c) {
                add_constant(constant, 0, constant.length());
                constant.clear();
                m_variable_positions.push_back(m_value.length());
                add_float_var();
            } else if (enum_to_underlying_type(VariablePlaceholder::Dictionary) == c) {
                add_constant(constant, 0, constant.length());
                constant.clear();
                m_variable_positions.push_back(m_value.length());
                add_dictionary_var();
            } else {
                constant += c;
            }
        }
    }
    if (constant.empty() == false) {
        add_constant(constant, 0, constant.length());
    }

    return error_code;
}

void LogTypeDictionaryEntry::read_from_file(streaming_compression::Decompressor& decompressor) {
    auto error_code = try_read_from_file(decompressor);
    if (ErrorCode_Success != error_code) {
        throw OperationFailed(error_code, __FILENAME__, __LINE__);
    }
}

string LogTypeDictionaryEntry::get_human_readable_value() const {
    string human_readable_value;

    size_t constant_begin_pos = 0;
    for (size_t placeholder_ix = 0; placeholder_ix < get_num_placeholders(); ++placeholder_ix) {
        VariablePlaceholder placeholder;
        size_t placeholder_pos = get_placeholder_info(placeholder_ix, placeholder);

        // Add the constant that's between the last variable and this one, with newlines escaped
        human_readable_value
                .append(m_value, constant_begin_pos, placeholder_pos - constant_begin_pos);

        if (VariablePlaceholder::Dictionary == placeholder) {
            human_readable_value += "v";
        } else if (VariablePlaceholder::Float == placeholder) {
            human_readable_value += "f";
        } else if (VariablePlaceholder::Integer == placeholder) {
            human_readable_value += "i";
        }
        // Move past the variable delimiter
        constant_begin_pos = placeholder_pos + 1;
    }
    // Append remainder of value, if any
    if (constant_begin_pos < m_value.length()) {
        human_readable_value.append(m_value, constant_begin_pos, string::npos);
    }
    return human_readable_value;
}

}  // namespace glt
