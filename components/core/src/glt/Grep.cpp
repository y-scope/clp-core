#include "Grep.hpp"

#include <algorithm>

#include <string_utils/string_utils.hpp>

#include "EncodedVariableInterpreter.hpp"
#include "ir/parsing.hpp"
#include "ir/types.hpp"
#include "StringReader.hpp"
#include "Utils.hpp"

using clp::string_utils::clean_up_wildcard_search_string;
using clp::string_utils::is_alphabet;
using clp::string_utils::is_wildcard;
using clp::string_utils::wildcard_match_unsafe;
using glt::ir::is_delim;
using glt::streaming_archive::reader::Archive;
using glt::streaming_archive::reader::File;
using glt::streaming_archive::reader::Message;
using std::make_pair;
using std::pair;
using std::string;
using std::vector;

namespace glt {
namespace {
// Local types
enum class SubQueryMatchabilityResult {
    MayMatch,  // The subquery might match a message
    WontMatch,  // The subquery has no chance of matching a message
    SupercedesAllSubQueries  // The subquery will cause all messages to be matched
};

// Class representing a token in a query. It is used to interpret a token in user's search string.
class QueryToken {
public:
    // Constructors
    QueryToken(string const& query_string, size_t begin_pos, size_t end_pos, bool is_var);

    // Methods
    bool cannot_convert_to_non_dict_var() const;
    bool contains_wildcards() const;
    bool has_greedy_wildcard_in_middle() const;
    bool has_prefix_greedy_wildcard() const;
    bool has_suffix_greedy_wildcard() const;
    bool is_ambiguous_token() const;
    bool is_float_var() const;
    bool is_int_var() const;
    bool is_var() const;
    bool is_wildcard() const;

    size_t get_begin_pos() const;
    size_t get_end_pos() const;
    string const& get_value() const;

    bool change_to_next_possible_type();

private:
    // Types
    // Type for the purpose of generating different subqueries. E.g., if a token is of type
    // DictOrIntVar, it would generate a different subquery than if it was of type Logtype.
    enum class Type {
        Wildcard,
        // Ambiguous indicates the token can be more than one of the types listed below
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
    string m_value;

