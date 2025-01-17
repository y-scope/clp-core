#include "Query.hpp"

using std::set;
using std::string;
using std::unordered_set;

// Local function prototypes
/**
 * Performs a set intersection of a & b, storing the result in b
 * @tparam SetType
 * @param a
 * @param b
 */
template <typename SetType>
static void inplace_set_intersection(SetType const& a, SetType& b);

template <typename SetType>
static void inplace_set_intersection(SetType const& a, SetType& b) {
    for (auto ix = b.cbegin(); ix != b.cend();) {
        if (a.count(*ix) == 0) {
            ix = b.erase(ix);
        } else {
            ++ix;
        }
    }
}

namespace glt {
namespace {
bool matches_var(
        std::vector<encoded_variable_t> const& logtype_vars,
        std::vector<QueryVar> const& query_vars,
        size_t l,
        size_t r
) {
    if (logtype_vars.size() < query_vars.size()) {
        // Not enough variables to satisfy query
        return false;
    }

    // Try to find m_vars in vars, in order, but not necessarily contiguously
    size_t possible_vars_ix = 0;
    size_t const num_possible_vars = query_vars.size();
    size_t vars_ix = l;
    if (r == 0) {
        r = logtype_vars.size();
    }
    // const size_t num_vars = logtype_vars.size();
    while (possible_vars_ix < num_possible_vars && vars_ix < r) {
        QueryVar const& possible_var = query_vars[possible_vars_ix];

        if (possible_var.matches(logtype_vars[vars_ix])) {
            // Matched
            ++possible_vars_ix;
            ++vars_ix;
        } else {
            ++vars_ix;
        }
    }
    return (num_possible_vars == possible_vars_ix);
}
}  // unnamed namespace

QueryVar::QueryVar(encoded_variable_t precise_non_dict_var) {
    m_precise_var = precise_non_dict_var;
    m_is_precise_var = true;
    m_is_dict_var = false;
    m_var_dict_entry = nullptr;
}

QueryVar::QueryVar(
        encoded_variable_t precise_dict_var,
        VariableDictionaryEntry const* var_dict_entry
) {
    m_precise_var = precise_dict_var;
    m_is_precise_var = true;
    m_is_dict_var = true;
    m_var_dict_entry = var_dict_entry;
}

QueryVar::QueryVar(
        unordered_set<encoded_variable_t> const& possible_dict_vars,
        unordered_set<VariableDictionaryEntry const*> const& possible_var_dict_entries
) {
    m_is_dict_var = true;
    if (possible_dict_vars.size() == 1) {
        // A single possible variable is the same as a precise variable
        m_precise_var = *possible_dict_vars.cbegin();
        m_is_precise_var = true;
        m_var_dict_entry = *possible_var_dict_entries.cbegin();
    } else {
        m_possible_dict_vars = possible_dict_vars;
        m_is_precise_var = false;
        m_possible_var_dict_entries = possible_var_dict_entries;
    }
}

bool QueryVar::matches(encoded_variable_t var) const {
    return (m_is_precise_var && m_precise_var == var)
           || (!m_is_precise_var && m_possible_dict_vars.count(var) > 0);
}

void QueryVar::remove_segments_that_dont_contain_dict_var(set<segment_id_t>& segment_ids) const {
    if (false == m_is_dict_var) {
        // Not a dictionary variable, so do nothing
        return;
    }

    if (m_is_precise_var) {
        auto& ids_of_segments_containing_query_var
                = m_var_dict_entry->get_ids_of_segments_containing_entry();
        inplace_set_intersection(ids_of_segments_containing_query_var, segment_ids);
    } else {
        set<segment_id_t> ids_of_segments_containing_query_var;
        for (auto entry : m_possible_var_dict_entries) {
            auto& ids_of_segments_containing_var = entry->get_ids_of_segments_containing_entry();
            ids_of_segments_containing_query_var.insert(
                    ids_of_segments_containing_var.cbegin(),
                    ids_of_segments_containing_var.cend()
            );
        }
        inplace_set_intersection(ids_of_segments_containing_query_var, segment_ids);
    }
}

void SubQuery::add_non_dict_var(encoded_variable_t precise_non_dict_var) {
    m_vars.emplace_back(precise_non_dict_var);
}

void SubQuery::add_dict_var(
        encoded_variable_t precise_dict_var,
        VariableDictionaryEntry const* var_dict_entry
) {
    m_vars.emplace_back(precise_dict_var, var_dict_entry);
}

void SubQuery::add_imprecise_dict_var(
        unordered_set<encoded_variable_t> const& possible_dict_vars,
        unordered_set<VariableDictionaryEntry const*> const& possible_var_dict_entries
) {
    m_vars.emplace_back(possible_dict_vars, possible_var_dict_entries);
}

void SubQuery::set_possible_logtypes(
        unordered_set<LogTypeDictionaryEntry const*> const& logtype_entries
) {
    m_possible_logtype_ids.clear();
    for (auto entry : logtype_entries) {
        m_possible_logtype_ids.insert(entry->get_id());
    }
    m_possible_logtype_entries = logtype_entries;
}

void SubQuery::mark_wildcard_match_required() {
    m_wildcard_match_required = true;
}

void SubQuery::calculate_ids_of_matching_segments() {
    // Get IDs of segments containing logtypes
    m_ids_of_matching_segments.clear();
    for (auto entry : m_possible_logtype_entries) {
        auto& ids_of_segments_containing_logtype = entry->get_ids_of_segments_containing_entry();
        m_ids_of_matching_segments.insert(
                ids_of_segments_containing_logtype.cbegin(),
                ids_of_segments_containing_logtype.cend()
        );
    }

    // Intersect with IDs of segments containing variables
    for (auto& query_var : m_vars) {
        query_var.remove_segments_that_dont_contain_dict_var(m_ids_of_matching_segments);
    }
}

void SubQuery::clear() {
    m_vars.clear();
    m_possible_logtype_ids.clear();
    m_logtype_boundaries.clear();
    m_wildcard_match_required = false;
}

void SubQuery::set_logtype_boundary(
        glt::logtype_dictionary_id_t logtype_id,
        size_t var_begin_ix,
        size_t var_end_ix
) {
    m_logtype_boundaries.emplace(logtype_id, QueryBoundary(var_begin_ix, var_end_ix));
}

Query::Query(
        epochtime_t search_begin_timestamp,
        epochtime_t search_end_timestamp,
        bool ignore_case,
        std::string search_string,
        std::vector<SubQuery> sub_queries
)
        : m_search_begin_timestamp{search_begin_timestamp},
          m_search_end_timestamp{search_end_timestamp},
          m_ignore_case{ignore_case},
          m_search_string{std::move(search_string)},
          m_sub_queries{std::move(sub_queries)} {
    m_search_string_matches_all = (m_search_string.empty() || "*" == m_search_string);
}

void Query::make_sub_queries_relevant_to_segment(segment_id_t segment_id) {
    if (segment_id == m_prev_segment_id) {
        // Sub-queries already relevant to segment
        return;
    }

    // Make sub-queries relevant to segment
    m_relevant_sub_queries.clear();
    for (auto& sub_query : m_sub_queries) {
        if (sub_query.get_ids_of_matching_segments().count(segment_id)) {
            m_relevant_sub_queries.push_back(&sub_query);
        }
    }
    m_prev_segment_id = segment_id;
}

bool LogtypeQuery::matches_vars(std::vector<encoded_variable_t> const& vars) const {
    return matches_var(vars, m_vars, m_var_begin_ix, m_var_end_ix);
}
}  // namespace glt
