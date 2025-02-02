#ifndef CLP_SQLITEPREPAREDSTATEMENT_HPP
#define CLP_SQLITEPREPAREDSTATEMENT_HPP

#include <string>

#include <sqlite3/sqlite3.h>

#include "ErrorCode.hpp"
#include "TraceableException.hpp"

namespace clp {
class SQLitePreparedStatement {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        char const* what() const noexcept override {
            return "SQLitePreparedStatement operation failed";
        }
    };

    // Constructors
    SQLitePreparedStatement(char const* statement, size_t statement_length, sqlite3* db_handle);
    virtual ~SQLitePreparedStatement();

    // Delete copy constructor and assignment
    SQLitePreparedStatement(SQLitePreparedStatement const&) = delete;
    SQLitePreparedStatement& operator=(SQLitePreparedStatement const&) = delete;

    // Move constructor and assignment
    SQLitePreparedStatement(SQLitePreparedStatement&& rhs) noexcept;
    SQLitePreparedStatement& operator=(SQLitePreparedStatement&& rhs) noexcept;

    // Methods
    void bind_int(int parameter_index, int value);
    void bind_int(std::string const& parameter_name, int value);
    void bind_int64(int parameter_index, int64_t value);
    void bind_int64(std::string const& parameter_name, int64_t value);
    void bind_text(int parameter_index, std::string const& value, bool copy_parameter);
    void
    bind_text(std::string const& parameter_name, std::string const& value, bool copy_parameter);
    void reset();

    bool step();
    int column_int(int parameter_index) const;
    int64_t column_int64(int parameter_index) const;
    void column_string(int parameter_index, std::string& value) const;

    bool is_row_ready() const { return m_row_ready; }

private:
    // Members
    sqlite3* m_db_handle;
    sqlite3_stmt* m_statement_handle;
    bool m_row_ready;
};
}  // namespace clp

#endif  // CLP_SQLITEPREPAREDSTATEMENT_HPP
