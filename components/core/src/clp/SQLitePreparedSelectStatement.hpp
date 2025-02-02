#ifndef CLP_SQLITEPREPAREDSELECTSTATEMENT_HPP
#define CLP_SQLITEPREPAREDSELECTSTATEMENT_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sqlite3/sqlite3.h>

#include "SQLitePreparedStatement.hpp"
#include "TraceableException.hpp"

/**
 * A SQLite `SELECT` prepared statement that maintains a mapping between each selected column and
 * its index, so that each column in the result set can be retrieved using its name.
 */
namespace clp {
class SQLitePreparedSelectStatement : public SQLitePreparedStatement {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(
                ErrorCode error_code,
                char const* const filename,
                int line_number,
                std::string msg
        )
                : TraceableException{error_code, filename, line_number},
                  m_msg{std::move(msg)} {}

        // Methods
        [[nodiscard]] auto what() const noexcept -> char const* override { return m_msg.c_str(); }

    private:
        std::string m_msg;
    };

    // Factory functions
    /**
     * Constructs a SQLite `SELECT` statement with the given parameters.
     * @param columns_to_select The names of the columns to select.
     * @param table The name of the table to select from.
     * @param where_clause_predicates Optional predicates for the `WHERE` clause.
     * @param sort_keys Optional sort keys for the `ORDER BY` clause. These may be in the form
     * "<column-name>" OR "<column-name> <ASC|DESC>".
     * @param db_handle
     * @return The SQLite statement.
     * @throw clp::SQLitePreparedStatement::OperationFailed on failure.
     */
    [[nodiscard]] static auto create_sqlite_prepared_select_statement(
            std::vector<std::string> const& columns_to_select,
            std::string_view table,
            std::vector<std::string> const& where_clause_predicates,
            std::vector<std::string> const& sort_keys,
            sqlite3* db_handle
    ) -> SQLitePreparedSelectStatement;

    // Constructors and assignment operators
    // Disable copy constructor and assignment operator
    SQLitePreparedSelectStatement(SQLitePreparedSelectStatement const&) = delete;
    auto operator=(SQLitePreparedSelectStatement const&) -> SQLitePreparedSelectStatement& = delete;

    // Use default move constructor and assignment operator
    SQLitePreparedSelectStatement(SQLitePreparedSelectStatement&& rhs) noexcept = default;
    auto operator=(SQLitePreparedSelectStatement&& rhs
    ) noexcept -> SQLitePreparedSelectStatement& = default;

    // Destructor
    ~SQLitePreparedSelectStatement() override = default;

    // Methods
    /**
     * @param column_name
     * @return The selected column's value as an int.
     * @throw OperationFailed if the name is not a selected column.
     */
    [[nodiscard]] auto column_int(std::string const& column_name) const -> int {
        return SQLitePreparedStatement::column_int(
                static_cast<int>(get_selected_column_idx(column_name))
        );
    }

    /**
     * @param column_name
     * @return The selected column's value as an int64.
     * @throw OperationFailed if the name is not a selected column.
     */
    [[nodiscard]] auto column_int64(std::string const& column_name) const -> int64_t {
        return SQLitePreparedStatement::column_int64(
                static_cast<int>(get_selected_column_idx(column_name))
        );
    }

    /**
     * @param selected_column
     * @param value Returns the selected column's value as a string.
     * @throw OperationFailed if the name is not a valid selected column.
     */
    auto column_string(std::string const& selected_column, std::string& value) const -> void {
        SQLitePreparedStatement::column_string(
                static_cast<int>(get_selected_column_idx(selected_column)),
                value
        );
    }

private:
    /**
     * @param statement
     * @param db_handle
     * @param selected_column_to_idx
     */
    explicit SQLitePreparedSelectStatement(
            std::string_view statement,
            sqlite3* db_handle,
            std::unordered_map<std::string, size_t> selected_column_to_idx
    )
            : SQLitePreparedStatement{statement.data(), statement.size(), db_handle},
              m_selected_column_to_idx{std::move(selected_column_to_idx)} {}

    /**
     * @param column_name
     * @return Index of the selected column in the statement.
     * @throw OperationFailed if the name is not a selected column.
     */
    [[nodiscard]] auto get_selected_column_idx(std::string const& column_name) const -> size_t;

    std::unordered_map<std::string, size_t> m_selected_column_to_idx;
};
}  // namespace clp

#endif
