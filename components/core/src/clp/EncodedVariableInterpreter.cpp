#include "EncodedVariableInterpreter.hpp"

#include <cassert>
#include <cmath>

#include <string_utils/string_utils.hpp>

#include "Defs.h"
#include "ffi/ir_stream/decoding_methods.hpp"
#include "ir/LogEvent.hpp"
#include "ir/types.hpp"
#include "spdlog_with_specializations.hpp"
#include "type_utils.hpp"

using clp::ffi::cEightByteEncodedFloatDigitsBitMask;
using clp::ir::eight_byte_encoded_variable_t;
using clp::ir::four_byte_encoded_variable_t;
using clp::ir::LogEvent;
using clp::ir::VariablePlaceholder;
using std::string;
using std::unordered_set;
using std::vector;

namespace clp {
variable_dictionary_id_t EncodedVariableInterpreter::decode_var_dict_id(
        encoded_variable_t encoded_var
) {
    return bit_cast<variable_dictionary_id_t>(encoded_var);
}

bool EncodedVariableInterpreter::convert_string_to_representable_integer_var(
        string const& value,
        encoded_variable_t& encoded_var
) {
    size_t length = value.length();
    if (0 == length) {
        // Empty string cannot be converted
        return false;
    }

    // Ensure start of value is an integer with no zero-padding or positive sign
    if ('-' == value[0]) {
        // Ensure first character after sign is a non-zero integer
        if (length < 2 || value[1] < '1' || '9' < value[1]) {
            return false;
        }
    } else {
        // Ensure first character is a digit
        if (value[0] < '0' || '9' < value[0]) {
            return false;
        }

        // Ensure value is not zero-padded
        if (length > 1 && '0' == value[0]) {
            return false;
        }
    }

    int64_t result;
    if (false == string_utils::convert_string_to_int(value, result)) {
        // Conversion failed
        return false;
    } else {
        encoded_var = result;
    }

    return true;
}

bool EncodedVariableInterpreter::convert_string_to_representable_float_var(
        string const& value,
        encoded_variable_t& encoded_var
) {
    if (value.empty()) {
        // Can't convert an empty string
        return false;
    }

    size_t pos = 0;
    constexpr size_t cMaxDigitsInRepresentableFloatVar = 16;
    // +1 for decimal point
    size_t max_length = cMaxDigitsInRepresentableFloatVar + 1;

    // Check for a negative sign
    bool is_negative = false;
    if ('-' == value[pos]) {
        is_negative = true;
        ++pos;
        // Include sign in max length
        ++max_length;
    }

    // Check if value can be represented in encoded format
    if (value.length() > max_length) {
        return false;
    }

    size_t num_digits = 0;
    size_t decimal_point_pos = string::npos;
    uint64_t digits = 0;
    for (; pos < value.length(); ++pos) {
        auto c = value[pos];
        if ('0' <= c && c <= '9') {
            digits *= 10;
            digits += (c - '0');
            ++num_digits;
        } else if (string::npos == decimal_point_pos && '.' == c) {
            decimal_point_pos = value.length() - 1 - pos;
        } else {
            // Invalid character
            return false;
        }
    }
    if (string::npos == decimal_point_pos || 0 == decimal_point_pos || 0 == num_digits) {
        // No decimal point found, decimal point is after all digits, or no digits found
        return false;
    }

    // Encode into 64 bits with the following format (from MSB to LSB):
    // -  1 bit : is negative
    // -  1 bit : unused
    // - 54 bits: The digits of the float without the decimal, as an integer
    // -  4 bits: # of decimal digits minus 1
    //     - This format can represent floats with between 1 and 16 decimal digits, so we use 4 bits
    //       and map the range [1, 16] to [0x0, 0xF]
    // -  4 bits: position of the decimal from the right minus 1
    //     - To see why the position is taken from the right, consider
    //       (1) "-123456789012345.6", (2) "-.1234567890123456", and
    //       (3) ".1234567890123456"
    //         - For (1), the decimal point is at index 16 from the left and index 1 from the right.
    //         - For (2), the decimal point is at index 1 from the left and index 16 from the right.
    //         - For (3), the decimal point is at index 0 from the left and index 16 from the right.
    //         - So if we take the decimal position from the left, it can range from 0 to 16 because
    //           of the negative sign. Whereas from the right, the negative sign is inconsequential.
    //     - Thus, we use 4 bits and map the range [1, 16] to [0x0, 0xF].
    uint64_t encoded_float = 0;
    if (is_negative) {
        encoded_float = 1;
    }
    encoded_float <<= 55;  // 1 unused + 54 for digits of the float
    encoded_float |= digits & cEightByteEncodedFloatDigitsBitMask;
    encoded_float <<= 4;
    encoded_float |= (num_digits - 1) & 0x0F;
    encoded_float <<= 4;
    encoded_float |= (decimal_point_pos - 1) & 0x0F;
    encoded_var = bit_cast<encoded_variable_t>(encoded_float);

    return true;
}

void EncodedVariableInterpreter::convert_encoded_float_to_string(
        encoded_variable_t encoded_var,
        string& value
) {
    auto encoded_float = bit_cast<uint64_t>(encoded_var);

    // Decode according to the format described in
    // EncodedVariableInterpreter::convert_string_to_representable_float_var
    uint8_t decimal_pos = (encoded_float & 0x0F) + 1;
    encoded_float >>= 4;
    uint8_t num_digits = (encoded_float & 0x0F) + 1;
    encoded_float >>= 4;
    uint64_t digits = encoded_float & cEightByteEncodedFloatDigitsBitMask;
    encoded_float >>= 55;
    bool is_negative = encoded_float > 0;

    size_t value_length = num_digits + 1 + is_negative;
    value.resize(value_length);
    size_t num_chars_to_process = value_length;

    // Add sign
    if (is_negative) {
        value[0] = '-';
        --num_chars_to_process;
    }

    // Decode until the decimal or the non-zero digits are exhausted
    size_t pos = value_length - 1;
    for (; pos > (value_length - 1 - decimal_pos) && digits > 0; --pos) {
        value[pos] = (char)('0' + (digits % 10));
        digits /= 10;
        --num_chars_to_process;
    }

    if (digits > 0) {
        // Skip decimal since it's added at the end
        --pos;
        --num_chars_to_process;

        while (digits > 0) {
            value[pos--] = (char)('0' + (digits % 10));
            digits /= 10;
            --num_chars_to_process;
        }
    }

    // Add remaining zeros
    for (; num_chars_to_process > 0; --num_chars_to_process) {
        value[pos--] = '0';
    }

    // Add decimal
    value[value_length - 1 - decimal_pos] = '.';
}

void EncodedVariableInterpreter::encode_and_add_to_dictionary(
        string const& message,
        LogTypeDictionaryEntry& logtype_dict_entry,
        VariableDictionaryWriter& var_dict,
        vector<encoded_variable_t>& encoded_vars,
        vector<variable_dictionary_id_t>& var_ids
) {
    // Extract all variables and add to dictionary while building logtype
    size_t var_begin_pos = 0;
    size_t var_end_pos = 0;
    string var_str;
    logtype_dict_entry.clear();
    // To avoid reallocating the logtype as we append to it, reserve enough space to hold the entire
    // message
    logtype_dict_entry.reserve_constant_length(message.length());
    while (logtype_dict_entry.parse_next_var(message, var_begin_pos, var_end_pos, var_str)) {
        auto encoded_var = encode_var(var_str, logtype_dict_entry, var_dict, var_ids);
        encoded_vars.push_back(encoded_var);
    }
}

template <typename encoded_variable_t>
void EncodedVariableInterpreter::encode_and_add_to_dictionary(
        LogEvent<encoded_variable_t> const& log_event,
        LogTypeDictionaryEntry& logtype_dict_entry,
        VariableDictionaryWriter& var_dict,
        std::vector<eight_byte_encoded_variable_t>& encoded_vars,
        std::vector<variable_dictionary_id_t>& var_ids,
        size_t& raw_num_bytes
) {
    logtype_dict_entry.clear();
    auto const& log_message = log_event.get_message();
    logtype_dict_entry.reserve_constant_length(log_message.get_logtype().length());

    raw_num_bytes = 0;

    auto constant_handler = [&](std::string const& value, size_t begin_pos, size_t length) {
        raw_num_bytes += length;
        logtype_dict_entry.add_constant(value, begin_pos, length);
    };

    auto encoded_int_handler = [&](encoded_variable_t encoded_var) {
        raw_num_bytes += ffi::decode_integer_var(encoded_var).length();
        logtype_dict_entry.add_int_var();

        eight_byte_encoded_variable_t eight_byte_encoded_var{};
        if constexpr (std::is_same_v<encoded_variable_t, eight_byte_encoded_variable_t>) {
            eight_byte_encoded_var = encoded_var;
        } else {  // std::is_same_v<encoded_variable_t, four_byte_encoded_variable_t>
            eight_byte_encoded_var = ffi::encode_four_byte_integer_as_eight_byte(encoded_var);
        }
        encoded_vars.push_back(eight_byte_encoded_var);
    };

    auto encoded_float_handler = [&](four_byte_encoded_variable_t encoded_var) {
        raw_num_bytes += ffi::decode_float_var(encoded_var).length();
        logtype_dict_entry.add_float_var();

        eight_byte_encoded_variable_t eight_byte_encoded_var{};
        if constexpr (std::is_same_v<encoded_variable_t, eight_byte_encoded_variable_t>) {
            eight_byte_encoded_var = encoded_var;
        } else {  // std::is_same_v<encoded_variable_t, four_byte_encoded_variable_t>
            eight_byte_encoded_var = ffi::encode_four_byte_float_as_eight_byte(encoded_var);
        }
        encoded_vars.push_back(eight_byte_encoded_var);
    };

    auto dict_var_handler = [&](string const& dict_var) {
        raw_num_bytes += dict_var.length();

        eight_byte_encoded_variable_t encoded_var{};
        if constexpr (std::is_same_v<encoded_variable_t, eight_byte_encoded_variable_t>) {
            encoded_var = encode_var_dict_id(
                    add_dict_var(dict_var, logtype_dict_entry, var_dict, var_ids)
            );
        } else {  // std::is_same_v<encoded_variable_t, four_byte_encoded_variable_t>
            encoded_var = encode_var(dict_var, logtype_dict_entry, var_dict, var_ids);
        }
        encoded_vars.push_back(encoded_var);
    };

    ffi::ir_stream::generic_decode_message<false>(
            log_message.get_logtype(),
            log_message.get_encoded_vars(),
            log_message.get_dict_vars(),
            constant_handler,
            encoded_int_handler,
            encoded_float_handler,
            dict_var_handler
    );
}

bool EncodedVariableInterpreter::decode_variables_into_message(
        LogTypeDictionaryEntry const& logtype_dict_entry,
        VariableDictionaryReader const& var_dict,
        vector<encoded_variable_t> const& encoded_vars,
        string& decompressed_msg
) {
    // Ensure the number of variables in the logtype matches the number of encoded variables given
    auto const& logtype_value = logtype_dict_entry.get_value();
    size_t const num_vars = logtype_dict_entry.get_num_variables();
    if (num_vars != encoded_vars.size()) {
        SPDLOG_ERROR(
                "EncodedVariableInterpreter: Logtype '{}' contains {} variables, but {} were given "
                "for decoding.",
                logtype_value.c_str(),
                num_vars,
                encoded_vars.size()
        );
        return false;
    }

    VariablePlaceholder var_placeholder;
    size_t constant_begin_pos = 0;
    string float_str;
    variable_dictionary_id_t var_dict_id;
    size_t const num_placeholders_in_logtype = logtype_dict_entry.get_num_placeholders();
    for (size_t placeholder_ix = 0, var_ix = 0; placeholder_ix < num_placeholders_in_logtype;
         ++placeholder_ix)
    {
        size_t placeholder_position
                = logtype_dict_entry.get_placeholder_info(placeholder_ix, var_placeholder);

        // Add the constant that's between the last placeholder and this one
        decompressed_msg.append(
                logtype_value,
                constant_begin_pos,
                placeholder_position - constant_begin_pos
        );
        switch (var_placeholder) {
            case VariablePlaceholder::Integer:
                decompressed_msg += std::to_string(encoded_vars[var_ix++]);
                break;
            case VariablePlaceholder::Float:
                convert_encoded_float_to_string(encoded_vars[var_ix++], float_str);
                decompressed_msg += float_str;
                break;
            case VariablePlaceholder::Dictionary:
                var_dict_id = decode_var_dict_id(encoded_vars[var_ix++]);
                decompressed_msg += var_dict.get_value(var_dict_id);
                break;
            case VariablePlaceholder::Escape:
                break;
            default:
                SPDLOG_ERROR(
                        "EncodedVariableInterpreter: Logtype '{}' contains unexpected variable "
                        "placeholder 0x{:x}",
                        logtype_value,
                        enum_to_underlying_type(var_placeholder)
                );
                return false;
        }
        // Move past the variable placeholder
        constant_begin_pos = placeholder_position + 1;
    }
    // Append remainder of logtype, if any
    if (constant_begin_pos < logtype_value.length()) {
        decompressed_msg.append(logtype_value, constant_begin_pos, string::npos);
    }

    return true;
}

bool EncodedVariableInterpreter::encode_and_search_dictionary(
        string const& var_str,
        VariableDictionaryReader const& var_dict,
        bool ignore_case,
        string& logtype,
        SubQuery& sub_query
) {
    size_t length = var_str.length();
    if (0 == length) {
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__);
    }

