#include "SQLitePreparedStatement.hpp"

#include "Defs.h"
#include "spdlog_with_specializations.hpp"

using std::string;

namespace clp {
SQLitePreparedStatement::SQLitePreparedStatement(
        char const* statement,
        size_t statement_length,
        sqlite3* db_handle
) {
    auto return_value = sqlite3_prepare_v2(
            db_handle,
            statement,
            statement_length,
            &m_statement_handle,
            nullptr
    );
    if (SQLITE_OK != return_value) {
        SPDLOG_ERROR(
                "SQLitePreparedStatement: Failed to prepare statement '{:.{}}' - {}",
                statement,
                statement_length,
                sqlite3_errmsg(db_handle)
        );
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }
    m_db_handle = db_handle;
    m_row_ready = false;
}

SQLitePreparedStatement::~SQLitePreparedStatement() {
    // NOTE: sqlite3_finalize can return an error but the docs seem to imply this is not a failure
    // of finalize but rather a notification that the statement was not in a good state before
    // finalization.
    sqlite3_finalize(m_statement_handle);
    m_statement_handle = nullptr;
    m_db_handle = nullptr;
}

SQLitePreparedStatement::SQLitePreparedStatement(SQLitePreparedStatement&& rhs) noexcept
        : m_db_handle(nullptr),
          m_statement_handle(nullptr),
          m_row_ready(false) {
    *this = std::move(rhs);
}

SQLitePreparedStatement& SQLitePreparedStatement::operator=(SQLitePreparedStatement&& rhs
) noexcept {
    if (this != &rhs) {
        if (nullptr != m_statement_handle) {
            sqlite3_finalize(m_statement_handle);
        }

        m_db_handle = rhs.m_db_handle;
        m_statement_handle = rhs.m_statement_handle;
        m_row_ready = rhs.m_row_ready;

        rhs.m_db_handle = nullptr;
        rhs.m_statement_handle = nullptr;
        rhs.m_row_ready = false;
    }

    return *this;
}

void SQLitePreparedStatement::bind_int(int parameter_index, int value) {
    auto return_value = sqlite3_bind_int(m_statement_handle, parameter_index, value);
    if (SQLITE_OK != return_value) {
        SPDLOG_ERROR(
                "SQLitePreparedStatement: Failed to bind int to statement - {}",
                sqlite3_errmsg(m_db_handle)
        );
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }
}

void SQLitePreparedStatement::bind_int(string const& parameter_name, int value) {
    int parameter_index = sqlite3_bind_parameter_index(m_statement_handle, parameter_name.c_str());
    if (0 == parameter_index) {
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__);
    }

    bind_int(parameter_index, value);
}

void SQLitePreparedStatement::bind_int64(int parameter_index, int64_t value) {
    auto return_value = sqlite3_bind_int64(m_statement_handle, parameter_index, value);
    if (SQLITE_OK != return_value) {
        SPDLOG_ERROR(
                "SQLitePreparedStatement: Failed to bind int64 to statement - {}",
                sqlite3_errmsg(m_db_handle)
        );
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }
}

void SQLitePreparedStatement::bind_int64(string const& parameter_name, int64_t value) {
    int parameter_index = sqlite3_bind_parameter_index(m_statement_handle, parameter_name.c_str());
    if (0 == parameter_index) {
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__);
    }

    bind_int64(parameter_index, value);
}

void SQLitePreparedStatement::bind_text(
        int parameter_index,
        std::string const& value,
        bool copy_parameter
) {
    auto return_value = sqlite3_bind_text(
            m_statement_handle,
            parameter_index,
            value.c_str(),
            value.length(),
            copy_parameter ? SQLITE_TRANSIENT : SQLITE_STATIC
    );
    if (SQLITE_OK != return_value) {
        SPDLOG_ERROR(
                "SQLitePreparedStatement: Failed to bind text to statement - {}",
                sqlite3_errmsg(m_db_handle)
        );
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }
}

void SQLitePreparedStatement::bind_text(
        string const& parameter_name,
        string const& value,
        bool copy_parameter
) {
    int parameter_index = sqlite3_bind_parameter_index(m_statement_handle, parameter_name.c_str());
    if (0 == parameter_index) {
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__);
    }

    bind_text(parameter_index, value, copy_parameter);
}

void SQLitePreparedStatement::reset() {
    // NOTE: sqlite3_reset can return an error but the docs seem to imply this is not a failure of
    // reset but rather a notification that the statement was not in a good state before reset.
    m_row_ready = false;
    sqlite3_reset(m_statement_handle);
}

bool SQLitePreparedStatement::step() {
    auto return_value = sqlite3_step(m_statement_handle);
    m_row_ready = (SQLITE_ROW == return_value);
    switch (return_value) {
        case SQLITE_BUSY:
            throw OperationFailed(ErrorCode_NotReady, __FILENAME__, __LINE__);
        case SQLITE_DONE:
            return false;
        case SQLITE_ROW:
            return true;
        default:
            throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }
}

int SQLitePreparedStatement::column_int(int parameter_index) const {
    if (false == m_row_ready) {
        throw OperationFailed(ErrorCode_NotReady, __FILENAME__, __LINE__);
    }

    return sqlite3_column_int(m_statement_handle, parameter_index);
}

int64_t SQLitePreparedStatement::column_int64(int parameter_index) const {
    if (false == m_row_ready) {
        throw OperationFailed(ErrorCode_NotReady, __FILENAME__, __LINE__);
    }

    return sqlite3_column_int64(m_statement_handle, parameter_index);
}

void SQLitePreparedStatement::column_string(int parameter_index, std::string& value) const {
    if (false == m_row_ready) {
        throw OperationFailed(ErrorCode_NotReady, __FILENAME__, __LINE__);
    }

    value.assign(
            reinterpret_cast<char const*>(sqlite3_column_text(m_statement_handle, parameter_index)),
            sqlite3_column_bytes(m_statement_handle, parameter_index)
    );
}
}  // namespace clp
