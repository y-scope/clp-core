#ifndef CLP_SQLITEDB_HPP
#define CLP_SQLITEDB_HPP

#include <string>

#include <sqlite3/sqlite3.h>

#include "ErrorCode.hpp"
#include "SQLitePreparedSelectStatement.hpp"
#include "SQLitePreparedStatement.hpp"
#include "TraceableException.hpp"

namespace clp {
class SQLiteDB {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        char const* what() const noexcept override { return "SQLiteDB operation failed"; }
    };

    // Constructors
    SQLiteDB() : m_db_handle(nullptr) {}

    // Destructor
    ~SQLiteDB();

    // Methods
    void open(std::string const& path);
    bool close();

    SQLitePreparedStatement prepare_statement(char const* statement, size_t statement_length);

    SQLitePreparedStatement prepare_statement(std::string const& statement) {
        return prepare_statement(statement.c_str(), statement.length());
    }

    /**
     * Constructs and returns a SQLite select statement with given parameters.
     * @param columns_to_select The name of columns to select.
     * @param table The name of the table to select from.
     * @param where_clause SQLite `WHERE` clause to filter rows in the result set.
     * @param ordering_clause SQLite `ORDER BY` clause to sort the result set.
     * @return a new constructed select statement.
     * @throw clp::SQLiteDB::OperationFailed if the db is not initialized.
     * @throw clp::SQLitePreparedStatement::OperationFailed if it fails to create a new statement.
     */
    SQLitePreparedSelectStatement prepare_select_statement(
            std::vector<std::string> const& columns_to_select,
            std::string_view table,
            std::vector<std::string> where_clause,
            std::vector<std::string> ordering_clause
    );

    char const* get_error_message() { return sqlite3_errmsg(m_db_handle); }

private:
    // Variables
    sqlite3* m_db_handle;
};
}  // namespace clp

#endif  // CLP_SQLITEDB_HPP
