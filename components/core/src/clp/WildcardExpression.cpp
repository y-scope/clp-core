#include "WildcardExpression.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include <log_surgeon/Lexer.hpp>
#include <string_utils/string_utils.hpp>

namespace clp {
WildcardExpression::WildcardExpression(std::string processed_search_string)
        : m_processed_search_string(std::move(processed_search_string)) {
    m_is_greedy_wildcard.reserve(m_processed_search_string.size());
    m_is_non_greedy_wildcard.reserve(m_processed_search_string.size());
    m_is_escape.reserve(m_processed_search_string.size());
    bool is_escaped = false;
    for (auto const& c : m_processed_search_string) {
        if (is_escaped) {
            m_is_greedy_wildcard.push_back(false);
            m_is_non_greedy_wildcard.push_back(false);
            m_is_escape.push_back(false);
            is_escaped = false;
        } else {
            if ('\\' == c) {
                m_is_greedy_wildcard.push_back(false);
                m_is_non_greedy_wildcard.push_back(false);
                m_is_escape.push_back(true);
                is_escaped = true;
            } else if ('*' == c) {
                m_is_greedy_wildcard.push_back(true);
                m_is_non_greedy_wildcard.push_back(false);
                m_is_escape.push_back(false);
            } else if ('?' == c) {
                m_is_greedy_wildcard.push_back(false);
                m_is_non_greedy_wildcard.push_back(true);
                m_is_escape.push_back(false);
            } else {
                m_is_greedy_wildcard.push_back(false);
                m_is_non_greedy_wildcard.push_back(false);
                m_is_escape.push_back(false);
            }
        }
    }
}

WildcardExpressionView::WildcardExpressionView(
        WildcardExpression const& wildcard_expression,
        size_t const begin_idx,
        size_t const end_idx
)
        : m_expression{&wildcard_expression},
          m_begin_idx{begin_idx},
          m_end_idx{end_idx} {
    m_end_idx = std::min(m_end_idx, wildcard_expression.length());
    m_begin_idx = std::min(m_begin_idx, m_end_idx);
}

auto WildcardExpressionView::extend_to_adjacent_greedy_wildcards() const -> WildcardExpressionView {
    auto extended_view = *this;
    bool const prev_char_is_greedy_wildcard
            = m_begin_idx > 0 && m_expression->char_is_greedy_wildcard(m_begin_idx - 1);
    if (prev_char_is_greedy_wildcard) {
        --extended_view.m_begin_idx;
    }
    bool const next_char_is_greedy_wildcard = m_end_idx < m_expression->length()
                                              && m_expression->char_is_greedy_wildcard(m_end_idx);
    if (next_char_is_greedy_wildcard) {
        ++extended_view.m_end_idx;
    }
    return extended_view;
}

auto WildcardExpressionView::surrounded_by_delims_or_wildcards(
        log_surgeon::lexers::ByteLexer const& lexer
) const -> bool {
    bool has_preceding_delim{};
    if (0 == m_begin_idx) {
        has_preceding_delim = true;
    } else {
        bool const preceded_by_greedy_wildcard
                = m_expression->char_is_greedy_wildcard(m_begin_idx - 1);
        bool const preceded_by_non_greedy_wildcard
                = m_expression->char_is_non_greedy_wildcard(m_begin_idx - 1);
        bool const preceded_by_delimiter
                = lexer.is_delimiter(m_expression->get_char(m_begin_idx - 1));
        has_preceding_delim = preceded_by_greedy_wildcard || preceded_by_non_greedy_wildcard
                              || preceded_by_delimiter;
    }

    bool has_succeeding_delim{};
    if (m_expression->length() == m_end_idx) {
        has_succeeding_delim = true;
    } else {
        bool const succeeded_by_greedy_wildcard = m_expression->char_is_greedy_wildcard(m_end_idx);
        bool const succeeded_by_non_greedy_wildcard
                = m_expression->char_is_non_greedy_wildcard(m_end_idx);
        // E.g. "foo:", where ':' is a delimiter
        bool const succeeded_by_unescaped_delim
                = false == m_expression->char_is_escape(m_end_idx)
                  && lexer.is_delimiter(m_expression->get_char(m_end_idx));
        // E.g. "foo\\", where '\' is a delimiter
        bool const succeeded_by_escaped_delim
                = m_expression->char_is_escape(m_end_idx)
                  && lexer.is_delimiter(m_expression->get_char(m_end_idx + 1));
        has_succeeding_delim = succeeded_by_greedy_wildcard || succeeded_by_non_greedy_wildcard
                               || succeeded_by_unescaped_delim || succeeded_by_escaped_delim;
    }

    return has_preceding_delim && has_succeeding_delim;
}
}  // namespace clp
