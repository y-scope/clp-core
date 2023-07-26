#ifndef QUERY_TOKEN_HPP
#define QUERY_TOKEN_HPP

// C++ standard libraries
#include <string>
#include <vector>

// Project headers
#include "Query.hpp"
#include "TraceableException.hpp"
#include "VariableDictionaryReader.hpp"
#include "VariableDictionaryWriter.hpp"

/**
 *  Class representing a token in a query. It is used to interpret a token in
 *  user's search string.
 */
class QueryToken {
public:
    // Constructors
    QueryToken (const std::string& query_string, size_t begin_pos, size_t end_pos, bool is_var);

    // Methods
    [[nodiscard]] bool cannot_convert_to_non_dict_var () const;

    [[nodiscard]] bool contains_wildcards () const;

    [[nodiscard]] bool has_greedy_wildcard_in_middle () const;

    [[nodiscard]] bool has_prefix_greedy_wildcard () const;

    [[nodiscard]] bool has_suffix_greedy_wildcard () const;

    [[nodiscard]] bool is_ambiguous_token () const;

    [[nodiscard]] bool is_float_var () const;

    [[nodiscard]] bool is_int_var () const;

    [[nodiscard]] bool is_var () const;

    [[nodiscard]] bool is_wildcard () const;

    [[nodiscard]] size_t get_begin_pos () const;

    [[nodiscard]] size_t get_end_pos () const;

    [[nodiscard]] const std::string& get_value () const;

    bool change_to_next_possible_type ();

private:
    // Types
    // Type for the purpose of generating different subqueries. E.g., if a token
    // is of type DictOrIntVar, it would generate a different subquery than if
    // it was of type Logtype.
    enum class Type {
        Wildcard,
        // Ambiguous indicates the token can be more than one of the types
        // listed below
        Ambiguous,
        Logtype,
        DictionaryVar,
        FloatVar,
        IntVar
    };

    // Variables
    bool m_cannot_convert_to_non_dict_var;
    bool m_contains_wildcards;
    bool m_has_greedy_wildcard_in_middle;
    bool m_has_prefix_greedy_wildcard;
    bool m_has_suffix_greedy_wildcard;

    size_t m_begin_pos;
    size_t m_end_pos;
    std::string m_value;

    // Type if variable has unambiguous type
    Type m_type;
    // Types if variable type is ambiguous
    std::vector<Type> m_possible_types;
    // Index of the current possible type selected for generating a subquery
    size_t m_current_possible_type_ix;
};

#endif // QUERY_TOKEN_HPP
        