    // Type if variable has unambiguous type
    Type m_type;
    // Types if variable type is ambiguous
    vector<Type> m_possible_types;
    // Index of the current possible type selected for generating a subquery
    size_t m_current_possible_type_ix;
};

QueryToken::QueryToken(
        string const& query_string,
        size_t const begin_pos,
        size_t const end_pos,
        bool const is_var
)
        : m_current_possible_type_ix(0) {
    m_begin_pos = begin_pos;
    m_end_pos = end_pos;
    m_value.assign(query_string, m_begin_pos, m_end_pos - m_begin_pos);

    // Set wildcard booleans and determine type
    if ("*" == m_value) {
        m_has_prefix_greedy_wildcard = true;
        m_has_suffix_greedy_wildcard = false;
        m_has_greedy_wildcard_in_middle = false;
        m_contains_wildcards = true;
        m_type = Type::Wildcard;
    } else {
        m_has_prefix_greedy_wildcard = ('*' == m_value[0]);
        m_has_suffix_greedy_wildcard = ('*' == m_value[m_value.length() - 1]);

        m_has_greedy_wildcard_in_middle = false;
        for (size_t i = 1; i < m_value.length() - 1; ++i) {
            if ('*' == m_value[i]) {
                m_has_greedy_wildcard_in_middle = true;
                break;
            }
        }

        m_contains_wildcards
                = (m_has_prefix_greedy_wildcard || m_has_suffix_greedy_wildcard
                   || m_has_greedy_wildcard_in_middle);

        if (!is_var) {
            if (!m_contains_wildcards) {
                m_type = Type::Logtype;
            } else {
                m_type = Type::Ambiguous;
                m_possible_types.push_back(Type::Logtype);
                m_possible_types.push_back(Type::IntVar);
                m_possible_types.push_back(Type::FloatVar);
                m_possible_types.push_back(Type::DictionaryVar);
            }
        } else {
            string value_without_wildcards = m_value;
            if (m_has_prefix_greedy_wildcard) {
                value_without_wildcards = value_without_wildcards.substr(1);
            }
            if (m_has_suffix_greedy_wildcard) {
                value_without_wildcards.resize(value_without_wildcards.length() - 1);
            }

            encoded_variable_t encoded_var;
            bool converts_to_non_dict_var = false;
            bool converts_to_int
                    = EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                            value_without_wildcards,
                            encoded_var
                    );
            bool converts_to_float = false;
            if (!converts_to_int) {
                converts_to_float
                        = EncodedVariableInterpreter::convert_string_to_representable_float_var(
                                value_without_wildcards,
                                encoded_var
                        );
            }
            if (converts_to_int || converts_to_float) {
                converts_to_non_dict_var = true;
            }
            if (!converts_to_non_dict_var) {
                // GLT TODO
                // Actually this is incorrect, because it's possible user enters 23412*34 aiming to
                // match 23412.34. we should consider the possibility that middle wildcard causes
                // the converts_to_non_dict_var to be false.
                m_type = Type::DictionaryVar;
                m_cannot_convert_to_non_dict_var = true;
            } else {
                m_type = Type::Ambiguous;
                m_possible_types.push_back(Type::IntVar);
                m_possible_types.push_back(Type::FloatVar);
                m_possible_types.push_back(Type::DictionaryVar);
                m_cannot_convert_to_non_dict_var = false;
            }
        }
    }
}

bool QueryToken::cannot_convert_to_non_dict_var() const {
    return m_cannot_convert_to_non_dict_var;
}

bool QueryToken::contains_wildcards() const {
    return m_contains_wildcards;
}

bool QueryToken::has_greedy_wildcard_in_middle() const {
    return m_has_greedy_wildcard_in_middle;
}

bool QueryToken::has_prefix_greedy_wildcard() const {
    return m_has_prefix_greedy_wildcard;
}

bool QueryToken::has_suffix_greedy_wildcard() const {
    return m_has_suffix_greedy_wildcard;
}

bool QueryToken::is_ambiguous_token() const {
    return Type::Ambiguous == m_type;
}

bool QueryToken::is_float_var() const {
    Type type;
    if (Type::Ambiguous == m_type) {
        type = m_possible_types[m_current_possible_type_ix];
    } else {
        type = m_type;
    }
    return Type::FloatVar == type;
}

bool QueryToken::is_int_var() const {
    Type type;
    if (Type::Ambiguous == m_type) {
        type = m_possible_types[m_current_possible_type_ix];
    } else {
        type = m_type;
    }
    return Type::IntVar == type;
}

bool QueryToken::is_var() const {
    Type type;
    if (Type::Ambiguous == m_type) {
        type = m_possible_types[m_current_possible_type_ix];
    } else {
        type = m_type;
    }
    return (Type::IntVar == type || Type::FloatVar == type || Type::DictionaryVar == type);
}

bool QueryToken::is_wildcard() const {
    return Type::Wildcard == m_type;
}

size_t QueryToken::get_begin_pos() const {
    return m_begin_pos;
}

size_t QueryToken::get_end_pos() const {
    return m_end_pos;
}

string const& QueryToken::get_value() const {
    return m_value;
}

bool QueryToken::change_to_next_possible_type() {
    if (m_current_possible_type_ix < m_possible_types.size() - 1) {
        ++m_current_possible_type_ix;
        return true;
    } else {
        m_current_possible_type_ix = 0;
        return false;
    }
}

// Local prototypes
/**
 * Process a QueryToken that is definitely a variable
 * @param query_token
 * @param archive
 * @param ignore_case
 * @param sub_query
 * @param logtype
 * @return true if this token might match a message, false otherwise
 */
bool process_var_token(
        QueryToken const& query_token,
        Archive const& archive,
        bool ignore_case,
        SubQuery& sub_query,
        string& logtype
);
/**
 * Finds a message matching the given query
 * @param query
 * @param archive
 * @param matching_sub_query
 * @param compressed_file
 * @param compressed_msg
 * @return true on success, false otherwise
 */
bool find_matching_message(
        Query const& query,
        Archive& archive,
        SubQuery const*& matching_sub_query,
        File& compressed_file,
        Message& compressed_msg
);
/**
 * Generates logtypes and variables for subquery
 * @param archive
 * @param processed_search_string
 * @param query_tokens
 * @param ignore_case
 * @param sub_query
 * @return SubQueryMatchabilityResult::SupercedesAllSubQueries
 * @return SubQueryMatchabilityResult::WontMatch
 * @return SubQueryMatchabilityResult::MayMatch
 */
SubQueryMatchabilityResult generate_logtypes_and_vars_for_subquery(
        Archive const& archive,
        string& processed_search_string,
        vector<QueryToken>& query_tokens,
        bool ignore_case,
        SubQuery& sub_query
);

bool process_var_token(
        QueryToken const& query_token,
        Archive const& archive,
        bool ignore_case,
        SubQuery& sub_query,
        string& logtype
) {
    // Even though we may have a precise variable, we still fallback to decompressing to ensure that
    // it is in the right place in the message
    sub_query.mark_wildcard_match_required();

    // Create QueryVar corresponding to token
    if (!query_token.contains_wildcards()) {
        if (EncodedVariableInterpreter::encode_and_search_dictionary(
                    query_token.get_value(),
                    archive.get_var_dictionary(),
                    ignore_case,
                    logtype,
                    sub_query
            )
            == false)
        {
            // Variable doesn't exist in dictionary
            return false;
        }
    } else {
        if (query_token.has_prefix_greedy_wildcard()) {
            logtype += '*';
        }

        if (query_token.is_float_var()) {
            LogTypeDictionaryEntry::add_float_var(logtype);
        } else if (query_token.is_int_var()) {
            LogTypeDictionaryEntry::add_int_var(logtype);
        } else {
            LogTypeDictionaryEntry::add_dict_var(logtype);

            if (query_token.cannot_convert_to_non_dict_var()) {
                // Must be a dictionary variable, so search variable dictionary
                if (!EncodedVariableInterpreter::wildcard_search_dictionary_and_get_encoded_matches(
                            query_token.get_value(),
                            archive.get_var_dictionary(),
                            ignore_case,
                            sub_query
                    ))
                {
                    // Variable doesn't exist in dictionary
                    return false;
                }
            }
        }

        if (query_token.has_suffix_greedy_wildcard()) {
            logtype += '*';
        }
    }

    return true;
}

bool find_matching_message(
        Query const& query,
        Archive& archive,
        SubQuery const*& matching_sub_query,
        File& compressed_file,
        Message& compressed_msg
) {
    if (query.contains_sub_queries()) {
        return false;
    } else if ((query.get_search_begin_timestamp() > cEpochTimeMin
                || query.get_search_end_timestamp() < cEpochTimeMax))
    {
        // TODO: remove
        return false;
    } else {
        bool read_successful = archive.get_next_message(compressed_file, compressed_msg);
        if (!read_successful) {
            return false;
        }
    }

    return true;
}

void find_boundaries(
        LogTypeDictionaryEntry const* logtype_entry,
        vector<pair<string, bool>> const& tokens,
        size_t& var_begin_ix,
        size_t& var_end_ix
) {
    auto const& logtype_string = logtype_entry->get_value();
    // left boundary is exclusive and right boundary are inclusive, meaning
    // that logtype_string.substr[0, left_boundary) and logtype_string.substr[right_boundary, end)
    // can be safely ignored.
    // They are initialized assuming that the entire logtype can be safely ignored. So if the
    // tokens doesn't contain variable. the behavior is consistent.
    size_t left_boundary{logtype_string.length()};
    size_t right_boundary{0};
    // First, match the token from front to end.
    size_t find_start_index{0};
    bool tokens_contain_variable{false};
    for (auto const& token : tokens) {
        auto const& token_str = token.first;
        bool contains_variable = token.second;
        size_t found_index = logtype_string.find(token_str, find_start_index);
        if (string::npos == found_index) {
            printf("failed to find: [%s] from %s\n",
                   token_str.c_str(),
                   logtype_string.substr(find_start_index).c_str());
            throw;
        }
        // the first time we see a token with variable, we know that
        //  we don't care about the variables in the substr before this token in the logtype.
        //  Technically, logtype_string.substr[0, token[begin_index])
        //  (since token[begin_index] is the beginning of the token)
        if (contains_variable) {
            tokens_contain_variable = true;
            left_boundary = found_index;
            break;
        }
        // else, the token doesn't contain a variable
        // we can proceed by skipping this token.
        find_start_index = found_index + token_str.length();
    }

    // second, match the token from back
    size_t rfind_end_index = logtype_string.length();
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        auto const& token_str = it->first;
        bool contains_var = it->second;

        size_t rfound_index = logtype_string.rfind(token_str, rfind_end_index);
        if (string::npos == rfound_index) {
            printf("failed to find: [%s] from %s\n",
                   token_str.c_str(),
                   logtype_string.substr(0, rfind_end_index).c_str());
            throw;
        }

        // the first time we see a token with variable, we know that
        // we don't care about the variables in the substr after this token in the logtype.
        // Technically, logtype_string.substr[rfound_index + len(token), end)
        // since logtype_string[rfound_index] is the beginning of the token
        if (contains_var) {
            tokens_contain_variable = true;
            right_boundary = rfound_index + token_str.length();
            break;
        }

        // Note, rfind end index is inclusive. has to subtract by 1 so
        // in the next rfind, we skip the token we have already seen.
        rfind_end_index = rfound_index - 1;
    }

