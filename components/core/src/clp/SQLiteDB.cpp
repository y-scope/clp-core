#include "SQLiteDB.hpp"

#include "Defs.h"
#include "spdlog_with_specializations.hpp"
#include "SQLitePreparedSelectStatement.hpp"

using std::string;

namespace clp {
void SQLiteDB::open(string const& path) {
    auto return_value = sqlite3_open(path.c_str(), &m_db_handle);
    if (SQLITE_OK != return_value) {
        SPDLOG_ERROR(
                "Failed to open sqlite database {} - {}",
                path.c_str(),
                sqlite3_errmsg(m_db_handle)
        );
        close();
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }
}

SQLiteDB::~SQLiteDB() {
    if (nullptr == m_db_handle) {
        return;
    }
    if (false == close()) {
        SPDLOG_WARN("Failed to close underlying SQLite database - this may cause a memory leak.");
    }
}

bool SQLiteDB::close() {
    auto return_value = sqlite3_close(m_db_handle);
    if (SQLITE_BUSY == return_value) {
        // Database objects (e.g., statements) not deallocated
        return false;
    }
    m_db_handle = nullptr;
    return true;
}

SQLitePreparedSelectStatement SQLiteDB::prepare_select_statement(
        std::vector<std::string> const& columns_to_select,
        std::string_view table,
        std::vector<std::string> where_clause,
        std::vector<std::string> ordering_clause
) {
    if (nullptr == m_db_handle) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }
    return SQLitePreparedSelectStatement::create_sqlite_prepared_select_statement(
            columns_to_select,
            table,
            where_clause,
            ordering_clause,
            m_db_handle
    );
}

SQLitePreparedStatement
SQLiteDB::prepare_statement(char const* statement, size_t statement_length) {
    if (nullptr == m_db_handle) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }

    return {statement, statement_length, m_db_handle};
}
}  // namespace clp
