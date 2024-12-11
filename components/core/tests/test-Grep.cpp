#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

#include <Catch2/single_include/catch2/catch.hpp>
#include <fmt/core.h>
#include <log_surgeon/Lexer.hpp>
#include <log_surgeon/SchemaParser.hpp>

#include "../src/clp/Grep.hpp"
#include "../src/clp/ir/types.hpp"
#include "../src/clp/QueryInterpretation.hpp"
#include "../src/clp/type_utils.hpp"
#include "log_surgeon/LogParser.hpp"

using clp::enum_to_underlying_type;
using clp::Grep;
using clp::ir::VariablePlaceholder;
using clp::load_lexer_from_file;
using clp::QueryInterpretation;
using clp::WildcardExpression;
using clp::WildcardExpressionView;
using fmt::format;
using fmt::join;
using fmt::make_format_args;
using fmt::vformat;
using log_surgeon::DelimiterStringAST;
using log_surgeon::lexers::ByteLexer;
using log_surgeon::ParserAST;
using log_surgeon::SchemaAST;
using log_surgeon::SchemaParser;
using log_surgeon::SchemaVarAST;
using std::apply;
using std::back_inserter;
using std::forward;
using std::index_sequence;
using std::make_index_sequence;
using std::ostream;
using std::ranges::transform;
using std::set;
using std::size_t;
using std::string;
using std::string_view;
using std::unordered_map;
using std::vector;

auto operator<<(ostream& os, unordered_map<uint32_t, string> const& map) -> ostream& {
    os << "{ ";
    for (auto const& [key, value] : map) {
        os << "{" << key << ": " << value << "} ";
    }
    os << "}";
    return os;
}

class ExpectedInterpretation {
public:
    explicit ExpectedInterpretation(ByteLexer& lexer) : lexer(lexer) {}

    // Handles the case where `force_add_to_dictionary_list` is empty
    static auto get_placeholder(string const& variable_type_name) -> char {
        if (variable_type_name == "int") {
            return enum_to_underlying_type(VariablePlaceholder::Integer);
        }
        if (variable_type_name == "float") {
            return enum_to_underlying_type(VariablePlaceholder::Float);
        }
        return enum_to_underlying_type(VariablePlaceholder::Dictionary);
    }

    static auto
    get_placeholder(string const& variable_type_name, bool const force_add_to_dictionary) -> char {
        if (force_add_to_dictionary) {
            return enum_to_underlying_type(VariablePlaceholder::Dictionary);
        }
        return get_placeholder(variable_type_name);
    }

    // Handles the case where there are no variable types because we can't call `get_placeholder`.
    auto add_string(
            string const& logtype,
            string const& has_wildcard,
            string const& is_encoded_with_wildcard,
            string const& logtype_string
    ) -> void {
        expected_strings.insert(
                format("logtype='{}', has_wildcard='{}', is_encoded_with_wildcard='{}', "
                       "logtype_string='{}'",
                       logtype,
                       has_wildcard,
                       is_encoded_with_wildcard,
                       logtype_string)
        );
    }

    // TODO: Fix this so you can omit force_add_to_dictionary_list for multiple variable types.
    template <typename... VariableTypeNames, typename... ForceAddToDictionaryList>
    auto add_string(
            string const& logtype,
            string const& has_wildcard,
            string const& is_encoded_with_wildcard,
            string const& logtype_string,
            VariableTypeNames... variable_type_names,
            ForceAddToDictionaryList... force_add_to_dictionary_list
    ) -> void {
        auto formatted_logtype
                = vformat(logtype, make_format_args(lexer.m_symbol_id[variable_type_names]...));
        string formatted_logtype_string;
        if constexpr (0 == sizeof...(force_add_to_dictionary_list)) {
            formatted_logtype_string = vformat(
                    logtype_string,
                    make_format_args((get_placeholder(variable_type_names), ...))
            );
        } else {
            formatted_logtype_string = vformat(
                    logtype_string,
                    make_format_args(get_placeholder(
                            variable_type_names,
                            force_add_to_dictionary_list

                    )...)
            );
        }
        add_string(
                formatted_logtype,
                has_wildcard,
                is_encoded_with_wildcard,
                formatted_logtype_string
        );
    }

    auto compare(string const& search_query_string) -> void {
        WildcardExpression search_query(search_query_string);
        set<QueryInterpretation> const& query_interpretations
                = Grep::generate_query_substring_interpretations(search_query, lexer);
        std::set<std::string> actual_strings;
        for (auto const& query_logtype : query_interpretations) {
            std::ostringstream oss;
            oss << query_logtype;
            actual_strings.insert(oss.str());
        }

        // Compare element by element.
        std::ostringstream oss;
        oss << lexer.m_id_symbol;
        CAPTURE(oss.str());
        CAPTURE(actual_strings);
        CAPTURE(expected_strings);

        while (false == actual_strings.empty() && false == expected_strings.empty()) {
            auto it_actual = actual_strings.begin();
            auto it_expected = expected_strings.begin();
            REQUIRE(*it_actual == *it_expected);

            actual_strings.erase(it_actual);
            expected_strings.erase(it_expected);
        }

        // Make sure all the elements of both sets were used
        REQUIRE(actual_strings == expected_strings);
    }

private:
    set<std::string> expected_strings;
    ByteLexer& lexer;
};