    // if we didn't find any variable, we can do an early return
    if (false == tokens_contain_variable) {
        var_begin_ix = logtype_entry->get_num_variables();
        var_end_ix = 0;
        return;
    }

    // Now we have the left boundary and right boundary, try to filter out the variables;
    // var_begin_ix is an inclusive interval
    auto const logtype_variable_num = logtype_entry->get_num_variables();
    ir::VariablePlaceholder var_placeholder;
    var_begin_ix = 0;
    for (size_t var_ix = 0; var_ix < logtype_variable_num; var_ix++) {
        size_t var_position = logtype_entry->get_variable_info(var_ix, var_placeholder);
        if (var_position < left_boundary) {
            // if the variable is within the left boundary, then it should be skipped.
            var_begin_ix++;
        } else {
            // if the variable is not within the left boundary
            break;
        }
    }

    // For right boundary, var_end_ix is an exclusive interval
    var_end_ix = logtype_variable_num;
    for (size_t var_ix = 0; var_ix < logtype_variable_num; var_ix++) {
        size_t reversed_ix = logtype_variable_num - 1 - var_ix;
        size_t var_position = logtype_entry->get_variable_info(reversed_ix, var_placeholder);
        if (var_position >= right_boundary) {
            // if the variable is within the right boundary, then it should be skipped.
            var_end_ix--;
        } else {
            // if the variable is not within the right boundary
            break;
        }
    }