    encoded_variable_t encoded_var;
    if (convert_string_to_representable_integer_var(var_str, encoded_var)) {
        LogTypeDictionaryEntry::add_int_var(logtype);
        sub_query.add_non_dict_var(encoded_var);
    } else if (convert_string_to_representable_float_var(var_str, encoded_var)) {
        LogTypeDictionaryEntry::add_float_var(logtype);
        sub_query.add_non_dict_var(encoded_var);
    } else {
        auto const entries = var_dict.get_entry_matching_value(var_str, ignore_case);
        if (entries.empty()) {
            // Not in dictionary
            return false;
        }

        LogTypeDictionaryEntry::add_dict_var(logtype);

        if (entries.size() == 1) {
            auto const* entry = entries.at(0);
            encoded_var = encode_var_dict_id(entry->get_id());
            sub_query.add_dict_var(encoded_var, entry);
            return true;
        }

        std::unordered_set<encoded_variable_t> encoded_vars;
        std::unordered_set<clp::VariableDictionaryEntry const*> const entries_set(
                entries.begin(),
                entries.end()
        );
        for (auto const entry : entries) {
            encoded_vars.insert(encode_var_dict_id(entry->get_id()));
        }
        sub_query.add_imprecise_dict_var(encoded_vars, entries_set);
    }