TEST_CASE("get_bounds_of_next_potential_var", "[get_bounds_of_next_potential_var]") {
    string str;
    size_t begin_pos;
    size_t end_pos;
    bool is_var;

    // m_end_pos past the end of the string
    str = "";
    begin_pos = string::npos;
    end_pos = string::npos;
    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == false);

    // Empty string
    str = "";
    begin_pos = 0;
    end_pos = 0;
    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == false);

    // No tokens
    str = "=";
    begin_pos = 0;
    end_pos = 0;
    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == false);

    // No wildcards
    str = " MAC address 95: ad ff 95 24 0d ff =-abc- ";
    begin_pos = 0;
    end_pos = 0;

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE("95" == str.substr(begin_pos, end_pos - begin_pos));
    REQUIRE(true == is_var);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE("ad" == str.substr(begin_pos, end_pos - begin_pos));
    REQUIRE(true == is_var);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE("ff" == str.substr(begin_pos, end_pos - begin_pos));
    REQUIRE(true == is_var);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE("95" == str.substr(begin_pos, end_pos - begin_pos));
    REQUIRE(true == is_var);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE("24" == str.substr(begin_pos, end_pos - begin_pos));
    REQUIRE(true == is_var);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE("0d" == str.substr(begin_pos, end_pos - begin_pos));
    REQUIRE(true == is_var);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE("ff" == str.substr(begin_pos, end_pos - begin_pos));
    REQUIRE(true == is_var);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE("-abc-" == str.substr(begin_pos, end_pos - begin_pos));
    REQUIRE(true == is_var);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == false);
    REQUIRE(str.length() == begin_pos);

    // With wildcards
    str = "~=1\\*x\\?!abc*123;1.2%x:+394/-=-*abc-";
    begin_pos = 0;
    end_pos = 0;

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE(str.substr(begin_pos, end_pos - begin_pos) == "1");
    REQUIRE(is_var == true);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE(str.substr(begin_pos, end_pos - begin_pos) == "abc*123");
    REQUIRE(is_var == true);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE(str.substr(begin_pos, end_pos - begin_pos) == "1.2");
    REQUIRE(is_var == true);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE(str.substr(begin_pos, end_pos - begin_pos) == "+394");
    REQUIRE(is_var == true);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == true);
    REQUIRE(str.substr(begin_pos, end_pos - begin_pos) == "-*abc-");
    REQUIRE(is_var == false);

    REQUIRE(Grep::get_bounds_of_next_potential_var(str, begin_pos, end_pos, is_var) == false);
}

