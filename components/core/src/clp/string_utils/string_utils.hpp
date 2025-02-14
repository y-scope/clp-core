#ifndef CLP_STRING_UTILS_STRING_UTILS_HPP
#define CLP_STRING_UTILS_STRING_UTILS_HPP

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

#include <outcome/single-header/outcome.hpp>

namespace clp::string_utils {
/**
 * Checks if the given character is an alphabet
 * @param c
 * @return true if c is an alphabet, false otherwise
 */
inline bool is_alphabet(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

/**
 * Checks if character is a decimal (base-10) digit
 * @param c
 * @return true if c is a decimal digit, false otherwise
 */
inline bool is_decimal_digit(char c) {
    return '0' <= c && c <= '9';
}

/**
 * Searches haystack starting at the given position for one of the given needles
 * @param haystack
 * @param needles
 * @param search_start_pos
 * @param needle_ix The index of the needle found
 * @return The position of the match or string::npos if none
 */
size_t find_first_of(
        std::string const& haystack,
        char const* needles,
        size_t search_start_pos,
        size_t& needle_ix
);

/**
 * Replaces the given characters in the given value with the given replacements
 * @param characters_to_escape
 * @param replacement_characters
 * @param value
 * @param escape Whether to precede the replacement with a '\' (e.g., so that a
 * line-feed character is output as "\n")
 * @return The string with replacements
 */
std::string replace_characters(
        char const* characters_to_escape,
        char const* replacement_characters,
        std::string const& value,
        bool escape
);

/**
 * Converts a string to lowercase
 * @param str
 */
void to_lower(std::string& str);

/**
 * Cleans wildcard search string
 * <ul>
 *   <li>Removes consecutive '*'</li>
 *   <li>Removes escaping from non-wildcard characters</li>
 *   <li>Removes dangling escape character from the end of the string</li>
 * </ul>
 * @param str Wildcard search string to clean
 * @return Cleaned wildcard search string
 */
std::string clean_up_wildcard_search_string(std::string_view str);

/**
 * Checks if character is a wildcard
 * @param c
 * @return true if c is a wildcard, false otherwise
 */
bool is_wildcard(char c);

/**
 * Same as ``wildcard_match_unsafe_case_sensitive`` except this method allows
 * the caller to specify whether the match should be case sensitive.
 *
 * @param tame The literal string
 * @param wild The wildcard string
 * @param case_sensitive_match Whether to consider case when matching
 * @return Whether the two strings match
 */
bool wildcard_match_unsafe(
        std::string_view tame,
        std::string_view wild,
        bool case_sensitive_match = true
);
/**
 * Checks if a string matches a wildcard string. Two wildcards are currently
 * supported: '*' to match 0 or more characters, and '?' to match any single
 * character. Each can be escaped using a preceding '\'. Other characters which
 * are escaped are treated as normal characters.
 * <br/>
 * This method is optimized for performance by omitting some checks on the
 * wildcard string that are unnecessary if the caller cleans up the wildcard
 * string as follows:
 * <ul>
 *   <li>The wildcard string should not contain consecutive '*'.</li>
 *   <li>The wildcard string should not contain an escape character without a
 *   character following it.</li>
 * </ul>
 *
 * @param tame The literal string
 * @param wild The wildcard string
 * @return Whether the two strings match
 */
bool wildcard_match_unsafe_case_sensitive(std::string_view tame, std::string_view wild);

/**
 * Converts the given string to a 64-bit integer if possible
 * @tparam integer_t
 * @param raw
 * @param converted
 * @return true if the conversion was successful, false otherwise
 */
template <typename integer_t>
bool convert_string_to_int(std::string_view raw, integer_t& converted);

template <typename integer_t>
bool convert_string_to_int(std::string_view raw, integer_t& converted) {
#if defined(_MSC_VER)
    auto* raw_begin = raw.data();
    auto* raw_end = raw_begin + raw.size();
#else
    auto raw_begin = raw.cbegin();
    auto raw_end = raw.cend();
#endif
    auto result = std::from_chars(raw_begin, raw_end, converted);
    if (raw_end != result.ptr) {
        return false;
    } else {
        return result.ec == std::errc();
    }
}

/**
 * This class implements a validating UTF-8 parser which takes as input valid or nearly-valid UTF-8
 * and outputs valid UTF-8, handling invalid UTF-8 characters according to some policy.
 */
class ValidatingUtf8Parser {
public:
    enum class InvalidUtf8Policy : uint8_t {
        SubstituteReplacementCharacter,
        ReturnError
    };

    /**
     * Validates a UTF-8 input string and returns a valid UTF-8 output string or an error.
     *
     * The returned string_view is guaranteed to be valid until either the next call to `validate`
     * or until the input string_view passed to this function becomes invalid, whichever is sooner.
     *
     * @param raw
     * @param policy The error handling policy to apply when encountering invalid UTF-8 characters.
     * @return A result containing a valid UTF-8 string or an error code indicating the failure:
     * - std::errc::illegal_byte_sequence if the input contains invalid UTF-8 and the policy is
     *   ReturnError.
     * - std::errc::invalid_argument if the configured policy is unknown.
     */
    auto validate(std::string_view raw, InvalidUtf8Policy policy)
            -> OUTCOME_V2_NAMESPACE::std_result<std::string_view>;

    /**
     * Validates a UTF-8 input string and returns a valid UTF-8 output string or an error.
     *
     * The returned string_view is guaranteed to be valid until either the next call to `validate`
     * or until the input string_view passed to this function becomes invalid, whichever is sooner.
     *
     * @param raw
     * @tparam policy The error handling policy to apply when encountering invalid UTF-8 characters.
     * @return A result containing a valid UTF-8 string or an error code indicating the failure:
     * - std::errc::illegal_byte_sequence if the input contains invalid UTF-8 and the policy is
     *   ReturnError.
     * - std::errc::invalid_argument if the configured policy is unknown.
     */
    template <InvalidUtf8Policy policy>
    auto validate(std::string_view raw) -> OUTCOME_V2_NAMESPACE::std_result<std::string_view>;

private:
    std::string m_buffer;
};
}  // namespace clp::string_utils

#endif  // CLP_STRING_UTILS_STRING_UTILS_HPP
