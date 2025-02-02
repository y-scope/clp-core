#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include <Catch2/single_include/catch2/catch.hpp>
#include <string_utils/string_utils.hpp>

#include "FileReader.hpp"
#include "spdlog_with_specializations.hpp"

using clp::string_utils::clean_up_wildcard_search_string;
using clp::string_utils::convert_string_to_int;
using clp::string_utils::wildcard_match_unsafe;
using clp::string_utils::wildcard_match_unsafe_case_sensitive;
using std::chrono::high_resolution_clock;
using std::span;
using std::string;
using std::string_view;
using std::vector;

namespace {
/**
 * All possible alphabets that could appear in a wildcard string. Note that the alphabets are
 * conceptual (e.g. EscapedAsterisk) rather than concrete (e.g. "\\*").
 */
enum class WildcardStringAlphabet : uint8_t {
    Empty = 0,
    AnyChar,
    Asterisk,
    QuestionMark,
    EscapedAsterisk,
    EscapedQuestionMark,
    EscapedBackslash,
};

/**
 * Recursively generates strings that will match the given wildcard string and tests that they
 * match.
 * @param chosen_alphabets
 * @param wild
 * @param tame Returns the string generated so far.
 */
void generate_and_test_tame_str(
        span<WildcardStringAlphabet> chosen_alphabets,
        string_view wild,
        string& tame
);

/**
 * Recursively generates and tests a wildcard string using the given template. Testing requires
 * generating one or more matching strings.
 * @param template_wildcard_str
 * @param chosen_alphabets Returns the alphabets chosen so far.
 * @param wild Returns the string generated so far.
 */
void generate_and_test_wildcard_str(
        span<vector<WildcardStringAlphabet>> template_wildcard_str,
        vector<WildcardStringAlphabet>& chosen_alphabets,
        string& wild
);

// NOLINTNEXTLINE(misc-no-recursion)
void generate_and_test_tame_str(
        span<WildcardStringAlphabet> chosen_alphabets,
        string_view wild,
        string& tame
) {
    // Base case
    if (chosen_alphabets.empty()) {
        INFO("tame: \"" << tame << "\", wild: \"" << wild << "\"");
        REQUIRE(wildcard_match_unsafe_case_sensitive(tame, wild));
        return;
    }

    auto const tame_size_before_modification = tame.size();
    auto alphabet = chosen_alphabets.front();
    auto const next_chosen_alphabets = chosen_alphabets.subspan(1);
    switch (alphabet) {
        case WildcardStringAlphabet::Empty:
            generate_and_test_tame_str(next_chosen_alphabets, wild, tame);
            return;
        case WildcardStringAlphabet::AnyChar:
            tame += 'a';
            generate_and_test_tame_str(next_chosen_alphabets, wild, tame);
            break;
        case WildcardStringAlphabet::Asterisk:
            // Generate "", "a", and "ab"
            for (size_t i = 0; i < 3; ++i) {
                generate_and_test_tame_str(next_chosen_alphabets, wild, tame);

                tame += static_cast<char>('a' + i);
            }
            break;
        case WildcardStringAlphabet::QuestionMark:
            tame += 'a';
            generate_and_test_tame_str(next_chosen_alphabets, wild, tame);
            break;
        case WildcardStringAlphabet::EscapedAsterisk:
            tame += '*';
            generate_and_test_tame_str(next_chosen_alphabets, wild, tame);
            break;
        case WildcardStringAlphabet::EscapedQuestionMark:
            tame += '?';
            generate_and_test_tame_str(next_chosen_alphabets, wild, tame);
            break;
        case WildcardStringAlphabet::EscapedBackslash:
            tame += '\\';
            generate_and_test_tame_str(next_chosen_alphabets, wild, tame);
            break;
        default:
            REQUIRE(false);
    }

    tame.resize(tame_size_before_modification);
}

// NOLINTNEXTLINE(misc-no-recursion)
void generate_and_test_wildcard_str(
        span<vector<WildcardStringAlphabet>> template_wildcard_str,
        vector<WildcardStringAlphabet>& chosen_alphabets,
        string& wild
) {
    // Base case
    if (template_wildcard_str.empty()) {
        string tame;
        generate_and_test_tame_str(chosen_alphabets, wild, tame);
        return;
    }

    auto const wild_size_before_modification = wild.size();

    auto const& test_alphabet = template_wildcard_str.front();
    for (auto alphabet : test_alphabet) {
        switch (alphabet) {
            case WildcardStringAlphabet::Empty:
                break;
            case WildcardStringAlphabet::AnyChar:
                wild += 'a';
                break;
            case WildcardStringAlphabet::Asterisk:
                wild += '*';
                break;
            case WildcardStringAlphabet::QuestionMark:
                wild += '?';
                break;
            case WildcardStringAlphabet::EscapedAsterisk:
                wild += "\\*";
                break;
            case WildcardStringAlphabet::EscapedQuestionMark:
                wild += "\\?";
                break;
            case WildcardStringAlphabet::EscapedBackslash:
                wild += "\\\\";
                break;
            default:
                REQUIRE(false);
        }

        chosen_alphabets.push_back(alphabet);
        generate_and_test_wildcard_str(template_wildcard_str.subspan(1), chosen_alphabets, wild);
        chosen_alphabets.pop_back();

        wild.resize(wild_size_before_modification);
    }
}
}  // namespace

