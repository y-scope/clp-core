#include "string_utils/string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

using std::string;
using std::string_view;

namespace clp::string_utils {
namespace {
/**
 * Helper for `wildcard_match_unsafe_case_sensitive` to advance `tame`'s iterator to the next
 * character that matches the current character in `wild`, and to bookmark this character. If the
 * current character in `wild` is escaped, `wild`'s iterator will also be advanced.
 *
 * NOTE:
 * - This method expects that `tame_it` < `tame_end_it`
 * - This method should be inlined for performance.
 *
 * @param tame_end_it
 * @param tame_it Returns `tame`'s updated iterator.
 * @param tame_bookmark_it Returns `tame`'s updated bookmark.
 * @param wild_it Returns `wild`'s updated iterator.
 * @return Whether `tame` might be able to match `wild`.
 */
inline bool advance_tame_to_next_match(
        string_view::const_iterator tame_end_it,
        string_view::const_iterator& tame_it,
        string_view::const_iterator& tame_bookmark_it,
        string_view::const_iterator& wild_it
);

/**
 * Helper for `wildcard_match_unsafe_case_sensitive` to determine if the given iterator points to
 * the end of `wild`, or the second-last character of `wild` if`wild` ends with a '*'.
 * @param wild_it
 * @param wild_end_it
 * @return Whether the match has reached the end of `tame` and `wild`.
 */
bool is_end_of_wild(string_view::const_iterator wild_it, string_view::const_iterator wild_end_it);

inline bool advance_tame_to_next_match(
        string_view::const_iterator tame_end_it,
        string_view::const_iterator& tame_it,
        string_view::const_iterator& tame_bookmark_it,
        string_view::const_iterator& wild_it
) {
    auto w = *wild_it;
    if ('?' != w) {
        // No need to check for '*' since the caller ensures `wild` doesn't contain consecutive '*'

        // Handle escaped characters
        if ('\\' == w) {
            // Safe without a bounds check
            ++wild_it;
            w = *wild_it;
        }

        // Advance `tame_it` until it matches `w`
        while (true) {
            if (*tame_it == w) {
                break;
            }
            ++tame_it;
            if (tame_end_it == tame_it) {
                return false;
            }
        }
    }

    tame_bookmark_it = tame_it;

    return true;
}

bool is_end_of_wild(string_view::const_iterator wild_it, string_view::const_iterator wild_end_it) {
    return (wild_end_it == wild_it) || (wild_end_it == wild_it + 1 && '*' == *wild_it);
}
}  // namespace

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
 * Given a wildcard string (a.k.a. "wild") like "abc*def*ghi*", it can be broken into groups of
 * characters delimited by one or more '*' characters. The goal of the algorithm is then to
 * determine whether the "tame" string contains each of those groups in the same order.
 *
 * Matching a group in `wild` against `tame` requires iteratively matching each character in `tame`
 * against each character in the group, with the exception of the '?' wildcard and escaped
 * characters ('*', '?', or '\'). When a mismatch occurs, there are two possibilities:
 *
 * 1. The mismatch occurs before the first '*' in `wild`, meaning that the entire wildcard match
 *    fails.
 * 2. The mismatch occurs after a '*' in `wild`. This case requires additional handling explained
 *    below.
 *
 * Consider `tame` = "ccd", `wild` = "*cd". When we start matching `tame` against the first group
 * in `wild`, the first 'c' will match, but the second 'c' won't match 'd'. In this case, we should
 * restart the matching process from the second 'c'.
 *
 * To generalize this, we need to maintain bookmarks for both `tame` and `wild`. Whenever we have a
 * mismatch, we should reset `wild` to its bookmark and `tame` to its bookmark + 1, and then try
 * the match again. If we get to the end of `tame` without getting to the end of the group in
 * `wild`, the entire wildcard match fails.
 *
 * NOTE:
 * - This method is on the critical path for searches in clg/clp-s/glt, so any modifications must be
 *   benchmarked to ensure performance is not significantly affected.
 * - Since the caller guarantees that there are no consecutive '*', we don't need to handle the
 *   case where a group in `wild` is empty.
 * - Since the caller guarantees that every '\' is followed by a character, we can advance passed
 *   '\' without doing a subsequent bounds check.
 * - The second part of this method could be rewritten in the following form:
 *
 *   ```
 *   while(true) {
 *     if (false == advance_tame_to_next_match(...)) return false;
 *
 *     while (true) {
 *       // Advance iterators
 *       // If we reached the end of `tame` before the end of `wild`, break
 *       // If we see a '*' in `wild`, break
 *       // If we see a mismatch, break
 *     }
 *   }
 *   ```
 *
 *   However, this form is ~2% slower.
 */
bool wildcard_match_unsafe_case_sensitive(string_view tame, string_view wild) {
    // Handle `tame` or `wild` being empty
    if (wild.empty()) {
        return tame.empty();
    }
    if (tame.empty()) {
        return "*" == wild;
    }

    auto tame_it = tame.cbegin();
    auto wild_it = wild.cbegin();
    auto const tame_end_it = tame.cend();
    auto const wild_end_it = wild.cend();
    string_view::const_iterator tame_bookmark_it{};
    string_view::const_iterator wild_bookmark_it{};

    // Match `tame` against `wild` against until we reach the first '*' in `wild` or they no longer
    // match
    while (true) {
        auto w = *wild_it;
        if ('*' == w) {
            break;
        }
        if ('?' != w) {
            // Handle escaped characters
            if ('\\' == w) {
                // Safe without a bounds check
                ++wild_it;
                w = *wild_it;
            }

            // Handle a mismatch
            if (w != *tame_it) {
                return false;
            }
        }

        ++tame_it;
        ++wild_it;

        // Handle boundary conditions
        // NOTE: The bodies of these if-blocks depend on the order of these conditions.
        if (tame_end_it == tame_it) {
            return is_end_of_wild(wild_it, wild_end_it);
        }
        if (wild_end_it == wild_it) {
            return false;
        }
    }

    // Find a match in `tame` for every group of characters between '*' in `wild`
    while (true) {
        auto w = *wild_it;
        if ('*' == w) {
            ++wild_it;
            if (wild_end_it == wild_it) {
                // `wild` ending with '*' means that it'll match the rest of `tame`
                return true;
            }

            // Set `tame` and `wild` bookmarks
            wild_bookmark_it = wild_it;
            if (false
                == advance_tame_to_next_match(tame_end_it, tame_it, tame_bookmark_it, wild_it))
            {
                return false;
            }
        } else if ('?' != w) {
            // Handle escaped characters
            if ('\\' == w) {
                // Safe without a bounds check
                ++wild_it;
                w = *wild_it;
            }

            // Handle a mismatch
            if (w != *tame_it) {
                // Reset to bookmarks
                tame_it = tame_bookmark_it + 1;
                if (tame_end_it == tame_it) {
                    return false;
                }
                wild_it = wild_bookmark_it;
                if (false
                    == advance_tame_to_next_match(tame_end_it, tame_it, tame_bookmark_it, wild_it))
                {
                    return false;
                }
            }
        }

        ++tame_it;
        ++wild_it;

        // Handle boundary conditions
        // NOTE: The bodies of these if-blocks depend on the order of these conditions.
        if (tame_end_it == tame_it) {
            return is_end_of_wild(wild_it, wild_end_it);
        }
        if (wild_end_it == wild_it) {
            // Reset to bookmarks
            tame_it = tame_bookmark_it + 1;
            wild_it = wild_bookmark_it;
            if (false
                == advance_tame_to_next_match(tame_end_it, tame_it, tame_bookmark_it, wild_it))
            {
                return false;
            }
        }
    }
}
}  // namespace clp::string_utils