TEST_CASE("SearchString", "[SearchString][schema_search]") {
    ByteLexer lexer;
    load_lexer_from_file("../tests/test_schema_files/search_schema.txt", false, lexer);

    WildcardExpression const search_string("* test\\* *");
    REQUIRE(search_string.substr(0, search_string.length()) == "* test\\* *");
    for (uint32_t idx = 0; idx < search_string.length(); idx++) {
        CAPTURE(idx);
        if (idx == 6) {
            REQUIRE(search_string.char_is_escape(idx));
        } else {
            REQUIRE(false == search_string.char_is_escape(idx));
        }
    }

    SECTION("surrounded_by_delims_or_wildcards and starts_or_ends_with_greedy_wildcard") {
        auto search_string_view1 = WildcardExpressionView{search_string, 0, search_string.length()};
        REQUIRE(search_string_view1.surrounded_by_delims_or_wildcards(lexer));
        REQUIRE(search_string_view1.starts_or_ends_with_greedy_wildcard());
        auto search_string_view2 = WildcardExpressionView{search_string, 1, search_string.length()};
        REQUIRE(search_string_view2.surrounded_by_delims_or_wildcards(lexer));
        REQUIRE(search_string_view2.starts_or_ends_with_greedy_wildcard());
        auto search_string_view3
                = WildcardExpressionView{search_string, 0, search_string.length() - 1};
        REQUIRE(search_string_view3.surrounded_by_delims_or_wildcards(lexer));
        REQUIRE(search_string_view3.starts_or_ends_with_greedy_wildcard());
        auto search_string_view4
                = WildcardExpressionView{search_string, 2, search_string.length() - 2};
        REQUIRE(search_string_view4.surrounded_by_delims_or_wildcards(lexer));
        REQUIRE(false == search_string_view4.starts_or_ends_with_greedy_wildcard());
        auto search_string_view5
                = WildcardExpressionView{search_string, 3, search_string.length() - 3};
        REQUIRE(false == search_string_view5.surrounded_by_delims_or_wildcards(lexer));
        REQUIRE(false == search_string_view5.starts_or_ends_with_greedy_wildcard());
        auto search_string_view6
                = WildcardExpressionView{search_string, 1, search_string.length() - 1};
        REQUIRE(search_string_view6.surrounded_by_delims_or_wildcards(lexer));
        REQUIRE(false == search_string_view6.starts_or_ends_with_greedy_wildcard());
    }

    SECTION("extend_to_adjacent_greedy_wildcards") {
        auto search_string_view
                = WildcardExpressionView{search_string, 1, search_string.length() - 1};
        REQUIRE(8 == search_string_view.length());
        auto extended_search_string_view = search_string_view.extend_to_adjacent_greedy_wildcards();
        REQUIRE(extended_search_string_view.surrounded_by_delims_or_wildcards(lexer));
        REQUIRE(10 == extended_search_string_view.length());
        REQUIRE(extended_search_string_view.get_value() == "* test\\* *");

        auto search_string_view2
                = WildcardExpressionView{search_string, 2, search_string.length() - 2};
        REQUIRE(6 == search_string_view2.length());
        auto extended_search_string_view2
                = search_string_view2.extend_to_adjacent_greedy_wildcards();
        REQUIRE(extended_search_string_view2.surrounded_by_delims_or_wildcards(lexer));
        REQUIRE(6 == extended_search_string_view2.length());
        REQUIRE(extended_search_string_view2.get_value() == "test\\*");
    }

    SECTION("getters") {
        auto search_string_view = WildcardExpressionView{search_string, 2, search_string.length()};
        REQUIRE(false == search_string_view.is_greedy_wildcard());
        REQUIRE(false == search_string_view.is_non_greedy_wildcard());
        REQUIRE('t' == search_string_view.get_char(0));
        REQUIRE(false == search_string_view.char_is_escape(0));
        REQUIRE(false == search_string_view.char_is_greedy_wildcard(0));
        REQUIRE(false == search_string_view.char_is_non_greedy_wildcard(0));
        REQUIRE('\\' == search_string_view.get_char(4));
        REQUIRE(search_string_view.char_is_escape(4));
        REQUIRE(false == search_string_view.char_is_greedy_wildcard(4));
        REQUIRE(false == search_string_view.char_is_non_greedy_wildcard(4));
        REQUIRE('*' == search_string_view.get_char(5));
        REQUIRE(false == search_string_view.char_is_escape(5));
        REQUIRE(false == search_string_view.char_is_greedy_wildcard(5));
        REQUIRE(false == search_string_view.char_is_non_greedy_wildcard(5));
        REQUIRE('*' == search_string_view.get_char(7));
        REQUIRE(false == search_string_view.char_is_escape(7));
        REQUIRE(search_string_view.char_is_greedy_wildcard(7));
        REQUIRE(false == search_string_view.char_is_non_greedy_wildcard(7));
    }

    SECTION("Greedy Wildcard") {
        auto search_string_view = WildcardExpressionView{search_string, 0, 1};
        REQUIRE(search_string_view.is_greedy_wildcard());
        REQUIRE(false == search_string_view.is_non_greedy_wildcard());
    }
}

