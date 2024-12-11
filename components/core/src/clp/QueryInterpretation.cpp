#include "QueryInterpretation.hpp"

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <variant>

#include "Defs.h"
#include "EncodedVariableInterpreter.hpp"
#include "log_surgeon/Lexer.hpp"
#include "LogTypeDictionaryEntry.hpp"
#include "string_utils/string_utils.hpp"

using log_surgeon::lexers::ByteLexer;
using std::string;

namespace clp {
auto VariableQueryToken::operator<(VariableQueryToken const& rhs) const -> bool {
    if (m_variable_type < rhs.m_variable_type) {
        return true;
    }
    if (m_variable_type > rhs.m_variable_type) {
        return false;
    }
    if (m_query_substring < rhs.m_query_substring) {
        return true;
    }
    if (m_query_substring > rhs.m_query_substring) {
        return false;
    }
    if (m_has_wildcard != rhs.m_has_wildcard) {
        return rhs.m_has_wildcard;
    }
    if (m_is_encoded != rhs.m_is_encoded) {
        return rhs.m_is_encoded;
    }
    return false;
}

auto VariableQueryToken::operator>(VariableQueryToken const& rhs) const -> bool {
    if (m_variable_type > rhs.m_variable_type) {
        return true;
    }
    if (m_variable_type < rhs.m_variable_type) {
        return false;
    }
    if (m_query_substring > rhs.m_query_substring) {
        return true;
    }
    if (m_query_substring < rhs.m_query_substring) {
        return false;
    }
    if (m_has_wildcard != rhs.m_has_wildcard) {
        return m_has_wildcard;
    }
    if (m_is_encoded != rhs.m_is_encoded) {
        return m_is_encoded;
    }
    return false;
}

void QueryInterpretation::append_logtype(QueryInterpretation& suffix) {
    auto const& first_new_token = suffix.m_logtype[0];
    if (auto& prev_token = m_logtype.back();
        false == m_logtype.empty() && std::holds_alternative<StaticQueryToken>(prev_token)
        && false == suffix.m_logtype.empty()
        && std::holds_alternative<StaticQueryToken>(first_new_token))
    {
        std::get<StaticQueryToken>(prev_token).append(std::get<StaticQueryToken>(first_new_token));
        m_logtype.insert(m_logtype.end(), suffix.m_logtype.begin() + 1, suffix.m_logtype.end());
    } else {
        m_logtype.insert(m_logtype.end(), suffix.m_logtype.begin(), suffix.m_logtype.end());
    }
}

void QueryInterpretation::generate_logtype_string(ByteLexer& lexer) {
    // Convert each query logtype into a set of logtype strings. Logtype strings are used in the
    // sub query as they have the correct format for comparing against the archive. Also, a
    // single query logtype might represent multiple logtype strings. While static text converts
    // one-to-one, wildcard variables that may be encoded have different logtype strings when
    // comparing against the dictionary than they do when comparing against the segment.

    // Reserve size for m_logtype_string
    uint32_t logtype_string_size = 0;
    for (uint32_t i = 0; i < get_logtype_size(); i++) {
        if (auto const& logtype_token = get_logtype_token(i);
            std::holds_alternative<StaticQueryToken>(logtype_token))
        {
            logtype_string_size
                    += std::get<StaticQueryToken>(logtype_token).get_query_substring().size();
        } else {
            logtype_string_size++;
        }
    }
    m_logtype_string.reserve(logtype_string_size);

    for (uint32_t i = 0; i < get_logtype_size(); i++) {
        if (auto const& logtype_token = get_logtype_token(i);
            std::holds_alternative<StaticQueryToken>(logtype_token))
        {
            m_logtype_string += std::get<StaticQueryToken>(logtype_token).get_query_substring();
        } else {
            auto const& variable_token = std::get<VariableQueryToken>(logtype_token);
            auto const variable_type = variable_token.get_variable_type();
            auto const& raw_string = variable_token.get_query_substring();
            auto const is_encoded_with_wildcard = variable_token.get_is_encoded_with_wildcard();
            auto const var_has_wildcard = variable_token.get_has_wildcard();
            auto& schema_type = lexer.m_id_symbol[variable_type];
            encoded_variable_t encoded_var = 0;
            if (is_encoded_with_wildcard) {
                if (cIntVarName == schema_type) {
                    LogTypeDictionaryEntry::add_int_var(m_logtype_string);
                } else if (cFloatVarName == schema_type) {
                    LogTypeDictionaryEntry::add_float_var(m_logtype_string);
                }
            } else if (false == var_has_wildcard && cIntVarName == schema_type
                       && EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                               raw_string,
                               encoded_var
                       ))
            {
                LogTypeDictionaryEntry::add_int_var(m_logtype_string);
            } else if (false == var_has_wildcard && cFloatVarName == schema_type
                       && EncodedVariableInterpreter::convert_string_to_representable_float_var(
                               raw_string,
                               encoded_var
                       ))
            {
                LogTypeDictionaryEntry::add_float_var(m_logtype_string);
            } else {
                LogTypeDictionaryEntry::add_dict_var(m_logtype_string);
            }
        }
    }
}

auto QueryInterpretation::operator<(QueryInterpretation const& rhs) const -> bool {
    if (m_logtype.size() < rhs.m_logtype.size()) {
        return true;
    }
    if (m_logtype.size() > rhs.m_logtype.size()) {
        return false;
    }
    for (uint32_t i = 0; i < m_logtype.size(); i++) {
        if (m_logtype[i] < rhs.m_logtype[i]) {
            return true;
        }
        if (m_logtype[i] > rhs.m_logtype[i]) {
            return false;
        }
    }
    return false;
}

auto operator<<(std::ostream& os, QueryInterpretation const& query_logtype) -> std::ostream& {
    os << "logtype='";
    for (uint32_t idx = 0; idx < query_logtype.get_logtype_size(); idx++) {
        if (auto const& query_token = query_logtype.get_logtype_token(idx);
            std::holds_alternative<StaticQueryToken>(query_token))
        {
            os << std::get<StaticQueryToken>(query_token).get_query_substring();
        } else {
            auto const& variable_token = std::get<VariableQueryToken>(query_token);
            os << "<" << variable_token.get_variable_type() << ">("
               << variable_token.get_query_substring() << ")";
        }
    }
    os << "', has_wildcard='";
    for (uint32_t idx = 0; idx < query_logtype.get_logtype_size(); idx++) {
        if (auto const& query_token = query_logtype.get_logtype_token(idx);
            std::holds_alternative<StaticQueryToken>(query_token))
        {
            os << 0;
        } else {
            auto const& variable_token = std::get<VariableQueryToken>(query_token);
            os << variable_token.get_has_wildcard();
        }
    }
    os << "', is_encoded_with_wildcard='";
    for (uint32_t idx = 0; idx < query_logtype.get_logtype_size(); idx++) {
        if (auto const& query_token = query_logtype.get_logtype_token(idx);
            std::holds_alternative<StaticQueryToken>(query_token))
        {
            os << 0;
        } else {
            auto const& variable_token = std::get<VariableQueryToken>(query_token);
            os << variable_token.get_is_encoded_with_wildcard();
        }
    }
    os << "', logtype_string='" << query_logtype.get_logtype_string() << "'";
    return os;
}
}  // namespace clp