    if (var_end_ix <= var_begin_ix) {
        printf("tokens contain a variable, end index %lu is smaller and equal than begin index "
               "%lu\n",
               var_end_ix,
               var_begin_ix);
        throw;
    }
}

template <typename EscapeDecoder>
vector<pair<string, bool>>
retokenization(std::string_view input_string, EscapeDecoder escape_decoder) {
    vector<pair<string, bool>> retokenized_tokens;
    size_t input_length = input_string.size();
    string current_token;
    bool contains_variable_placeholder = false;
    for (size_t ix = 0; ix < input_length; ix++) {
        auto const current_char = input_string.at(ix);
        if (enum_to_underlying_type(ir::VariablePlaceholder::Escape) == current_char) {
            escape_decoder(input_string, ix, current_token);
            continue;
        }

        if (current_char != '*') {
            current_token += current_char;
            contains_variable_placeholder |= ir::is_variable_placeholder(current_char);
        } else {
            if (!current_token.empty()) {
                retokenized_tokens.emplace_back(current_token, contains_variable_placeholder);
                current_token.clear();
            }
        }
    }
    if (!current_token.empty()) {
        retokenized_tokens.emplace_back(current_token, contains_variable_placeholder);
    }
    return retokenized_tokens;
}

SubQueryMatchabilityResult generate_logtypes_and_vars_for_subquery(
        Archive const& archive,
        string& processed_search_string,
        vector<QueryToken>& query_tokens,
        bool ignore_case,
        SubQuery& sub_query
) {
    size_t last_token_end_pos = 0;
    string logtype;
    auto escape_handler
            = [](std::string_view constant, size_t char_to_escape_pos, string& logtype) -> void {
        auto const escape_char{enum_to_underlying_type(ir::VariablePlaceholder::Escape)};
        auto const next_char_pos{char_to_escape_pos + 1};
        // NOTE: We don't want to add additional escapes for wildcards that have been escaped. E.g.,
        // the query "\\*" should remain unchanged.
        if (next_char_pos < constant.length() && false == is_wildcard(constant[next_char_pos])) {
            logtype += escape_char;
        } else if (ir::is_variable_placeholder(constant[char_to_escape_pos])) {
            logtype += escape_char;
            logtype += escape_char;
        }
    };
    auto escape_decoder
            = [](std::string_view input_str, size_t& current_pos, string& token) -> void {
        auto const escape_char{enum_to_underlying_type(ir::VariablePlaceholder::Escape)};
        // Note: we don't need to do a check, because the upstream should guarantee all
        // escapes are followed by some characters
        auto const next_char = input_str.at(current_pos + 1);
        if (escape_char == next_char) {
            // turn two consecutive escape into a single one.
            token += escape_char;
        } else if (is_wildcard(next_char)) {
            // if it is an escape followed by a wildcard, we know no escape has been added.
            // we also remove the original escape because it was purely for query
            token += next_char;
        } else if (ir::is_variable_placeholder(next_char)) {
            // If we are at here, it means we are in the middle of processing a '\\\v' sequence
            // in this case, since we removed only one escape from the previous '\\' sequence
            // we need to remove another escape here.
            token += next_char;
        } else {
            printf("Unexpected\n");
            throw;
        }
        current_pos++;
    };

    for (auto const& query_token : query_tokens) {
        // Append from end of last token to beginning of this token, to logtype
        ir::append_constant_to_logtype(
                static_cast<std::string_view>(processed_search_string)
                        .substr(last_token_end_pos,
                                query_token.get_begin_pos() - last_token_end_pos),
                escape_handler,
                logtype
        );
        last_token_end_pos = query_token.get_end_pos();

        if (query_token.is_wildcard()) {
            logtype += '*';
        } else if (query_token.has_greedy_wildcard_in_middle()) {
            // Fallback to decompression + wildcard matching for now to avoid handling queries where
            // the pieces of the token on either side of each wildcard need to be processed as
            // ambiguous tokens
            sub_query.mark_wildcard_match_required();
            if (!query_token.is_var()) {
                // Must mean the token is text only, with * in it.
                logtype += '*';
            } else {
                logtype += '*';
                LogTypeDictionaryEntry::add_dict_var(logtype);
                logtype += '*';
            }
        } else {
            if (!query_token.is_var()) {
                ir::append_constant_to_logtype(query_token.get_value(), escape_handler, logtype);
            } else if (!process_var_token(query_token, archive, ignore_case, sub_query, logtype)) {
                return SubQueryMatchabilityResult::WontMatch;
            }
        }
    }

    if (last_token_end_pos < processed_search_string.length()) {
        // Append from end of last token to end
        ir::append_constant_to_logtype(
                static_cast<std::string_view>(processed_search_string)
                        .substr(last_token_end_pos, string::npos),
                escape_handler,
                logtype
        );
        last_token_end_pos = processed_search_string.length();
    }

    if ("*" == logtype) {
        // Logtype will match all messages
        return SubQueryMatchabilityResult::SupercedesAllSubQueries;
    }

    // Find matching logtypes
    std::unordered_set<LogTypeDictionaryEntry const*> possible_logtype_entries;
    archive.get_logtype_dictionary()
            .get_entries_matching_wildcard_string(logtype, ignore_case, possible_logtype_entries);
    if (possible_logtype_entries.empty()) {
        return SubQueryMatchabilityResult::WontMatch;
    }

    // Find boundaries
    auto const retokenized_tokens = retokenization(logtype, escape_decoder);
    for (auto const& logtype_entry : possible_logtype_entries) {
        size_t var_begin_index;
        size_t var_end_index;
        find_boundaries(logtype_entry, retokenized_tokens, var_begin_index, var_end_index);
        sub_query.set_logtype_boundary(logtype_entry->get_id(), var_begin_index, var_end_index);
    }
    sub_query.set_possible_logtypes(possible_logtype_entries);

    // Calculate the IDs of the segments that may contain results for the sub-query now that we've
    // calculated the matching logtypes and variables
    sub_query.calculate_ids_of_matching_segments();

    return SubQueryMatchabilityResult::MayMatch;
}
}  // namespace

std::optional<Query> Grep::process_raw_query(
        Archive const& archive,
        string const& search_string,
        epochtime_t search_begin_ts,
        epochtime_t search_end_ts,
        bool ignore_case
) {
    // Add prefix and suffix '*' to make the search a sub-string match
    string processed_search_string = "*";
    processed_search_string += search_string;
    processed_search_string += '*';
    processed_search_string = clean_up_wildcard_search_string(processed_search_string);

    // Split search_string into tokens with wildcards
    vector<QueryToken> query_tokens;
    size_t begin_pos = 0;
    size_t end_pos = 0;
    bool is_var;
    string search_string_for_sub_queries{processed_search_string};

    // Replace '?' wildcards with '*' wildcards since we currently have no support for
    // generating sub-queries with '?' wildcards. The final wildcard match on the decompressed
    // message uses the original wildcards, so correctness will be maintained.
    std::replace(
            search_string_for_sub_queries.begin(),
            search_string_for_sub_queries.end(),
            '?',
            '*'
    );
    // Clean-up in case any instances of "?*" or "*?" were changed into "**"
    search_string_for_sub_queries = clean_up_wildcard_search_string(search_string_for_sub_queries);
    while (get_bounds_of_next_potential_var(
            search_string_for_sub_queries,
            begin_pos,
            end_pos,
            is_var
    ))
    {
        query_tokens.emplace_back(search_string_for_sub_queries, begin_pos, end_pos, is_var);
    }

    // Get pointers to all ambiguous tokens. Exclude tokens with wildcards in the middle since we
    // fall-back to decompression + wildcard matching for those.
    vector<QueryToken*> ambiguous_tokens;
    for (auto& query_token : query_tokens) {
        if (!query_token.has_greedy_wildcard_in_middle() && query_token.is_ambiguous_token()) {
            ambiguous_tokens.push_back(&query_token);
        }
    }

    // Generate a sub-query for each combination of ambiguous tokens
    // E.g., if there are two ambiguous tokens each of which could be a logtype or variable, we need
    // to create:
    // - (token1 as logtype) (token2 as logtype)
    // - (token1 as logtype) (token2 as var)
    // - (token1 as var) (token2 as logtype)
    // - (token1 as var) (token2 as var)
    vector<SubQuery> sub_queries;
    string logtype;
    bool type_of_one_token_changed = true;
    while (type_of_one_token_changed) {
        SubQuery sub_query;

        // Compute logtypes and variables for query
        auto matchability = generate_logtypes_and_vars_for_subquery(
                archive,
                search_string_for_sub_queries,
                query_tokens,
                ignore_case,
                sub_query
        );
        switch (matchability) {
            case SubQueryMatchabilityResult::SupercedesAllSubQueries:
                // Since other sub-queries will be superceded by this one, we can stop processing
                // now
                return Query{
                        search_begin_ts,
                        search_end_ts,
                        ignore_case,
                        processed_search_string,
                        {}
                };
            case SubQueryMatchabilityResult::MayMatch:
                sub_queries.push_back(std::move(sub_query));
                break;
            case SubQueryMatchabilityResult::WontMatch:
            default:
                // Do nothing
                break;
        }

        // Update combination of ambiguous tokens
        type_of_one_token_changed = false;
        for (auto* ambiguous_token : ambiguous_tokens) {
            if (ambiguous_token->change_to_next_possible_type()) {
                type_of_one_token_changed = true;
                break;
            }
        }
    }

    if (sub_queries.empty()) {
        return std::nullopt;
    }

    return Query{
            search_begin_ts,
            search_end_ts,
            ignore_case,
            processed_search_string,
            std::move(sub_queries)
    };
}

