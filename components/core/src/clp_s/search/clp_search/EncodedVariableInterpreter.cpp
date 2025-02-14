// Code from CLP

#include "EncodedVariableInterpreter.hpp"

#include <cassert>
#include <cmath>

#include <spdlog/spdlog.h>

#include "../../VariableEncoder.hpp"

using std::string;
using std::unordered_set;
using std::vector;

namespace clp_s::search::clp_search {
bool EncodedVariableInterpreter::encode_and_search_dictionary(
        string const& var_str,
        VariableDictionaryReader const& var_dict,
        bool ignore_case,
        string& logtype,
        SubQuery& sub_query
) {
    size_t length = var_str.length();
    if (0 == length) {
        throw OperationFailed(ErrorCodeBadParam, __FILENAME__, __LINE__);
    }

    encoded_variable_t encoded_var;
    if (VariableEncoder::convert_string_to_representable_integer_var(var_str, encoded_var)) {
        LogTypeDictionaryEntry::add_non_double_var(logtype);
        sub_query.add_non_dict_var(encoded_var);
    } else if (VariableEncoder::convert_string_to_representable_double_var(var_str, encoded_var)) {
        LogTypeDictionaryEntry::add_double_var(logtype);
        sub_query.add_non_dict_var(encoded_var);
    } else {
        auto const entries = var_dict.get_entry_matching_value(var_str, ignore_case);
        if (entries.empty()) {
            // Not in dictionary
            return false;
        }

        LogTypeDictionaryEntry::add_non_double_var(logtype);

        if (entries.size() == 1) {
            auto const* entry = entries.at(0);
            encoded_var = VariableEncoder::encode_var_dict_id(entry->get_id());
            sub_query.add_dict_var(encoded_var, entry);
            return true;
        }

        std::unordered_set<encoded_variable_t> encoded_vars;
        std::unordered_set<VariableDictionaryEntry const*> entries_set(
                entries.begin(),
                entries.end()
        );
        for (auto const entry : entries) {
            encoded_vars.insert(VariableEncoder::encode_var_dict_id(entry->get_id()));
        }
        sub_query.add_imprecise_dict_var(encoded_vars, entries_set);
    }

    return true;
}

bool EncodedVariableInterpreter::wildcard_search_dictionary_and_get_encoded_matches(
        std::string const& var_wildcard_str,
        VariableDictionaryReader const& var_dict,
        bool ignore_case,
        SubQuery& sub_query
) {
    // Find matches
    unordered_set<VariableDictionaryEntry const*> var_dict_entries;
    var_dict.get_entries_matching_wildcard_string(var_wildcard_str, ignore_case, var_dict_entries);
    if (var_dict_entries.empty()) {
        // Not in dictionary
        return false;
    }

    // Encode matches
    unordered_set<encoded_variable_t> encoded_vars;
    for (auto const* entry : var_dict_entries) {
        encoded_vars.insert(VariableEncoder::encode_var_dict_id(entry->get_id()));
    }

    sub_query.add_imprecise_dict_var(encoded_vars, var_dict_entries);

    return true;
}
}  // namespace clp_s::search::clp_search