    return true;
}

bool EncodedVariableInterpreter::wildcard_search_dictionary_and_get_encoded_matches(
        std::string const& var_wildcard_str,
        VariableDictionaryReader const& var_dict,
        bool ignore_case,
        SubQuery& sub_query
) {
    // Find matches
    unordered_set<VariableDictionaryEntry const*> var_dict_entries;
    var_dict.get_entries_matching_wildcard_string(var_wildcard_str, ignore_case, var_dict_entries);
    if (var_dict_entries.empty()) {
        // Not in dictionary
        return false;
    }

    // Encode matches
    unordered_set<encoded_variable_t> encoded_vars;
    for (auto entry : var_dict_entries) {
        encoded_vars.insert(encode_var_dict_id(entry->get_id()));
    }

    sub_query.add_imprecise_dict_var(encoded_vars, var_dict_entries);

    return true;
}

encoded_variable_t EncodedVariableInterpreter::encode_var_dict_id(variable_dictionary_id_t id) {
    return bit_cast<encoded_variable_t>(id);
}

encoded_variable_t EncodedVariableInterpreter::encode_var(
        string const& var,
        LogTypeDictionaryEntry& logtype_dict_entry,
        VariableDictionaryWriter& var_dict,
        vector<variable_dictionary_id_t>& var_ids
) {
    encoded_variable_t encoded_var{0};
    if (convert_string_to_representable_integer_var(var, encoded_var)) {
        logtype_dict_entry.add_int_var();
    } else if (convert_string_to_representable_float_var(var, encoded_var)) {
        logtype_dict_entry.add_float_var();
    } else {
        // Variable string looks like a dictionary variable, so encode it as so
        encoded_var = encode_var_dict_id(add_dict_var(var, logtype_dict_entry, var_dict, var_ids));
    }
    return encoded_var;
}