bool Grep::get_bounds_of_next_potential_var(
        string const& value,
        size_t& begin_pos,
        size_t& end_pos,
        bool& is_var
) {
    auto const value_length = value.length();
    if (end_pos >= value_length) {
        return false;
    }

    is_var = false;
    bool contains_wildcard = false;
    while (false == is_var && false == contains_wildcard && begin_pos < value_length) {
        // Start search at end of last token
        begin_pos = end_pos;

        // Find next wildcard or non-delimiter
        bool is_escaped = false;
        for (; begin_pos < value_length; ++begin_pos) {
            char c = value[begin_pos];

            if (is_escaped) {
                is_escaped = false;

                if (false == is_delim(c)) {
                    // Found escaped non-delimiter, so reverse the index to retain the escape
                    // character
                    --begin_pos;
                    break;
                }
            } else if ('\\' == c) {
                // Escape character
                is_escaped = true;
            } else {
                if (is_wildcard(c)) {
                    contains_wildcard = true;
                    break;
                }
                if (false == is_delim(c)) {
                    break;
                }
            }
        }

        bool contains_decimal_digit = false;
        bool contains_alphabet = false;

        // Find next delimiter
        is_escaped = false;
        end_pos = begin_pos;
        for (; end_pos < value_length; ++end_pos) {
            char c = value[end_pos];

            if (is_escaped) {
                is_escaped = false;

                if (is_delim(c)) {
                    // Found escaped delimiter, so reverse the index to retain the escape character
                    --end_pos;
                    break;
                }
            } else if ('\\' == c) {
                // Escape character
                is_escaped = true;
            } else {
                if (is_wildcard(c)) {
                    contains_wildcard = true;
                } else if (is_delim(c)) {
                    // Found delimiter that's not also a wildcard
                    break;
                }
            }

            if (clp::string_utils::is_decimal_digit(c)) {
                contains_decimal_digit = true;
            } else if (is_alphabet(c)) {
                contains_alphabet = true;
            }
        }

        // Treat token as a definite variable if:
        // - it contains a decimal digit, or
        // - it could be a multi-digit hex value, or
        // - it's directly preceded by an equals sign and contains an alphabet without a wildcard
        //   between the equals sign and the first alphabet of the token
        auto variable = static_cast<std::string_view>(value).substr(begin_pos, end_pos - begin_pos);
        if (contains_decimal_digit || ir::could_be_multi_digit_hex_value(variable)) {
            is_var = true;
        } else if (begin_pos > 0 && '=' == value[begin_pos - 1] && contains_alphabet) {
            // Find first alphabet or wildcard in token
            is_escaped = false;
            bool found_wildcard_before_alphabet = false;
            for (auto i = begin_pos; i < end_pos; ++i) {
                auto c = value[i];

                if (is_escaped) {
                    is_escaped = false;

                    if (is_alphabet(c)) {
                        break;
                    }
                } else if ('\\' == c) {
                    // Escape character
                    is_escaped = true;
                } else if (is_wildcard(c)) {
                    found_wildcard_before_alphabet = true;
                    break;
                }
            }

            if (false == found_wildcard_before_alphabet) {
                is_var = true;
            }
        }
    }

    return (value_length != begin_pos);
}

void
Grep::calculate_sub_queries_relevant_to_file(File const& compressed_file, vector<Query>& queries) {
    for (auto& query : queries) {
        query.make_sub_queries_relevant_to_segment(compressed_file.get_segment_id());
    }
}

