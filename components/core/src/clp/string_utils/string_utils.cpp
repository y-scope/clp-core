#include "string_utils/string_utils.hpp"

#include <utfcpp/source/utf8.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <iterator>
#include <string_view>
#include <system_error>

#include <simdjson.h>

using std::string;
using std::string_view;

namespace {
/**
 * Helper for ``wildcard_match_unsafe_case_sensitive`` to advance the pointer in
 * tame to the next character which matches wild. This method should be inlined
 * for performance.
 * @param tame_current
 * @param tame_bookmark
 * @param tame_end
 * @param wild_current
 * @param wild_bookmark
 * @return true on success, false if wild cannot match tame
 */
inline bool advance_tame_to_next_match(
        char const*& tame_current,
        char const*& tame_bookmark,
        char const* tame_end,
        char const*& wild_current
);

inline bool advance_tame_to_next_match(
        char const*& tame_current,
        char const*& tame_bookmark,
        char const* tame_end,
        char const*& wild_current
) {
    auto w = *wild_current;
    if ('?' != w) {
        // No need to check for '*' since the caller ensures wild doesn't
        // contain consecutive '*'

        // Handle escaped characters
        if ('\\' == w) {
            ++wild_current;
            // This is safe without a bounds check since this the caller ensures
            // there are no dangling escape characters
            w = *wild_current;
        }

        // Advance tame_current until it matches wild_current
        while (true) {
            if (tame_end == tame_current) {
                // Wild group is longer than last group in tame, so can't match
                // e.g. "*abc" doesn't match "zab"
                return false;
            }
            auto t = *tame_current;
            if (t == w) {
                break;
            }
            ++tame_current;
        }
    }

    tame_bookmark = tame_current;

    return true;
}
}  // namespace