variable_dictionary_id_t EncodedVariableInterpreter::add_dict_var(
        string const& var,
        LogTypeDictionaryEntry& logtype_dict_entry,
        VariableDictionaryWriter& var_dict,
        vector<variable_dictionary_id_t>& var_ids
) {
    variable_dictionary_id_t id{cVariableDictionaryIdMax};
    var_dict.add_entry(var, id);
    var_ids.push_back(id);

    logtype_dict_entry.add_dictionary_var();

    return id;
}

// Explicitly declare template specializations so that we can define the template methods in this
// file
template void
EncodedVariableInterpreter::encode_and_add_to_dictionary<eight_byte_encoded_variable_t>(
        LogEvent<eight_byte_encoded_variable_t> const& log_event,
        LogTypeDictionaryEntry& logtype_dict_entry,
        VariableDictionaryWriter& var_dict,
        std::vector<eight_byte_encoded_variable_t>& encoded_vars,
        std::vector<variable_dictionary_id_t>& var_ids,
        size_t& raw_num_bytes
);

template void
EncodedVariableInterpreter::encode_and_add_to_dictionary<four_byte_encoded_variable_t>(
        LogEvent<four_byte_encoded_variable_t> const& log_event,
        LogTypeDictionaryEntry& logtype_dict_entry,
        VariableDictionaryWriter& var_dict,
        std::vector<eight_byte_encoded_variable_t>& encoded_vars,
        std::vector<variable_dictionary_id_t>& var_ids,
        size_t& raw_num_bytes
);
}  // namespace clp