size_t Grep::search_and_output(
        Query const& query,
        size_t limit,
        Archive& archive,
        File& compressed_file,
        OutputFunc output_func,
        void* output_func_arg
) {
    size_t num_matches = 0;

    Message compressed_msg;
    string decompressed_msg;
    string const& orig_file_path = compressed_file.get_orig_path();
    while (num_matches < limit) {
        // Find matching message
        SubQuery const* matching_sub_query = nullptr;
        if (find_matching_message(
                    query,
                    archive,
                    matching_sub_query,
                    compressed_file,
                    compressed_msg
            )
            == false)
        {
            break;
        }

        // Decompress match
        bool decompress_successful
                = archive.decompress_message(compressed_file, compressed_msg, decompressed_msg);
        if (!decompress_successful) {
            break;
        }

        // Perform wildcard match if required
        // Check if:
        // - Sub-query requires wildcard match, or
        // - no subqueries exist and the search string is not a match-all
        if ((query.contains_sub_queries() && matching_sub_query->wildcard_match_required())
            || (query.contains_sub_queries() == false && query.search_string_matches_all() == false
            ))
        {
            bool matched = wildcard_match_unsafe(
                    decompressed_msg,
                    query.get_search_string(),
                    query.get_ignore_case() == false
            );
            if (!matched) {
                continue;
            }
        }

        // Print match
        output_func(orig_file_path, compressed_msg, decompressed_msg, output_func_arg);
        ++num_matches;
    }

    return num_matches;
}

bool Grep::search_and_decompress(
        Query const& query,
        Archive& archive,
        File& compressed_file,
        Message& compressed_msg,
        string& decompressed_msg
) {
    string const& orig_file_path = compressed_file.get_orig_path();

    bool matched = false;
    while (false == matched) {
        // Find matching message
        SubQuery const* matching_sub_query = nullptr;
        bool message_found = find_matching_message(
                query,
                archive,
                matching_sub_query,
                compressed_file,
                compressed_msg
        );
        if (false == message_found) {
            return false;
        }

        // Decompress match
        bool decompress_successful
                = archive.decompress_message(compressed_file, compressed_msg, decompressed_msg);
        if (false == decompress_successful) {
            return false;
        }

        // Perform wildcard match if required
        // Check if:
        // - Sub-query requires wildcard match, or
        // - no subqueries exist and the search string is not a match-all
        if ((query.contains_sub_queries() && matching_sub_query->wildcard_match_required())
            || (query.contains_sub_queries() == false && query.search_string_matches_all() == false
            ))
        {
            matched = wildcard_match_unsafe(
                    decompressed_msg,
                    query.get_search_string(),
                    query.get_ignore_case() == false
            );
        } else {
            matched = true;
        }
    }

    return true;
}

size_t Grep::search(Query const& query, size_t limit, Archive& archive, File& compressed_file) {
    size_t num_matches = 0;

    Message compressed_msg;
    string decompressed_msg;
    string const& orig_file_path = compressed_file.get_orig_path();
    while (num_matches < limit) {
        // Find matching message
        SubQuery const* matching_sub_query = nullptr;
        if (find_matching_message(
                    query,
                    archive,
                    matching_sub_query,
                    compressed_file,
                    compressed_msg
            )
            == false)
        {
            break;
        }

        // Perform wildcard match if required
        // Check if:
        // - Sub-query requires wildcard match, or
        // - no subqueries exist and the search string is not a match-all
        if ((query.contains_sub_queries() && matching_sub_query->wildcard_match_required())
            || (query.contains_sub_queries() == false && query.search_string_matches_all() == false
            ))
        {
            // Decompress match
            bool decompress_successful
                    = archive.decompress_message(compressed_file, compressed_msg, decompressed_msg);
            if (!decompress_successful) {
                break;
            }

            bool matched = wildcard_match_unsafe(
                    decompressed_msg,
                    query.get_search_string(),
                    query.get_ignore_case() == false
            );
            if (!matched) {
                continue;
            }
        }

        ++num_matches;
    }

    return num_matches;
}

std::unordered_map<logtype_dictionary_id_t, LogtypeQueries>
Grep::get_converted_logtype_query(Query const& query, size_t segment_id) {
    // use a map so that queries are ordered by ascending logtype_id
    std::unordered_map<logtype_dictionary_id_t, LogtypeQueries> converted_logtype_based_queries;
    auto const& relevant_subqueries = query.get_relevant_sub_queries();
    for (auto const& sub_query : relevant_subqueries) {
        // loop through all possible logtypes
        auto const& possible_log_entries = sub_query->get_possible_logtype_entries();
        for (auto const& possible_logtype_entry : possible_log_entries) {
            // create one LogtypeQuery for each logtype
            logtype_dictionary_id_t possible_logtype_id = possible_logtype_entry->get_id();
            auto const& boundary = sub_query->get_boundary_by_logtype_id(possible_logtype_id);
            LogtypeQuery query_info(
                    sub_query->get_vars(),
                    sub_query->wildcard_match_required(),
                    boundary
            );

            // The boundary is a range like [left:right). note it's open on the right side
            auto const& containing_segments
                    = possible_logtype_entry->get_ids_of_segments_containing_entry();
            if (containing_segments.find(segment_id) != containing_segments.end()) {
                if (converted_logtype_based_queries.find(possible_logtype_id)
                    == converted_logtype_based_queries.end())
                {
                    converted_logtype_based_queries[possible_logtype_id].set_logtype_id(
                            possible_logtype_id
                    );
                }
                converted_logtype_based_queries[possible_logtype_id].add_query(query_info);
            }
        }
    }
    return converted_logtype_based_queries;
}