namespace clp::string_utils {
size_t find_first_of(
        string const& haystack,
        char const* needles,
        size_t search_start_pos,
        size_t& needle_ix
) {
    size_t haystack_length = haystack.length();
    size_t needles_length = strlen(needles);
    for (size_t i = search_start_pos; i < haystack_length; ++i) {
        for (needle_ix = 0; needle_ix < needles_length; ++needle_ix) {
            if (haystack[i] == needles[needle_ix]) {
                return i;
            }
        }
    }

    return string::npos;
}

string replace_characters(
        char const* characters_to_replace,
        char const* replacement_characters,
        string const& value,
        bool escape
) {
    string new_value;
    size_t search_start_pos = 0;
    while (true) {
        size_t replace_char_ix;
        size_t char_to_replace_pos
                = find_first_of(value, characters_to_replace, search_start_pos, replace_char_ix);
        if (string::npos == char_to_replace_pos) {
            new_value.append(value, search_start_pos, string::npos);
            break;
        } else {
            new_value.append(value, search_start_pos, char_to_replace_pos - search_start_pos);
            if (escape) {
                new_value += "\\";
            }
            new_value += replacement_characters[replace_char_ix];
            search_start_pos = char_to_replace_pos + 1;
        }
    }
    return new_value;
}

void to_lower(string& str) {
    std::transform(str.cbegin(), str.cend(), str.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
}

bool is_wildcard(char c) {
    static constexpr char cWildcards[] = "?*";
    for (size_t i = 0; i < strlen(cWildcards); ++i) {
        if (cWildcards[i] == c) {
            return true;
        }
    }
    return false;
}

string clean_up_wildcard_search_string(string_view str) {
    string cleaned_str;

    bool is_escaped = false;
    auto str_end = str.cend();
    for (auto current = str.cbegin(); current != str_end;) {
        auto c = *current;
        if (is_escaped) {
            is_escaped = false;

            if (is_wildcard(c) || '\\' == c) {
                // Keep escaping if c is a wildcard character or an escape
                // character
                cleaned_str += '\\';
            }
            cleaned_str += c;
            ++current;
        } else if ('*' == c) {
            cleaned_str += c;

            // Skip over all '*' to find the next non-'*'
            do {
                ++current;
            } while (current != str_end && '*' == *current);
        } else {
            if ('\\' == c) {
                is_escaped = true;
            } else {
                cleaned_str += c;
            }
            ++current;
        }
    }

    return cleaned_str;
}

bool wildcard_match_unsafe(string_view tame, string_view wild, bool case_sensitive_match) {
    if (case_sensitive_match) {
        return wildcard_match_unsafe_case_sensitive(tame, wild);
    } else {
        // We convert to lowercase (rather than uppercase) anticipating that
        // callers use lowercase more frequently, so little will need to change.
        string lowercase_tame(tame);
        to_lower(lowercase_tame);
        string lowercase_wild(wild);
        to_lower(lowercase_wild);
        return wildcard_match_unsafe_case_sensitive(lowercase_tame, lowercase_wild);
    }
}

/**
 * The algorithm basically works as follows:
 * Given a wild string "*abc*def*ghi*", it can be broken into groups of
 * characters delimited by one or more '*' characters. The goal of the algorithm
 * is then to determine whether the tame string contains each of those groups in
 * the same order.
 *
 * Thus, the algorithm:
 * 1. searches for the start of one of these groups in wild,
 * 2. searches for a group in tame starting with the same character, and then
 * 3. checks if the two match. If not, the search repeats with the next group in
 *    tame.
 */
bool wildcard_match_unsafe_case_sensitive(string_view tame, string_view wild) {
    auto const tame_length = tame.length();
    auto const wild_length = wild.length();
    char const* tame_current = tame.data();
    char const* wild_current = wild.data();
    char const* tame_bookmark = nullptr;
    char const* wild_bookmark = nullptr;
    char const* tame_end = tame_current + tame_length;
    char const* wild_end = wild_current + wild_length;

    // Handle wild or tame being empty
    if (0 == wild_length) {
        return 0 == tame_length;
    } else {
        if (0 == tame_length) {
            return "*" == wild;
        }
    }

    char w;
    char t;
    bool is_escaped = false;
    while (true) {
        w = *wild_current;
        if ('*' == w) {
            ++wild_current;
            if (wild_end == wild_current) {
                // Trailing '*' means everything remaining in tame will match
                return true;
            }

            // Set wild and tame bookmarks
            wild_bookmark = wild_current;
            if (false
                == advance_tame_to_next_match(tame_current, tame_bookmark, tame_end, wild_current))
            {
                return false;
            }
        } else {
            // Handle escaped characters
            if ('\\' == w) {
                is_escaped = true;
                ++wild_current;
                // This is safe without a bounds check since this the caller
                // ensures there are no dangling escape characters
                w = *wild_current;
            }

            // Handle a mismatch
            t = *tame_current;
            if (!((false == is_escaped && '?' == w) || t == w)) {
                if (nullptr == wild_bookmark) {
                    // No bookmark to return to
                    return false;
                }

                wild_current = wild_bookmark;
                tame_current = tame_bookmark + 1;
                if (false
                    == advance_tame_to_next_match(
                            tame_current,
                            tame_bookmark,
                            tame_end,
                            wild_current
                    ))
                {
                    return false;
                }
            }
        }

        ++tame_current;
        ++wild_current;

        // Handle reaching the end of tame or wild
        if (tame_end == tame_current) {
            return (wild_end == wild_current
                    || ('*' == *wild_current && (wild_current + 1) == wild_end));
        } else {
            if (wild_end == wild_current) {
                if (nullptr == wild_bookmark) {
                    // No bookmark to return to
                    return false;
                } else {
                    wild_current = wild_bookmark;
                    tame_current = tame_bookmark + 1;
                    if (false
                        == advance_tame_to_next_match(
                                tame_current,
                                tame_bookmark,
                                tame_end,
                                wild_current
                        ))
                    {
                        return false;
                    }
                }
            }
        }
    }
}

auto ValidatingUtf8Parser::validate(std::string_view raw, InvalidUtf8Policy policy)
        -> OUTCOME_V2_NAMESPACE::std_result<std::string_view> {
    if (simdjson::validate_utf8(raw.data(), raw.size())) {
        return raw;
    }
    switch (policy) {
        case InvalidUtf8Policy::SubstituteReplacementCharacter:
            m_buffer.clear();
            utf8::replace_invalid(raw.begin(), raw.end(), std::back_inserter(m_buffer));
            return m_buffer;
        case InvalidUtf8Policy::ReturnError:
            return std::errc::illegal_byte_sequence;
        default:
            return std::errc::invalid_argument;
    }
}

template <>
auto ValidatingUtf8Parser::validate<ValidatingUtf8Parser::InvalidUtf8Policy::ReturnError>(
        std::string_view raw
) -> OUTCOME_V2_NAMESPACE::std_result<std::string_view> {
    if (simdjson::validate_utf8(raw.data(), raw.size())) {
        return raw;
    }
    return std::errc::illegal_byte_sequence;
}

template <>
auto ValidatingUtf8Parser::validate<
        ValidatingUtf8Parser::InvalidUtf8Policy::SubstituteReplacementCharacter>(
        std::string_view raw
) -> OUTCOME_V2_NAMESPACE::std_result<std::string_view> {
    if (simdjson::validate_utf8(raw.data(), raw.size())) {
        return raw;
    }
    m_buffer.clear();
    utf8::replace_invalid(raw.begin(), raw.end(), std::back_inserter(m_buffer));
    return m_buffer;
}
}  // namespace clp::string_utils
