#ifndef IR_PARSING_HPP
#define IR_PARSING_HPP

/**
 * TODO Technically, the methods in this file are more general than for their
 * use in generating CLP's IR. However, introducing a parsing namespace in the
 * root source directory would be confusing since we also have the
 * compressor_frontend namespace. Once most of compressor_frontend is moved into
 * https://github.com/y-scope/log-surgeon, we should reconsider the placement of
 * the methods in this file.
 */

#include <string_view>
#include <vector>

namespace ir {
enum class VariablePlaceholder : char {
    Integer = 0x11,
    Dictionary = 0x12,
    Float = 0x13,
    Escape = 0x5c,
};

constexpr char cVariablePlaceholderEscapeCharacter = '\\';

/**
 * Checks if the given character is a delimiter
 * We treat everything *except* the following quoted characters as a
 * delimiter: "+-.0-9A-Z\_a-z"
 * @param c
 * @return Whether c is a delimiter
 */
bool is_delim(signed char c);

/**
 * @param c
 * @return Whether the character is a variable placeholder
 */
bool is_variable_placeholder(char c);

/**
 * NOTE: This method is marked inline for a 1-2% performance improvement
 * @param str
 * @return Whether the given string could be a multi-digit hex value
 */
inline bool could_be_multi_digit_hex_value(std::string_view str) {
    if (str.length() < 2) {
        return false;
    }

    // NOTE: This is 1-2% faster than using std::all_of with the opposite
    // condition
    for (auto c : str) {
        if (false == (('a' <= c && c <= 'f') || ('A' <= c && c <= 'F') || ('0' <= c && c <= '9'))) {
            return false;
        }
    }

    return true;
}

/**
 * @param value
 * @return Whether the given value is a variable according to the schemas
 * specified in ffi::get_bounds_of_next_var
 */
bool is_var(std::string_view value);

/**
 * Gets the bounds of the next variable in the given string
 * A variable is a token (word between two delimiters) that matches one of
 * these schemas:
 * - ".*[0-9].*"
 * - "=(.*[a-zA-Z].*)" (the variable is within the capturing group)
 * - "[a-fA-F0-9]{2,}"
 * @param str String to search within
 * @param begin_pos Begin position of last variable, changes to begin
 * position of next variable
 * @param end_pos End position of last variable, changes to end position of
 * next variable
 * @return true if a variable was found, false otherwise
 */
bool get_bounds_of_next_var(std::string_view str, size_t& begin_pos, size_t& end_pos);

/**
 * Appends the given constant to the logtype, escaping any variable placeholders
 * and track the position of escape characters appended.
 * @param constant
 * @param logtype
 * @param escape_placeholder_positions The vector to append the positions of the
 * added escape placeholders
 * @return Total number of escape placeholders added
 */
[[maybe_unused]] size_t escape_and_append_constant_to_logtype_with_tracking(
        std::string_view constant,
        std::string& logtype,
        std::vector<size_t>& escape_placeholder_positions
);

/**
 * Appends the given constant to the logtype, escaping any variable placeholders
 * @tparam double_escape Whether to escape the variable placeholders twice. This
 * should be set to true when building a logtype for wildcard search.
 * @param constant
 * @param logtype
 */
template <bool double_escape = false>
void escape_and_append_constant_to_logtype(std::string_view constant, std::string& logtype);

/**
 * Appends the given constant to the logtype, escaping any variable placeholders
 * by using the escape handler.
 * @tparam double_escape Whether to escape the variable placeholders twice.
 * @tparam EscapeHandler Method to append and track escape chars when escaping
 * variable placeholders. Signature: (std::string& logtype)
 * @param constant
 * @param logtype
 */
template <bool double_escape = false, typename EscapeHandler>
void append_constant_to_logtype(
        std::string_view constant,
        std::string& logtype,
        EscapeHandler escape_handler
);
}  // namespace ir

#include "parsing.inc"
#endif  // IR_PARSING_HPP
