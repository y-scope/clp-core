// C++ standard libraries
#include <unordered_map>

// Catch2
#include <Catch2/single_include/catch2/catch.hpp>

// Project headers
#include "../src/ffi/encoding_methods.hpp"
#include "../src/ffi/search/ExactVariableToken.hpp"
#include "../src/ffi/search/query_methods.hpp"
#include "../src/ffi/search/QueryMethodFailed.hpp"
#include "../src/ffi/search/WildcardToken.hpp"
#include "../src/ir/parsing.hpp"

using ffi::eight_byte_encoded_variable_t;
using ffi::four_byte_encoded_variable_t;
using ffi::search::ExactVariableToken;
using ffi::search::generate_subqueries;
using ffi::search::Subquery;
using ffi::search::TokenType;
using ffi::search::WildcardToken;
using ir::VariablePlaceholder;
using std::string;
using std::variant;
using std::vector;

/**
 * Simple class to hold the type of a query variable
 */
struct QueryVariableType {
public:
    QueryVariableType () = default;
    QueryVariableType (bool is_exact, VariablePlaceholder interpretation) :
            is_exact(is_exact), interpretation(interpretation) {}

    bool is_exact;
    VariablePlaceholder interpretation;
};

/**
 * Simple class to hold the expected values of a subquery in tests
 */
struct ExpectedSubquery {
public:
    // Methods
    void clear () {
        logtype_query.clear();
        logtype_query_contains_wildcards = false;
        query_var_types.clear();
    }

    string logtype_query;
    bool logtype_query_contains_wildcards;
    vector<QueryVariableType> query_var_types;
};