TEST_CASE("to_lower", "[to_lower]") {
    string str = "test123TEST";
    clp::string_utils::to_lower(str);
    REQUIRE(str == "test123test");
}

TEST_CASE("clean_up_wildcard_search_string", "[clean_up_wildcard_search_string]") {
    string str;

    // No wildcards
    str = "test";
    REQUIRE(clean_up_wildcard_search_string(str) == "test");

    // Only '?'
    str = "?est";
    REQUIRE(clean_up_wildcard_search_string(str) == "?est");

    // Normal case
    str = "***t**\\*s\\?t?**";
    REQUIRE(clean_up_wildcard_search_string(str) == "*t*\\*s\\?t?*");

    // Abnormal cases
    str = "***";
    REQUIRE(clean_up_wildcard_search_string(str) == "*");

    str = "*?*";
    REQUIRE(clean_up_wildcard_search_string(str) == "*?*");

    str = "?";
    REQUIRE(clean_up_wildcard_search_string(str) == "?");

    str = "?";
    REQUIRE(clean_up_wildcard_search_string(str) == "?");

    str = "a\\bc\\";
    REQUIRE(clean_up_wildcard_search_string(str) == "abc");
}

TEST_CASE("wildcard_match_unsafe_case_sensitive", "[wildcard]") {
    // We want to test all varieties of wildcard strings and strings that can be matched by them.
    // We do this by using a kind of template wildcard string---where each character has a set of
    // possibilities---to generate this variety. For each wildcard string, we also generate one or
    // more strings that can be matched by the wildcard string.

    // The template below is meant to test 1-2 groups of WildcardStringAlphabets separated by '*'.
    // The groups allow contiguous repeats of all possible alphabets except '*' since
    // `wildcard_match_unsafe_case_sensitive` doesn't accept such wildcard strings. Each alphabet in
    // the template may be empty except at least one in each group (so we don't unintentionally
    // create two contiguous '*'). Overall, this should cover all matching cases.
    vector<WildcardStringAlphabet> const nullable_asterisk_template{
            WildcardStringAlphabet::Empty,
            WildcardStringAlphabet::Asterisk,
    };
    vector<WildcardStringAlphabet> const nullable_non_asterisk_template{
            WildcardStringAlphabet::Empty,
            WildcardStringAlphabet::QuestionMark,
            WildcardStringAlphabet::EscapedAsterisk,
            WildcardStringAlphabet::EscapedQuestionMark,
            WildcardStringAlphabet::EscapedBackslash,
            WildcardStringAlphabet::AnyChar,
    };
    vector<WildcardStringAlphabet> const non_asterisk_template{
            WildcardStringAlphabet::QuestionMark,
            WildcardStringAlphabet::EscapedAsterisk,
            WildcardStringAlphabet::EscapedQuestionMark,
            WildcardStringAlphabet::EscapedBackslash,
            WildcardStringAlphabet::AnyChar,
    };
    vector<vector<WildcardStringAlphabet>> template_wildcard_str;
    for (size_t i = 0; i < 2; ++i) {
        if (0 == i) {
            template_wildcard_str.emplace_back(nullable_asterisk_template);
            template_wildcard_str.emplace_back(nullable_non_asterisk_template);
            template_wildcard_str.emplace_back(non_asterisk_template);
            template_wildcard_str.emplace_back(nullable_non_asterisk_template);
            template_wildcard_str.push_back(nullable_asterisk_template);
        } else {
            // Insert "*<non-asterisk-group>" before the last asterisk
            // NOTE: We insert in reverse since we're using the same iterator for all inserts
            auto insert_pos_it = template_wildcard_str.end() - 1;
            template_wildcard_str.insert(insert_pos_it, nullable_non_asterisk_template);
            template_wildcard_str.insert(insert_pos_it, non_asterisk_template);
            template_wildcard_str.insert(insert_pos_it, nullable_non_asterisk_template);
            template_wildcard_str.insert(
                    insert_pos_it,
                    {
                            WildcardStringAlphabet::Asterisk,
                    }
            );
        }

        vector<WildcardStringAlphabet> chosen_alphabets;
        string wild;
        generate_and_test_wildcard_str(template_wildcard_str, chosen_alphabets, wild);
    }

    // We test non-matching cases using a tame string that matches a diverse wildcard string as
    // follows. We test that every substring (anchored at index 0) of tame doesn't match the
    // complete wildcard string.
    constexpr string_view tame{"abcdef?*?ghixyz"};
    constexpr string_view wild{R"(*a?c*\?\*\?*x?z*)"};
    // Sanity-check that they match.
    REQUIRE(wildcard_match_unsafe_case_sensitive(tame, wild));
    auto tame_begin_it = tame.cbegin();
    for (auto it = tame.cend() - 1; tame_begin_it != it; --it) {
        auto const tame_substr = string_view{tame_begin_it, it};
        INFO("tame: \"" << tame_substr << "\", wild: \"" << wild << "\"");
        REQUIRE((false == wildcard_match_unsafe_case_sensitive(tame_substr, wild)));
    }
}