// Handle the case where the processed search string is a wildcard (Note this doesn't guarantee the
// original search string is a wildcard) Return all messages as long as they fall into the time
// range
size_t Grep::output_message_in_segment_within_time_range(
        Query const& query,
        size_t limit,
        streaming_archive::reader::Archive& archive,
        OutputFunc output_func,
        void* output_func_arg
) {
    size_t num_matches = 0;

    Message compressed_msg;
    string decompressed_msg;

    // Get the correct order of looping through logtypes
    auto& logtype_table_manager = archive.get_logtype_table_manager();
    auto const& logtype_order = logtype_table_manager.get_single_order();
    for (auto const& logtype_id : logtype_order) {
        logtype_table_manager.open_logtype_table(logtype_id);
        logtype_table_manager.load_all();
        auto num_vars = archive.get_logtype_dictionary().get_entry(logtype_id).get_num_variables();
        compressed_msg.resize_var(num_vars);
        compressed_msg.set_logtype_id(logtype_id);
        while (num_matches < limit) {
            // Find matching message
            bool found_message = archive.get_next_message_in_logtype_table(compressed_msg);
            if (!found_message) {
                break;
            }
            if (!query.timestamp_is_in_search_time_range(compressed_msg.get_ts_in_milli())) {
                continue;
            }
            bool decompress_successful = archive.decompress_message_with_fixed_timestamp_pattern(
                    compressed_msg,
                    decompressed_msg
            );
            if (!decompress_successful) {
                break;
            }
            // Perform wildcard match if required
            // In this branch, subqueries should not exist
            // So just check if the search string is not a match-all
            if (query.search_string_matches_all() == false) {
                bool matched = wildcard_match_unsafe(
                        decompressed_msg,
                        query.get_search_string(),
                        query.get_ignore_case() == false
                );
                if (!matched) {
                    continue;
                }
            }
            std::string orig_file_path = archive.get_file_name(compressed_msg.get_file_id());
            // Print match
            output_func(orig_file_path, compressed_msg, decompressed_msg, output_func_arg);
            ++num_matches;
        }
        logtype_table_manager.close_logtype_table();
    }
    return num_matches;
}

size_t Grep::output_message_in_combined_segment_within_time_range(
        Query const& query,
        size_t limit,
        streaming_archive::reader::Archive& archive,
        OutputFunc output_func,
        void* output_func_arg
) {
    size_t num_matches = 0;

    Message compressed_msg;
    string decompressed_msg;
    auto& logtype_table_manager = archive.get_logtype_table_manager();
    size_t combined_table_count = logtype_table_manager.get_combined_table_count();
    auto const& combined_logtype_order = logtype_table_manager.get_combined_order();
    auto& combined_tables = logtype_table_manager.combined_tables();
    for (size_t table_ix = 0; table_ix < combined_table_count; table_ix++) {
        // load the combined table
        logtype_table_manager.open_combined_table(table_ix);
        auto const& logtype_order = combined_logtype_order.at(table_ix);

        for (auto const& logtype_id : logtype_order) {
            // load the logtype id
            logtype_table_manager.load_logtype_table_from_combine(logtype_id);
            auto num_vars
                    = archive.get_logtype_dictionary().get_entry(logtype_id).get_num_variables();
            compressed_msg.resize_var(num_vars);
            compressed_msg.set_logtype_id(logtype_id);
            while (num_matches < limit) {
                // Find matching message
                bool found_message = combined_tables.get_next_message(compressed_msg);
                if (!found_message) {
                    break;
                }
                if (!query.timestamp_is_in_search_time_range(compressed_msg.get_ts_in_milli())) {
                    continue;
                }
                bool decompress_successful
                        = archive.decompress_message_with_fixed_timestamp_pattern(
                                compressed_msg,
                                decompressed_msg
                        );
                if (!decompress_successful) {
                    break;
                }
                // Perform wildcard match if required
                // In this execution branch, subqueries should not exist
                // So just check if the search string is not a match-all
                if (query.search_string_matches_all() == false) {
                    bool matched = wildcard_match_unsafe(
                            decompressed_msg,
                            query.get_search_string(),
                            query.get_ignore_case() == false
                    );
                    if (!matched) {
                        continue;
                    }
                }
                std::string orig_file_path = archive.get_file_name(compressed_msg.get_file_id());
                // Print match
                output_func(orig_file_path, compressed_msg, decompressed_msg, output_func_arg);
                ++num_matches;
            }
            combined_tables.close_logtype_table();
        }
        logtype_table_manager.close_combined_table();
    }
    return num_matches;
}

size_t Grep::search_segment_and_output(
        std::vector<LogtypeQueries> const& queries,
        Query const& query,
        size_t limit,
        Archive& archive,
        OutputFunc output_func,
        void* output_func_arg
) {
    size_t num_matches = 0;

    Message compressed_msg;
    string decompressed_msg;

    // Go through each logtype
    for (auto const& query_for_logtype : queries) {
        size_t logtype_matches = 0;
        // preload the data
        auto logtype_id = query_for_logtype.get_logtype_id();
        auto const& sub_queries = query_for_logtype.get_queries();
        auto& logtype_table_manager = archive.get_logtype_table_manager();
        logtype_table_manager.open_logtype_table(logtype_id);
        logtype_table_manager.load_all();
        auto num_vars = archive.get_logtype_dictionary().get_entry(logtype_id).get_num_variables();
        compressed_msg.resize_var(num_vars);
        compressed_msg.set_logtype_id(logtype_id);

        while (num_matches < limit) {
            // Find matching message
            bool required_wild_card = false;
            bool found_matched = archive.find_message_matching_with_logtype_query(
                    sub_queries,
                    compressed_msg,
                    required_wild_card,
                    query
            );
            if (found_matched == false) {
                break;
            }
            // Decompress match
            bool decompress_successful = archive.decompress_message_with_fixed_timestamp_pattern(
                    compressed_msg,
                    decompressed_msg
            );
            if (!decompress_successful) {
                break;
            }

            // Perform wildcard match if required
            // Check if:
            // - Sub-query requires wildcard match, or
            // - no subqueries exist and the search string is not a match-all
            if ((query.contains_sub_queries() && required_wild_card)
                || (query.contains_sub_queries() == false
                    && query.search_string_matches_all() == false))
            {
                bool matched = wildcard_match_unsafe(
                        decompressed_msg,
                        query.get_search_string(),
                        query.get_ignore_case() == false
                );
                if (!matched) {
                    continue;
                }
            }
            std::string orig_file_path = archive.get_file_name(compressed_msg.get_file_id());
            // Print match
            output_func(orig_file_path, compressed_msg, decompressed_msg, output_func_arg);
            ++logtype_matches;
        }
        logtype_table_manager.close_logtype_table();
        num_matches += logtype_matches;
    }

    return num_matches;
}