TEMPLATE_TEST_CASE("ffi::search::query_methods", "[ffi][search][query_methods]",
                   eight_byte_encoded_variable_t, four_byte_encoded_variable_t)
{
    using TestTypeExactVariableToken = ExactVariableToken<TestType>;
    using TestTypeWildcardVariableToken = WildcardToken<TestType>;

    string wildcard_query;
    vector<Subquery<TestType>> subqueries;

    SECTION("Empty query") {
        REQUIRE_THROWS_AS(generate_subqueries(wildcard_query, subqueries),
                          ffi::search::QueryMethodFailed);
    }

    SECTION("\"*\"") {
        wildcard_query = "*";
        generate_subqueries(wildcard_query, subqueries);
        REQUIRE(subqueries.size() == 1);
        const auto& subquery = subqueries.front();
        REQUIRE(subquery.get_logtype_query() == wildcard_query);
        REQUIRE(subquery.logtype_query_contains_wildcards());
    }

    SECTION("No wildcards") {
        // Encode a message
        string message;
        string logtype;
        vector<TestType> encoded_vars;
        vector<int32_t> dictionary_var_bounds;
        vector<string> var_strs = {"4938", std::to_string(INT32_MAX), std::to_string(INT64_MAX),
                                   "0.1", "-25.519686", "-25.5196868642755", "-00.00",
                                   "bin/python2.7.3", "abc123"};
        size_t var_ix = 0;
        message = "here is a string with a small int " + var_strs[var_ix++];
        message += " and a medium int " + var_strs[var_ix++];
        message += " and a very large int " + var_strs[var_ix++];
        message += " and a small double " + var_strs[var_ix++];
        message += " and a medium double " + var_strs[var_ix++];
        message += " and a weird double " + var_strs[var_ix++];
        message += " and a string with numbers " + var_strs[var_ix++];
        message += " and another string with numbers " + var_strs[var_ix++];
        message += " and an escape ";
        message += enum_to_underlying_type(ir::VariablePlaceholder::Escape);
        message += " and an int placeholder ";
        message += enum_to_underlying_type(ir::VariablePlaceholder::Integer);
        message += " and a float placeholder ";
        message += enum_to_underlying_type(ir::VariablePlaceholder::Float);
        message += " and a dictionary placeholder ";
        message += enum_to_underlying_type(ir::VariablePlaceholder::Dictionary);
        REQUIRE(ffi::encode_message(message, logtype, encoded_vars, dictionary_var_bounds));

        wildcard_query = message;
        generate_subqueries(wildcard_query, subqueries);
        REQUIRE(subqueries.size() == 1);
        const auto& subquery = subqueries.front();

        // Validate that the subquery matches the encoded message
        REQUIRE(logtype == subquery.get_logtype_query());
        REQUIRE(false == subquery.logtype_query_contains_wildcards());
        size_t dict_var_idx = 0;
        size_t encoded_var_idx = 0;
        for (const auto& query_var : subquery.get_query_vars()) {
            REQUIRE(std::holds_alternative<TestTypeExactVariableToken>(query_var));
            const auto& exact_var = std::get<TestTypeExactVariableToken>(query_var);
            if (VariablePlaceholder::Dictionary == exact_var.get_placeholder()) {
                auto begin_pos = dictionary_var_bounds[dict_var_idx];
                auto end_pos = dictionary_var_bounds[dict_var_idx + 1];
                REQUIRE(exact_var.get_value() == message.substr(begin_pos, end_pos - begin_pos));
                dict_var_idx += 2;
            } else {
                REQUIRE(exact_var.get_encoded_value() == encoded_vars[encoded_var_idx]);
                ++encoded_var_idx;
            }
        }
    }

    // This test is meant to encompass most cases without being impossible to
    // write by hand. The cases are organized below in the order that they
    // would be generated by following the process described in the README.
    SECTION("\"*abc*123?456?\"") {
        std::unordered_map<string, ExpectedSubquery> logtype_query_to_expected_subquery;
        ExpectedSubquery expected_subquery;

        // In the comments below, we use:
        // - \i to denote VariablePlaceholder::Integer,
        // - \f to denote VariablePlaceholder::Float, and
        // - \d to VariablePlaceholder::Dictionary

        // All wildcards treated as delimiters, "*abc*" as static text
        // Expected logtype: "*abc*\i?\i?"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(true, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\f?\i?"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(true, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\d?\i?"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(
                VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(true, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // All wildcards treated as delimiters, "*abc*" as a dictionary variable
        // Expected logtype: "*\d*\i?\i?"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query += enum_to_underlying_type(
                VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(true, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\f?\i?"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query += enum_to_underlying_type(
                VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(true, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\d?\i?"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query += enum_to_underlying_type(
                VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(
                VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(true, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Second '*' treated as a non-delimiter
        // Expected logtype: "*\d?\i?"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(true, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Second '*' treated as delim, first '?' as non-delim, "*abc*" as
        // static text
        // Expected logtype: "*abc*\i?"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\f?"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\d?"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Second '*' treated as delim, first '?' as non-delim, "*abc*" as a
        // dictionary variable
        // Expected logtype: "*\d*\i?"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\f?"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\d?"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Second '*' as non-delim, first '?' as non-delim
        // Expected logtype: "*\d?"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Second '*' as delim, first '?' as delim, second '?' as non-delim,
        // "*abc*" as static text
        // Expected logtype: "*abc*\i?\i"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\f?\i"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\d?\i"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\i?\f"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\f?\f"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\d?\f"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\i?\d"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\f?\d"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\d?\d"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Second '*' as delim, first '?' as delim, second '?' as non-delim,
        // "*abc*" as a dictionary variable
        // Expected logtype: "*\d*\i?\i"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\f?\i"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\d?\i"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\i?\f"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\f?\f"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\d?\f"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\i?\d"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\f?\d"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\d?\d"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Second '*' as non-delim, first '?' as delim, second '?' as non-delim
        // Expected logtype: "*\d?\i"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d?\f"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d?\d"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '?';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Second '*' as delim, first '?' as non-delim, second '?' as non-delim,
        // "*abc*" as static text
        // Expected logtype: "*abc*\i"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\f"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*abc*\d"
        expected_subquery.logtype_query = "*abc*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Second '*' as delim, first '?' as non-delim, second '?' as non-delim,
        // "*abc*" as a dictionary variable
        // Expected logtype: "*\d*\i"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Integer);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Integer);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\f"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Float);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Float);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Expected logtype: "*\d*\d"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += '*';
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        // Second '*' as non-delim, first '?' as non-delim, second '?' as
        // non-delim"*abc*" as a dictionary variable
        // Expected logtype: "*\d"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query +=
                enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        logtype_query_to_expected_subquery.emplace(expected_subquery.logtype_query,
                                                   expected_subquery);
        expected_subquery.clear();

        wildcard_query = "*abc*123?456?";
        generate_subqueries(wildcard_query, subqueries);
        REQUIRE(subqueries.size() == logtype_query_to_expected_subquery.size());
        const auto map_end = logtype_query_to_expected_subquery.cend();
        for (const auto& subquery : subqueries) {
            const auto& logtype_query = subquery.get_logtype_query();
            const auto& query_vars = subquery.get_query_vars();

            auto idx = logtype_query_to_expected_subquery.find(logtype_query);
            REQUIRE(map_end != idx);
            const auto& expected_subquery = idx->second;
            REQUIRE(subquery.logtype_query_contains_wildcards()
                    == expected_subquery.logtype_query_contains_wildcards);
            const auto& expected_var_types = expected_subquery.query_var_types;
            REQUIRE(expected_var_types.size() == query_vars.size());
            for (size_t i = 0; i < expected_var_types.size(); ++i) {
                const auto& expected_var_type = expected_var_types[i];
                const auto& query_var = query_vars[i];

                if (expected_var_type.is_exact) {
                    REQUIRE(std::holds_alternative<TestTypeExactVariableToken>(query_var));
                    const auto& exact_var = std::get<TestTypeExactVariableToken>(query_var);
                    REQUIRE(expected_var_type.interpretation == exact_var.get_placeholder());
                } else {
                    REQUIRE(std::holds_alternative<TestTypeWildcardVariableToken>(query_var));
                    const auto& wildcard_var = std::get<TestTypeWildcardVariableToken>(query_var);
                    switch (expected_var_type.interpretation) {
                        case VariablePlaceholder::Integer:
                            REQUIRE(TokenType::IntegerVariable
                                    == wildcard_var.get_current_interpretation());
                            break;
                        case VariablePlaceholder::Float:
                            REQUIRE(TokenType::FloatVariable
                                    == wildcard_var.get_current_interpretation());
                            break;
                        case VariablePlaceholder::Dictionary:
                            REQUIRE(TokenType::DictionaryVariable
                                    == wildcard_var.get_current_interpretation());
                            break;
                        default:
                            REQUIRE(false);
                    }
                }
            }
        }
    }

    // In the following wildcard query, `^Q` represents a char with the value of
    // VariablePlaceholder::Integer and `^R` represents a char with the value of
    // VariablePlaceholder::Dictionary.
    SECTION("*escape ^Q placeholders ^R in subqueries*") {
        std::string const inner_static_text{
                std::string(" ") + enum_to_underlying_type(VariablePlaceholder::Integer)
                + " placeholders " + enum_to_underlying_type(VariablePlaceholder::Dictionary)
                + " in "};
        std::string const double_escaped_static_text{
                std::string(" ") + enum_to_underlying_type(VariablePlaceholder::Escape)
                + enum_to_underlying_type(VariablePlaceholder::Escape)
                + enum_to_underlying_type(VariablePlaceholder::Integer) + " placeholders "
                + enum_to_underlying_type(VariablePlaceholder::Escape)
                + enum_to_underlying_type(VariablePlaceholder::Escape)
                + enum_to_underlying_type(VariablePlaceholder::Dictionary) + " in "};
        std::string const prefix{"*escape"};
        std::string const postfix{"subqueries*"};

        std::unordered_map<string, ExpectedSubquery> logtype_query_to_expected_subquery;
        ExpectedSubquery expected_subquery;

        // In the comments below, \d donates VariablePlaceholder::Dictionary
        // Expected log type: "*escape \\^Q placeholders \\^R in subqueries*"
        expected_subquery.logtype_query = prefix;
        expected_subquery.logtype_query += double_escaped_static_text;
        expected_subquery.logtype_query += postfix;
        expected_subquery.logtype_query_contains_wildcards = true;
        logtype_query_to_expected_subquery.emplace(
                expected_subquery.logtype_query,
                expected_subquery
        );
        expected_subquery.clear();

        // Expected log type: "*\d \\^Q placeholders \\^R in subqueries*"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += double_escaped_static_text;
        expected_subquery.logtype_query += postfix;
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        logtype_query_to_expected_subquery.emplace(
                expected_subquery.logtype_query,
                expected_subquery
        );
        expected_subquery.clear();

        // Expected log type: "*escape \\^Q placeholders \\^R in \d*"
        expected_subquery.logtype_query = prefix;
        expected_subquery.logtype_query += double_escaped_static_text;
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += "*";
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        logtype_query_to_expected_subquery.emplace(
                expected_subquery.logtype_query,
                expected_subquery
        );
        expected_subquery.clear();

        // Expected log type: "*\d \\^Q placeholders \\^R in \d*"
        expected_subquery.logtype_query = "*";
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += double_escaped_static_text;
        expected_subquery.logtype_query += enum_to_underlying_type(VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query += "*";
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.query_var_types.emplace_back(false, VariablePlaceholder::Dictionary);
        expected_subquery.logtype_query_contains_wildcards = true;
        logtype_query_to_expected_subquery.emplace(
                expected_subquery.logtype_query,
                expected_subquery
        );
        expected_subquery.clear();

        wildcard_query = prefix + inner_static_text + postfix;
        generate_subqueries(wildcard_query, subqueries);

        REQUIRE(subqueries.size() == logtype_query_to_expected_subquery.size());
        auto const map_end{logtype_query_to_expected_subquery.cend()};
        for (auto const& subquery : subqueries) {
            auto const& logtype_query{subquery.get_logtype_query()};
            auto const& query_vars{subquery.get_query_vars()};

            auto const idx{logtype_query_to_expected_subquery.find(logtype_query)};
            REQUIRE(map_end != idx);
            auto const& expected_subquery{idx->second};
            REQUIRE(subquery.logtype_query_contains_wildcards()
                    == expected_subquery.logtype_query_contains_wildcards);
            auto const& expected_var_types{expected_subquery.query_var_types};
            REQUIRE(expected_var_types.size() == query_vars.size());
            for (size_t i{0}; i < expected_var_types.size(); ++i) {
                auto const& expected_var_type{expected_var_types[i]};
                auto const& query_var{query_vars[i]};

                if (expected_var_type.is_exact) {
                    REQUIRE(false);
                } else {
                    REQUIRE(std::holds_alternative<TestTypeWildcardVariableToken>(query_var));
                    auto const& wildcard_var{std::get<TestTypeWildcardVariableToken>(query_var)};
                    switch (expected_var_type.interpretation) {
                        case VariablePlaceholder::Integer:
                            REQUIRE(TokenType::IntegerVariable
                                    == wildcard_var.get_current_interpretation());
                            break;
                        case VariablePlaceholder::Float:
                            REQUIRE(TokenType::FloatVariable
                                    == wildcard_var.get_current_interpretation());
                            break;
                        case VariablePlaceholder::Dictionary:
                            REQUIRE(TokenType::DictionaryVariable
                                    == wildcard_var.get_current_interpretation());
                            break;
                        default:
                            REQUIRE(false);
                    }
                }
            }
        }
    }
}
