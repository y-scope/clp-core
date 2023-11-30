#include "Subquery.hpp"

#include "../../ir/parsing.hpp"
#include "QueryWildcard.hpp"

using std::string;
using std::variant;
using std::vector;

namespace ffi::search {
template <typename encoded_variable_t>
Subquery<encoded_variable_t>::Subquery(string logtype_query, Subquery::QueryVariables variables)
        : m_logtype_query{std::move(logtype_query)},
          m_logtype_query_contains_wildcards{false},
          m_query_vars{std::move(variables)} {
    // Determine if the query contains variables
    bool is_escaped{false};
    auto const logtype_query_length{m_logtype_query.size()};
    std::vector<size_t> escaped_placeholder_positions;
    escaped_placeholder_positions.reserve(logtype_query_length / 2);
    auto const escape_char{enum_to_underlying_type(ir::VariablePlaceholder::Escape)};
    for (size_t idx = 0; idx < logtype_query_length; ++idx) {
        char const c{m_logtype_query[idx]};
        if (is_escaped) {
            is_escaped = false;
            if (escape_char == c) {
                continue;
            }
            escaped_placeholder_positions.push_back(idx);
        } else if (escape_char == c) {
            is_escaped = true;
        } else if ((enum_to_underlying_type(WildcardType::ZeroOrMoreChars) == c
                    || enum_to_underlying_type(WildcardType::AnyChar) == c))
        {
            m_logtype_query_contains_wildcards = true;
        }
    }
    if (false == m_logtype_query_contains_wildcards) {
        return;
    }
    if (escaped_placeholder_positions.empty()) {
        return;
    }
    std::string double_escaped_logtype_query;
    size_t curr_pos{0};
    for (auto const pos : escaped_placeholder_positions) {
        double_escaped_logtype_query.append(m_logtype_query, curr_pos, pos - curr_pos);
        double_escaped_logtype_query += escape_char;
        curr_pos = pos;
    }
    if (logtype_query_length != curr_pos) {
        double_escaped_logtype_query
                .append(m_logtype_query, curr_pos, logtype_query_length - curr_pos);
    }
    m_logtype_query = std::move(double_escaped_logtype_query);
}

// Explicitly declare specializations to avoid having to validate that the
// template parameters are supported
template class Subquery<eight_byte_encoded_variable_t>;
template class Subquery<four_byte_encoded_variable_t>;
}  // namespace ffi::search