TEST_CASE("get_matching_variable_types", "[get_matching_variable_types][schema_search]") {
    ByteLexer lexer;
    load_lexer_from_file("../tests/test_schema_files/search_schema.txt", false, lexer);

    SECTION("Non-wildcard search query") {
        constexpr std::string_view cWildcardExprValue("* 10000 reply: *");
        constexpr std::string_view cNumber = "10000";
        constexpr size_t cFirstGreedyWildcardIdx = cWildcardExprValue.find_first_of('*');
        constexpr size_t cLastGreedyWildcardIdx = cWildcardExprValue.find_last_of('*');
        constexpr size_t cECharIdx = cWildcardExprValue.find('e');
        constexpr size_t cNumberBeginIdx = cWildcardExprValue.find(cNumber);
        constexpr size_t cNumberEndIdx = cNumberBeginIdx + cNumber.length();
        WildcardExpression const wildcard_expr{string{cWildcardExprValue}};

        // Test all subexpressions of `wildcard_expr`
        for (uint32_t end_idx = 1; end_idx <= wildcard_expr.length(); end_idx++) {
            for (uint32_t begin_idx = 0; begin_idx < end_idx; begin_idx++) {
                auto [variable_types, contains_wildcard] = Grep::get_matching_variable_types(
                        WildcardExpressionView{wildcard_expr, begin_idx, end_idx},
                        lexer
                );

                std::set<uint32_t> expected_variable_types;
                if ((cFirstGreedyWildcardIdx == begin_idx && cFirstGreedyWildcardIdx + 1 == end_idx)
                    || (cLastGreedyWildcardIdx == begin_idx && cLastGreedyWildcardIdx + 1 == end_idx
                    ))
                {
                    // "*"
                    expected_variable_types
                            = {lexer.m_symbol_id["timestamp"],
                               lexer.m_symbol_id["int"],
                               lexer.m_symbol_id["float"],
                               lexer.m_symbol_id["hex"],
                               lexer.m_symbol_id["hasNumber"],
                               lexer.m_symbol_id["uniqueVariable"],
                               lexer.m_symbol_id["test"]};
                } else if (cNumberBeginIdx <= begin_idx && end_idx <= cNumberEndIdx) {
                    // Substrings of "10000"
                    expected_variable_types
                            = {lexer.m_symbol_id["int"], lexer.m_symbol_id["hasNumber"]};
                } else if (cECharIdx == begin_idx && cECharIdx + 1 == end_idx) {
                    // "e"
                    expected_variable_types = {lexer.m_symbol_id["hex"]};
                }

                bool expected_contains_wildcard = false;
                if (cFirstGreedyWildcardIdx == begin_idx || cLastGreedyWildcardIdx + 1 == end_idx) {
                    expected_contains_wildcard = true;
                }

                CAPTURE(wildcard_expr.substr(begin_idx, end_idx - begin_idx));
                CAPTURE(begin_idx);
                CAPTURE(end_idx);
                REQUIRE(variable_types == expected_variable_types);
                REQUIRE(contains_wildcard == expected_contains_wildcard);
            }
        }
    }

    SECTION("Non-greedy wildcard followed by a greedy wildcard") {
        constexpr std::string_view cWildcardExprValue("?*");

        WildcardExpression const wildcard_expr{string{cWildcardExprValue}};
        auto [variable_types, contains_wildcard] = Grep::get_matching_variable_types(
                WildcardExpressionView{wildcard_expr, 0, wildcard_expr.length()},
                lexer
        );

        set expected_variable_types
                = {lexer.m_symbol_id["timestamp"],
                   lexer.m_symbol_id["int"],
                   lexer.m_symbol_id["float"],
                   lexer.m_symbol_id["hex"],
                   lexer.m_symbol_id["hasNumber"],
                   lexer.m_symbol_id["uniqueVariable"],
                   lexer.m_symbol_id["test"]};
        bool expected_contains_wildcard = true;

        REQUIRE(variable_types == expected_variable_types);
        REQUIRE(contains_wildcard == expected_contains_wildcard);
    }
}

TEST_CASE(
        "get_interpretations_for_whole_wildcard_expr",
        "[get_interpretations_for_whole_wildcard_expr][schema_search]"
) {
    ByteLexer lexer;
    load_lexer_from_file("../tests/test_schema_files/search_schema.txt", false, lexer);

    SECTION("Non-wildcard search query") {
        constexpr string_view cWildcardExprValue("* 10000 reply: *");
        constexpr string_view cNumber = "10000";
        constexpr size_t cNumberBeginIdx = cWildcardExprValue.find(cNumber);
        constexpr size_t cNumberEndIdx = cNumberBeginIdx + cNumber.length();
        WildcardExpression const wildcard_expr{string{cWildcardExprValue}};

        for (uint32_t end_idx = 1; end_idx <= wildcard_expr.length(); end_idx++) {
            for (uint32_t begin_idx = 0; begin_idx < end_idx; begin_idx++) {
                auto interpretations = Grep::get_interpretations_for_whole_wildcard_expr(
                        WildcardExpressionView{wildcard_expr, begin_idx, end_idx},
                        lexer
                );

                vector<QueryInterpretation> expected_interpretations(0);
                if (cNumberBeginIdx == begin_idx && cNumberEndIdx == end_idx) {
                    QueryInterpretation expected_interpretation;
                    expected_interpretation.append_variable_token(
                            static_cast<int>(lexer.m_symbol_id["int"]),
                            string{cNumber},
                            false,
                            false
                    );
                    expected_interpretations.emplace_back(expected_interpretation);
                } else if ((0 != begin_idx && wildcard_expr.length() != end_idx)
                           || (end_idx - begin_idx == 1))
                {
                    QueryInterpretation expected_interpretation;
                    for (uint32_t idx = begin_idx; idx < end_idx; idx++) {
                        expected_interpretation.append_static_token(wildcard_expr.substr(idx, 1));
                    }
                    expected_interpretations.emplace_back(expected_interpretation);
                }

                CAPTURE(begin_idx);
                CAPTURE(end_idx);
                REQUIRE(interpretations == expected_interpretations);
            }
        }
    }

    SECTION("Non-greedy wildcard followed by a greedy wildcard") {
        constexpr string_view cWildcardExprValue(" ?* ");
        WildcardExpression const wildcard_expr{string{cWildcardExprValue}};

        auto interpretations = Grep::get_interpretations_for_whole_wildcard_expr(
                WildcardExpressionView{wildcard_expr, 1, 2},
                lexer
        );
        vector<QueryInterpretation> expected_interpretations(0);

        {
            QueryInterpretation expected_interpretation;
            expected_interpretation.append_static_token("?");
            expected_interpretations.emplace_back(expected_interpretation);
        }

        for (auto const& var_type : {"int", "float"}) {
            for (auto const encoded : {true, false}) {
                QueryInterpretation expected_interpretation;
                expected_interpretation.append_variable_token(
                        static_cast<int>(lexer.m_symbol_id[var_type]),
                        string{"?*"},
                        true,
                        encoded
                );
                expected_interpretations.emplace_back(expected_interpretation);
            }
        }

        // Note: all the other non-encodable variable types are ignored because CLP considers them
        // to be the same as timestamp (i.e., they're all stored in the dictionary).
        for (auto const& var_type : {"timestamp"}) {
            QueryInterpretation expected_interpretation;
            expected_interpretation.append_variable_token(
                    static_cast<int>(lexer.m_symbol_id[var_type]),
                    string{"?*"},
                    true,
                    false
            );
            expected_interpretations.emplace_back(expected_interpretation);
        }

        std::ostringstream oss;
        oss << lexer.m_id_symbol;
        CAPTURE(oss.str());
        REQUIRE(interpretations == expected_interpretations);
    }
}