TEST_CASE("wildcard_match_unsafe", "[wildcard]") {
    constexpr string_view tame{"0!2#4%6&8(aBcDeFgHiJkLmNoPqRsTuVwXyZ"};
    string wild;

    wild = "0!2#4%6&8(AbCdEfGhIjKlMnOpQrStUvWxYz";
    REQUIRE(wildcard_match_unsafe(tame, wild, false));
    REQUIRE((false == wildcard_match_unsafe(tame, wild, true)));

    wild = "0?2?4?6?8?A?C?E?G?I?K?M?O?Q?S?U?W?Y?";
    REQUIRE(wildcard_match_unsafe(tame, wild, false));
    REQUIRE((false == wildcard_match_unsafe(tame, wild, true)));

    wild = "?!?#?%?&?(?b?d?f?h?j?l?n?p?r?t?v?x?z";
    REQUIRE(wildcard_match_unsafe(tame, wild, false));
    REQUIRE((false == wildcard_match_unsafe(tame, wild, true)));

    wild = "*?b?d?f?h?j?l?n?p?r?t?v?x?z*";
    REQUIRE(wildcard_match_unsafe(tame, wild, false));
    REQUIRE((false == wildcard_match_unsafe(tame, wild, true)));

    wild = "*?A?C?E?G?I?K?M?O?Q?S?U?W?Y?*";
    REQUIRE(wildcard_match_unsafe(tame, wild, false));
    REQUIRE((false == wildcard_match_unsafe(tame, wild, true)));
}

SCENARIO("wildcard_match_unsafe_case_sensitive performance", "[wildcard performance]") {
    auto const tests_dir = std::filesystem::path{__FILE__}.parent_path();
    auto const log_file_path = tests_dir / "test_network_reader_src" / "random.log";

    clp::FileReader file_reader;
    file_reader.open(log_file_path.string());
    string line;
    vector<string> lines;
    while (file_reader.read_to_delimiter('\n', false, false, line)) {
        lines.push_back(line);
    }
    file_reader.close();

    auto const begin_timestamp = high_resolution_clock::now();
    for (size_t i = 0; i < 10'000; ++i) {
        for (auto const& tame : lines) {
            wildcard_match_unsafe_case_sensitive(tame, "*to*blk_1073742594_1770*");
        }
    }
    auto const end_timestamp = high_resolution_clock::now();

    SPDLOG_INFO(
            "wildcard_match_unsafe_case_sensitive performance test took {} milliseconds.",
            std::chrono::duration_cast<std::chrono::milliseconds>(end_timestamp - begin_timestamp)
                    .count()
    );
}

TEST_CASE("convert_string_to_int", "[convert_string_to_int]") {
    int64_t raw_as_int;
    string raw;
    int64_t converted;

    // Corner cases
    // Empty string
    raw = "";
    REQUIRE(false == convert_string_to_int(raw, converted));

    // Edges of representable range
    raw_as_int = INT64_MAX;
    raw = std::to_string(raw_as_int);
    REQUIRE(convert_string_to_int(raw, converted));
    REQUIRE(raw_as_int == converted);

    raw_as_int = INT64_MIN;
    raw = std::to_string(raw_as_int);
    REQUIRE(convert_string_to_int(raw, converted));
    REQUIRE(raw_as_int == converted);

    raw = "9223372036854775808";  // INT64_MAX + 1 == 2^63
    REQUIRE(false == convert_string_to_int(raw, converted));

    raw = "-9223372036854775809";  // INT64_MIN - 1 == -2^63 - 1
    REQUIRE(false == convert_string_to_int(raw, converted));

    // Non-integers
    raw = "abc";
    REQUIRE(false == convert_string_to_int(raw, converted));

    raw = "90a";
    REQUIRE(false == convert_string_to_int(raw, converted));

    raw = "0.5";
    REQUIRE(false == convert_string_to_int(raw, converted));

    // Non-decimal integers
    raw = "0x5A";
    REQUIRE(false == convert_string_to_int(raw, converted));

    // Integers
    raw = "98340";
    REQUIRE(convert_string_to_int(raw, converted));
    REQUIRE(98'340 == converted);
}
