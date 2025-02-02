#include "SQLitePreparedSelectStatement.hpp"

#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fmt/core.h>
#include <fmt/fmt.h>
#include <sqlite3/sqlite3.h>

#include "database_utils.hpp"
#include "ErrorCode.hpp"
#include "SQLitePreparedStatement.hpp"

using std::string;
using std::vector;

namespace clp {
auto SQLitePreparedSelectStatement::create_sqlite_prepared_select_statement(
        vector<string> const& columns_to_select,
        std::string_view table,
        vector<string> const& where_clause_predicates,
        vector<string> const& sort_keys,
        sqlite3* db_handle
) -> SQLitePreparedSelectStatement {
    if (columns_to_select.empty()) {
        throw OperationFailed(
                ErrorCode_Failure,
                __FILENAME__,
                __LINE__,
                "clp::SQLitePreparedSelectStatement: No columns to select."
        );
    }

    std::unordered_map<string, size_t> selected_column_to_idx;
    fmt::memory_buffer statement_buffer;
    auto statement_buffer_ix{std::back_inserter(statement_buffer)};

    fmt::format_to(
            statement_buffer_ix,
            "SELECT {} FROM {}",
            get_field_names_sql(columns_to_select),
            table
    );
    size_t idx{0};
    for (auto const& column : columns_to_select) {
        selected_column_to_idx.emplace(column, idx++);
    }

    if (false == where_clause_predicates.empty()) {
        bool is_first{true};
        for (auto const& predicate : where_clause_predicates) {
            fmt::format_to(statement_buffer_ix, " {} {}", is_first ? "WHERE" : "AND", predicate);
            is_first = false;
        }
    }

    if (false == sort_keys.empty()) {
        fmt::format_to(statement_buffer_ix, " ORDER BY");
        bool is_first{true};
        for (auto const& sort_key : sort_keys) {
            if (is_first) {
                fmt::format_to(statement_buffer_ix, " {}", sort_key);
                is_first = false;
                continue;
            }
            fmt::format_to(statement_buffer_ix, ", {}", sort_key);
        }
    }

    return SQLitePreparedSelectStatement{
            {statement_buffer.data(), statement_buffer.size()},
            db_handle,
            selected_column_to_idx
    };
}

auto SQLitePreparedSelectStatement::get_selected_column_idx(string const& column_name
) const -> size_t {
    auto const idx_it{m_selected_column_to_idx.find(column_name)};
    if (m_selected_column_to_idx.cend() == idx_it) {
        string const msg{
                "clp::SQLitePreparedSelectStatement: " + column_name + " is not a selected column."
        };
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__, msg + column_name);
    }
    return idx_it->second;
}
}  // namespace clp