TEST_CASE(
        "generate_query_substring_interpretations",
        "[generate_query_substring_interpretations][schema_search]"
) {
    ByteLexer lexer;
    load_lexer_from_file("../tests/test_schema_files/search_schema.txt", false, lexer);

    SECTION("Query with static text") {
        ExpectedInterpretation exp_interp(lexer);

        exp_interp.add_string("* z *", "0", "0", "* z *");

        exp_interp.compare("* z *");
    }
    SECTION("Query with a hex value") {
        ExpectedInterpretation exp_interp(lexer);

        // "* a *"
        exp_interp.add_string("* a *", "0", "0", "* a *");
        // "* <hex>(a) *"
        exp_interp.add_string<string>("* <{}>(a) *", "000", "000", "* {} *", "hex");

        exp_interp.compare("* a *");
    }
    SECTION("Query with an integer") {
        ExpectedInterpretation exp_interp(lexer);

        // "* 10000 reply: *"
        exp_interp.add_string("* 10000 reply: *", "0", "0", "* 10000 reply: *");
        // "* <int>(10000) reply: *"
        exp_interp
                .add_string<string>("* <{}>(10000) reply: *", "000", "000", "* {} reply: *", "int");

        exp_interp.compare("* 10000 reply: *");
    }
    SECTION("Query with a non-greedy wildcard at the start of a variable") {
        ExpectedInterpretation exp_interp(lexer);

        // "* ?10000 *"
        exp_interp.add_string("* ?10000 *", "0", "0", "* ?10000 *");
        // "* ?<int>(10000) *"
        exp_interp.add_string<string>("* ?<{}>(10000) *", "000", "000", "* ?{} *", "int");
        // "* <int>(?10000) *"
        // TODO: Add logic to determine this case is impossible.
        exp_interp.add_string<string>("* <{}>(?10000) *", "010", "000", "* {} *", "int", true);
        exp_interp.add_string<string>("* <{}>(?10000) *", "010", "010", "* {} *", "int", false);
        // "* <hasNumber>(?10000) *"
        exp_interp.add_string<string>("* <{}>(?10000) *", "010", "000", "* {} *", "hasNumber");

        exp_interp.compare("* ?10000 *");
    }
    SECTION("Query with a non-greedy wildcard at the end of a variable") {
        ExpectedInterpretation exp_interp(lexer);

        // "* 10000? *"
        exp_interp.add_string("* 10000? *", "0", "0", "* 10000? *");
        // "* <int>(10000)? *"
        exp_interp.add_string<string>("* <{}>(10000)? *", "000", "000", "* {}? *", "int");
        // "* <int>(10000?) *"
        exp_interp.add_string<string>("* <{}>(10000?) *", "010", "000", "* {} *", "int", true);
        exp_interp.add_string<string>("* <{}>(10000?) *", "010", "010", "* {} *", "int", false);
        // "* <hasNumber>(10000?) *"
        exp_interp.add_string<string>("* <{}>(10000?) *", "010", "000", "* {} *", "hasNumber");

        exp_interp.compare("* 10000? *");
    }
    SECTION("Query with a non-greedy wildcard in the middle of a variable") {
        ExpectedInterpretation exp_interp(lexer);

        // "* 10000? *"
        exp_interp.add_string("* 100?00 *", "0", "0", "* 100?00 *");
        // "* <int>(100?00) *"
        exp_interp.add_string<string>("* <{}>(100?00) *", "010", "010", "* {} *", "int", false);
        // TODO: add logic to determine this case is impossible
        exp_interp.add_string<string>("* <{}>(100?00) *", "010", "000", "* {} *", "int", true);
        // "* <float>(100?00) *"
        exp_interp.add_string<string>("* <{}>(100?00) *", "010", "010", "* {} *", "float", false);
        // TODO: add logic to determine this case is impossible
        exp_interp.add_string<string>("* <{}>(100?00) *", "010", "000", "* {} *", "float", true);
        // "* <hasNumber>(100?00) *"
        exp_interp.add_string<string>("* <{}>(100?00) *", "010", "000", "* {} *", "hasNumber");
        // "* <int>(100)?00 *"
        // TODO: Add logic to determine this case is impossible.
        exp_interp.add_string<string>("* <{}>(100)?00 *", "000", "000", "* {}?00 *", "int");
        // "* 100?<int>(00) *"
        // TODO: Add logic to determine this case is impossible.
        exp_interp.add_string<string>("* 100?<{}>(00) *", "000", "000", "* 100?{} *", "int", true);
        // "* <int>(100)?<int>(00) *"
        exp_interp.add_string<string, string>(
                "* <{}>(100)?<{}>(00) *",
                "00000",
                "00000",
                "* {}?{} *",
                "int",
                "int",
                false,
                true
        );

        exp_interp.compare("* 100?00 *");
    }
    SECTION("Query with a non-greedy wildcard and escaped wildcard") {
        ExpectedInterpretation exp_interp(lexer);

        // "* 10\\?000? *"
        exp_interp.add_string("* 10\\?000? *", "0", "0", "* 10\\?000? *");
        // "* <int>(10)\\?000? *"
        exp_interp.add_string<string>(
                "* <{}>(10)\\?000? *",
                "000",
                "000",
                "* {}\\?000? *",
                "int",
                false
        );
        // "* <int>(10)\\?<int>(000)? *"
        exp_interp.add_string<string, string>(
                "* <{}>(10)\\?<{}>(000)? *",
                "00000",
                "00000",
                "* {}\\?{}? *",
                "int",
                "int",
                false,
                true
        );
        // "* <int>(10)\\?<int>(000?) *"
        exp_interp.add_string<string, string>(
                "* <{}>(10)\\?<{}>(000?) *",
                "00010",
                "00010",
                "* {}\\?{} *",
                "int",
                "int",
                false,
                false
        );
        exp_interp.add_string<string, string>(
                "* <{}>(10)\\?<{}>(000?) *",
                "00010",
                "00000",
                "* {}\\?{} *",
                "int",
                "int",
                false,
                true
        );
        // "* <int>(10)\\?<hasNumber>(000?) *"
        exp_interp.add_string<string, string>(
                "* <{}>(10)\\?<{}>(000?) *",
                "00010",
                "00000",
                "* {}\\?{} *",
                "int",
                "hasNumber",
                false,
                true
        );
        // "* 10\\?<int>(000)? *"
        exp_interp.add_string<string>(
                "* 10\\?<{}>(000)? *",
                "000",
                "000",
                "* 10\\?{}? *",
                "int",
                true
        );
        // "* 10\\?<int>(000?) *"
        exp_interp.add_string<string>(
                "* 10\\?<{}>(000?) *",
                "010",
                "000",
                "* 10\\?{} *",
                "int",
                true
        );
        exp_interp.add_string<string>(
                "* 10\\?<{}>(000?) *",
                "010",
                "010",
                "* 10\\?{} *",
                "int",
                false
        );
        // "* 10\\?<hasNumber>(000?) *"
        exp_interp.add_string<string>(
                "* 10\\?<{}>(000?) *",
                "010",
                "000",
                "* 10\\?{} *",
                "hasNumber",
                false
        );

        exp_interp.compare("* 10\\?000? *");
    }
    SECTION("Query with greedy wildcard") {
        ExpectedInterpretation exp_interp(lexer);

        // "* *10000 *"
        exp_interp.add_string("* *10000 *", "0", "0", "* *10000 *");
        // "*<timestamp>(* *)*10000 *"
        exp_interp.add_string<string>(
                "*<{}>(* *)*10000 *",
                "010",
                "000",
                "*{}*10000 *",
                "timestamp",
                false
        );
        // "* *<int>(*10000) *"
        exp_interp.add_string<string>("* *<{}>(*10000) *", "010", "000", "* *{} *", "int", true);
        exp_interp.add_string<string>("* *<{}>(*10000) *", "010", "010", "* *{} *", "int", false);
        // "* *<float>(*10000) *"
        exp_interp.add_string<string>("* *<{}>(*10000) *", "010", "000", "* *{} *", "float", true);
        exp_interp.add_string<string>("* *<{}>(*10000) *", "010", "010", "* *{} *", "float", false);
        // "* *<hasNumber>(*10000) *"
        exp_interp.add_string<string>("* *<{}>(*10000) *", "010", "000", "* *{} *", "hasNumber");
        // "*<timestamp>(* *)*<int>(*10000) *"
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*<{}>(*10000) *",
                "01010",
                "00000",
                "*{}*{} *",
                "timestamp",
                "int",
                false,
                true
        );
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*<{}>(*10000) *",
                "01010",
                "00010",
                "*{}*{} *",
                "timestamp",
                "int",
                false,
                false
        );
        // "*<timestamp>(* *)*<float>(*10000) *"
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*<{}>(*10000) *",
                "01010",
                "00000",
                "*{}*{} *",
                "timestamp",
                "float",
                false,
                true
        );
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*<{}>(*10000) *",
                "01010",
                "00010",
                "*{}*{} *",
                "timestamp",
                "float",
                false,
                false
        );
        // "*<timestamp>(* *)*<hasNumber>(*10000) *"
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*<{}>(*10000) *",
                "01010",
                "00000",
                "*{}*{} *",
                "timestamp",
                "hasNumber",
                false,
                false
        );

        exp_interp.compare("* *10000 *");
    }
    SECTION("Query with greedy wildcard followed by non-greedy wildcard") {
        ExpectedInterpretation exp_interp(lexer);

        // "* *?10000 *"
        exp_interp.add_string("* *?10000 *", "0", "0", "* *?10000 *");
        // "*<timestamp>(* *)*?10000 *"
        exp_interp.add_string<string>(
                "*<{}>(* *)*?10000 *",
                "010",
                "000",
                "*{}*?10000 *",
                "timestamp"
        );
        // "*<timestamp>(* *)*<int>(*?10000) *"
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*<{}>(*?10000) *",
                "01010",
                "00000",
                "*{}*{} *",
                "timestamp",
                "int",
                false,
                true
        );
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*<{}>(*?10000) *",
                "01010",
                "00010",
                "*{}*{} *",
                "timestamp",
                "int",
                false,
                false
        );
        // "*<timestamp>(* *)*<float>(*?10000) *"
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*<{}>(*?10000) *",
                "01010",
                "00000",
                "*{}*{} *",
                "timestamp",
                "float",
                false,
                true
        );
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*<{}>(*?10000) *",
                "01010",
                "00010",
                "*{}*{} *",
                "timestamp",
                "float",
                false,
                false
        );
        // "*<timestamp>(* *)*<hasNumber>(*?10000) *"
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*<{}>(*?10000) *",
                "01010",
                "00000",
                "*{}*{} *",
                "timestamp",
                "hasNumber",
                false,
                false
        );
        // "*<timestamp>(* *)*?<int>(10000) *"
        exp_interp.add_string<string, string>(
                "*<{}>(* *)*?<{}>(10000) *",
                "01000",
                "00000",
                "*{}*?{} *",
                "timestamp",
                "int",
                false,
                false
        );
        // "* *<int>(*?10000) *"
        exp_interp.add_string<string>("* *<{}>(*?10000) *", "010", "000", "* *{} *", "int", true);
        exp_interp.add_string<string>("* *<{}>(*?10000) *", "010", "010", "* *{} *", "int", false);
        // "* *<float>(*?10000) *"
        exp_interp.add_string<string>("* *<{}>(*?10000) *", "010", "000", "* *{} *", "float", true);
        exp_interp
                .add_string<string>("* *<{}>(*?10000) *", "010", "010", "* *{} *", "float", false);
        // "* *<hasNumber>(*?10000) *"
        exp_interp.add_string<string>("* *<{}>(*?10000) *", "010", "000", "* *{} *", "hasNumber");
        // "* *?<int>(10000) *"
        exp_interp.add_string<string>("* *?<{}>(10000) *", "000", "000", "* *?{} *", "int");

        exp_interp.compare("* *?10000 *");
    }
    SECTION("Query with non-greedy wildcard followed by greedy wildcard") {
        ExpectedInterpretation exp_interp(lexer);

        // "* ?*10000 *"
        exp_interp.add_string("* ?*10000 *", "0", "0", "* ?*10000 *");
        // "*<timestamp>(* ?*)*10000 *"
        exp_interp.add_string<string>(
                "*<{}>(* ?*)*10000 *",
                "010",
                "000",
                "*{}*10000 *",
                "timestamp"
        );
        // "*<timestamp>(* ?*)*<hasNumber>(*10000) *"
        exp_interp.add_string<string, string>(
                "*<{}>(* ?*)*<{}>(*10000) *",
                "01010",
                "00000",
                "*{}*{} *",
                "timestamp",
                "hasNumber",
                false,
                false
        );
        // "* <hasNumber>(?*10000) *"
        exp_interp.add_string<string>("* <{}>(?*10000) *", "010", "000", "* {} *", "hasNumber");
        // "* <hasNumber>(*10000) *"
        exp_interp.add_string<string>("* ?*<{}>(*10000) *", "010", "000", "* ?*{} *", "hasNumber");
        // Note: all the other non-encodable variable types are ignored because CLP considers them
        // to be the same as timestamp (i.e., they're all stored in the dictionary).
        for (auto type1 : {"timestamp"}) {
            // "* <hasNumber/timestamp>(?*)*10000 *"
            exp_interp
                    .add_string<string>("* <{}>(?*)*10000 *", "010", "000", "* {}*10000 *", type1);
            for (auto type2 : {"int", "float"}) {
                // "* <hasNumber/Timestamp>(?*)*<int/float>(*10000) *"
                exp_interp.add_string<string, string>(
                        "* <{}>(?*)*<{}>(*10000) *",
                        "01010",
                        "00000",
                        "* {}*{} *",
                        type1,
                        type2,
                        false,
                        true
                );
                exp_interp.add_string<string, string>(
                        "* <{}>(?*)*<{}>(*10000) *",
                        "01010",
                        "00010",
                        "* {}*{} *",
                        type1,
                        type2,
                        false,
                        false
                );
            }
            // "* <hasNumber/Timestamp>(?*)*<hasNumber>(*10000) *"
            exp_interp.add_string<string, string>(
                    "* <{}>(?*)*<{}>(*10000) *",
                    "01010",
                    "00000",
                    "* {}*{} *",
                    type1,
                    "hasNumber",
                    false,
                    false
            );
        }
        for (auto type1 : {"int", "float"}) {
            // "*<timestamp>(* ?*)*<int/float>(*10000) *"
            exp_interp.add_string<string, string>(
                    "*<{}>(* ?*)*<{}>(*10000) *",
                    "01010",
                    "00000",
                    "*{}*{} *",
                    "timestamp",
                    type1,
                    false,
                    true
            );
            exp_interp.add_string<string, string>(
                    "*<{}>(* ?*)*<{}>(*10000) *",
                    "01010",
                    "00010",
                    "*{}*{} *",
                    "timestamp",
                    type1,
                    false,
                    false
            );
            // "* ?*<int/float>(*10000) *"
            exp_interp.add_string<string>(
                    "* ?*<{}>(*10000) *",
                    "010",
                    "000",
                    "* ?*{} *",
                    type1,
                    true
            );
            exp_interp.add_string<string>(
                    "* ?*<{}>(*10000) *",
                    "010",
                    "010",
                    "* ?*{} *",
                    type1,
                    false
            );
            // "* <int/float>(?*10000) *"
            exp_interp.add_string<string>("* <{}>(?*10000) *", "010", "000", "* {} *", type1, true);
            exp_interp
                    .add_string<string>("* <{}>(?*10000) *", "010", "010", "* {} *", type1, false);
            // "* <int/float>(?*)*10000 *"
            exp_interp.add_string<string>(
                    "* <{}>(?*)*10000 *",
                    "010",
                    "000",
                    "* {}*10000 *",
                    type1,
                    true
            );
            exp_interp.add_string<string>(
                    "* <{}>(?*)*10000 *",
                    "010",
                    "010",
                    "* {}*10000 *",
                    type1,
                    false
            );
            for (auto type2 : {"int", "float"}) {
                // "* <int/float>(?*)*<int/float>(*10000) *"
                exp_interp.add_string<string, string>(
                        "* <{}>(?*)*<{}>(*10000) *",
                        "01010",
                        "00000",
                        "* {}*{} *",
                        type1,
                        type2,
                        true,
                        true
                );
                exp_interp.add_string<string, string>(
                        "* <{}>(?*)*<{}>(*10000) *",
                        "01010",
                        "00010",
                        "* {}*{} *",
                        type1,
                        type2,
                        true,
                        false
                );
                exp_interp.add_string<string, string>(
                        "* <{}>(?*)*<{}>(*10000) *",
                        "01010",
                        "01000",
                        "* {}*{} *",
                        type1,
                        type2,
                        false,
                        true
                );
                exp_interp.add_string<string, string>(
                        "* <{}>(?*)*<{}>(*10000) *",
                        "01010",
                        "01010",
                        "* {}*{} *",
                        type1,
                        type2,
                        false,
                        false
                );
            }
            // "* <int/float>(?*)*<hasNumber>(*10000) *"
            exp_interp.add_string<string, string>(
                    "* <{}>(?*)*<{}>(*10000) *",
                    "01010",
                    "00000",
                    "* {}*{} *",
                    type1,
                    "hasNumber",
                    true,
                    false
            );
            exp_interp.add_string<string, string>(
                    "* <{}>(?*)*<{}>(*10000) *",
                    "01010",
                    "01000",
                    "* {}*{} *",
                    type1,
                    "hasNumber",
                    false,
                    false
            );
        }
        exp_interp.compare("* ?*10000 *");
    }
}
