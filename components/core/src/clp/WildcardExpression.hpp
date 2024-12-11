#ifndef CLP_WILDCARDEXPRESSION_HPP
#define CLP_WILDCARDEXPRESSION_HPP

#include <cstddef>
#include <string>
#include <vector>

#include <log_surgeon/Lexer.hpp>

namespace clp {
/**
 * A pattern for matching strings. The pattern supports two types of wildcards:
 * - '*' matches zero or more characters
 * - '?' matches any single character
 *
 * To match a literal '*' or '?', the pattern should escape it with a backslash (`\`).
 */
class WildcardExpression {
public:
    explicit WildcardExpression(std::string processed_search_string);

    [[nodiscard]] auto substr(size_t const begin_idx, size_t const length) const -> std::string {
        return m_processed_search_string.substr(begin_idx, length);
    }

    [[nodiscard]] auto length() const -> size_t { return m_processed_search_string.size(); }

    [[nodiscard]] auto char_is_greedy_wildcard(size_t const idx) const -> bool {
        return m_is_greedy_wildcard[idx];
    }

    [[nodiscard]] auto char_is_non_greedy_wildcard(size_t const idx) const -> bool {
        return m_is_non_greedy_wildcard[idx];
    }

    [[nodiscard]] auto char_is_escape(size_t const idx) const -> bool { return m_is_escape[idx]; }

    [[nodiscard]] auto get_char(size_t const idx) const -> char {
        return m_processed_search_string[idx];
    }

private:
    std::vector<bool> m_is_greedy_wildcard;
    std::vector<bool> m_is_non_greedy_wildcard;
    std::vector<bool> m_is_escape;
    std::string m_processed_search_string;
};

/**
 * A view of a WildcardExpression.
 */
class WildcardExpressionView {
public:
    /**
     * Creates a view of the range [begin_idx, end_idx) in the given wildcard expression.
     *
     * NOTE: To ensure validity, end_idx is limited to wildcard_expression.length(), and then
     * begin_idx is limited to end_idx.
     * @param wildcard_expression
     * @param begin_idx
     * @param end_idx
     */
    WildcardExpressionView(
            WildcardExpression const& wildcard_expression,
            size_t begin_idx,
            size_t end_idx
    );

    /**
     * @return A copy of this view, but extended to include adjacent greedy wildcards.
     */
    [[nodiscard]] auto extend_to_adjacent_greedy_wildcards() const -> WildcardExpressionView;

    [[nodiscard]] auto is_greedy_wildcard() const -> bool {
        return 1 == length() && m_expression->char_is_greedy_wildcard(m_begin_idx);
    }

    [[nodiscard]] auto is_non_greedy_wildcard() const -> bool {
        return 1 == length() && m_expression->char_is_non_greedy_wildcard(m_begin_idx);
    }

    [[nodiscard]] auto starts_or_ends_with_greedy_wildcard() const -> bool {
        return length() > 0
               && (m_expression->char_is_greedy_wildcard(m_begin_idx)
                   || m_expression->char_is_greedy_wildcard(m_end_idx - 1));
    }

    /**
     * @param lexer
     * @return Whether the substring in view is surrounded by delimiters or unescaped wildcards.
     * NOTE: This method assumes that the viewed string is preceded and succeeded by a delimiter.
     */
    [[nodiscard]] auto surrounded_by_delims_or_wildcards(log_surgeon::lexers::ByteLexer const& lexer
    ) const -> bool;

    [[nodiscard]] auto length() const -> size_t { return m_end_idx - m_begin_idx; }

    [[nodiscard]] auto char_is_greedy_wildcard(size_t const idx) const -> bool {
        return m_expression->char_is_greedy_wildcard(m_begin_idx + idx);
    }

    [[nodiscard]] auto char_is_non_greedy_wildcard(size_t const idx) const -> bool {
        return m_expression->char_is_non_greedy_wildcard(m_begin_idx + idx);
    }

    [[nodiscard]] auto char_is_escape(size_t const idx) const -> bool {
        return m_expression->char_is_escape(m_begin_idx + idx);
    }

    [[nodiscard]] auto get_char(size_t const idx) const -> char {
        return m_expression->get_char(m_begin_idx + idx);
    }

    [[nodiscard]] auto get_value() const -> std::string {
        return m_expression->substr(m_begin_idx, m_end_idx - m_begin_idx);
    }

private:
    WildcardExpression const* m_expression;
    size_t m_begin_idx;
    size_t m_end_idx;
};
}  // namespace clp

#endif  // CLP_WILDCARDEXPRESSION_HPP