size_t Grep::search_combined_table_and_output(
        combined_table_id_t table_id,
        std::vector<LogtypeQueries> const& queries,
        Query const& query,
        size_t limit,
        Archive& archive,
        OutputFunc output_func,
        void* output_func_arg
) {
    size_t num_matches = 0;

    Message compressed_msg;
    string decompressed_msg;
    auto& logtype_table_manager = archive.get_logtype_table_manager();
    logtype_table_manager.open_combined_table(table_id);
    for (auto const& iter : queries) {
        logtype_dictionary_id_t logtype_id = iter.get_logtype_id();
        logtype_table_manager.load_logtype_table_from_combine(logtype_id);

        auto const& queries_by_logtype = iter.get_queries();

        // Initialize message
        auto num_vars = archive.get_logtype_dictionary().get_entry(logtype_id).get_num_variables();
        compressed_msg.resize_var(num_vars);
        compressed_msg.set_logtype_id(logtype_id);

        size_t var_begin_ix = num_vars;
        size_t var_end_ix = 0;
        get_union_of_bounds(queries_by_logtype, var_begin_ix, var_end_ix);

        bool required_wild_card;
        while (num_matches < limit) {
            // Find matching message
            bool found_matched = archive.find_message_matching_with_logtype_query_from_combined(
                    queries_by_logtype,
                    compressed_msg,
                    required_wild_card,
                    query,
                    var_begin_ix,
                    var_end_ix
            );
            if (found_matched == false) {
                break;
            }
            // Decompress match
            bool decompress_successful = archive.decompress_message_with_fixed_timestamp_pattern(
                    compressed_msg,
                    decompressed_msg
            );
            if (!decompress_successful) {
                break;
            }

            // Perform wildcard match if required
            // Check if:
            // - Sub-query requires wildcard match, or
            // - no subqueries exist and the search string is not a match-all
            if ((query.contains_sub_queries() && required_wild_card)
                || (query.contains_sub_queries() == false
                    && query.search_string_matches_all() == false))
            {
                bool matched = wildcard_match_unsafe(
                        decompressed_msg,
                        query.get_search_string(),
                        query.get_ignore_case() == false
                );
                if (!matched) {
                    continue;
                }
            }
            std::string orig_file_path = archive.get_file_name(compressed_msg.get_file_id());
            // Print match
            output_func(orig_file_path, compressed_msg, decompressed_msg, output_func_arg);
            ++num_matches;
        }
        logtype_table_manager.combined_tables().close_logtype_table();
    }
    logtype_table_manager.close_combined_table();
    return num_matches;
}

size_t Grep::search_segment_optimized_and_output(
        std::vector<LogtypeQueries> const& queries,
        Query const& query,
        size_t limit,
        Archive& archive,
        OutputFunc output_func,
        void* output_func_arg
) {
    size_t num_matches = 0;

    Message compressed_msg;
    string decompressed_msg;

    // Go through each logtype
    auto& logtype_table_manager = archive.get_logtype_table_manager();
    for (auto const& query_for_logtype : queries) {
        // preload the data
        auto logtype_id = query_for_logtype.get_logtype_id();
        auto const& sub_queries = query_for_logtype.get_queries();
        logtype_table_manager.open_logtype_table(logtype_id);

        auto num_vars = archive.get_logtype_dictionary().get_entry(logtype_id).get_num_variables();

        size_t var_begin_ix = num_vars;
        size_t var_end_ix = 0;
        get_union_of_bounds(sub_queries, var_begin_ix, var_end_ix);

        // load timestamps and columns that fall into the ranges.
        logtype_table_manager.load_ts();
        logtype_table_manager.load_partial_columns(var_begin_ix, var_end_ix);

        std::vector<size_t> matched_row_ix;
        std::vector<bool> wildcard_required;
        // Find matching message
        archive.find_message_matching_with_logtype_query_optimized(
                sub_queries,
                matched_row_ix,
                wildcard_required,
                query
        );

        size_t num_potential_matches = matched_row_ix.size();
        if (num_potential_matches != 0) {
            // Decompress match
            std::vector<epochtime_t> loaded_ts(num_potential_matches);
            std::vector<file_id_t> loaded_file_id(num_potential_matches);
            std::vector<encoded_variable_t> loaded_vars(num_potential_matches * num_vars);
            logtype_table_manager.logtype_table().load_remaining_data_into_vec(
                    loaded_ts,
                    loaded_file_id,
                    loaded_vars,
                    matched_row_ix
            );
            num_matches += archive.decompress_messages_and_output(
                    logtype_id,
                    loaded_ts,
                    loaded_file_id,
                    loaded_vars,
                    wildcard_required,
                    query,
                    output_func,
                    output_func_arg
            );
        }
        logtype_table_manager.close_logtype_table();
    }

    return num_matches;
}

// we use a simple assumption atm.
// if subquery1 has range (a,b) and subquery2 has range (c,d).
// then the range will be (min(a,c), max(b,d)), even if c > b.
void Grep::get_union_of_bounds(
        std::vector<LogtypeQuery> const& sub_queries,
        size_t& var_begin_ix,
        size_t& var_end_ix
) {
    for (auto const& subquery : sub_queries) {
        // we use a simple assumption atm.
        // if subquery1 has range [begin1, end1) and subquery2 has range [begin2, end2).
        // then the range will be (min(begin1, begin2), max(end1, end2)).
        // Note, this would cause some inefficiency if begin1 < end1 < begin2 < end2.
        var_begin_ix = std::min(var_begin_ix, subquery.get_begin_ix());
        var_end_ix = std::max(var_end_ix, subquery.get_end_ix());
    }
}

}  // namespace glt
