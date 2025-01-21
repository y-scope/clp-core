#include "MySQLTableMetadataDB.hpp"

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include "../../clp/database_utils.hpp"
#include "../../clp/type_utils.hpp"

enum class TableMetadataFieldIndexes : uint16_t {
    Name = 0,
    Type,
    Length,
};

namespace clp_s::metadata_uploader {
void MySQLTableMetadataDB::open() {
    if (m_is_open) {
        throw OperationFailed(ErrorCodeNotReady, __FILENAME__, __LINE__);
    }

    m_db.open(m_host, m_port, m_username, m_password, m_database_name);
    m_is_open = true;
}

void MySQLTableMetadataDB::init(std::string const& table_name) {
    if (false == m_is_open) {
        throw OperationFailed(ErrorCodeNotReady, __FILENAME__, __LINE__);
    }

    m_db.execute_query(fmt::format(
            "CREATE TABLE IF NOT EXISTS {}{} ("
            "name VARCHAR(512) NOT NULL, "
            "type BIGINT NOT NULL,"
            "PRIMARY KEY (name, type)"
            ")",
            m_table_prefix,
            table_name
    ));

    m_insert_field_statement.reset();

    std::vector<std::string> table_metadata_field_names(
            clp::enum_to_underlying_type(TableMetadataFieldIndexes::Length)
    );
    table_metadata_field_names[clp::enum_to_underlying_type(TableMetadataFieldIndexes::Name)]
            = "name";
    table_metadata_field_names[clp::enum_to_underlying_type(TableMetadataFieldIndexes::Type)]
            = "type";
    fmt::memory_buffer statement_buffer;
    auto statement_buffer_ix = std::back_inserter(statement_buffer);

    fmt::format_to(
            statement_buffer_ix,
            "INSERT IGNORE INTO {}{} ({}) VALUES ({})",
            m_table_prefix,
            table_name,
            clp::get_field_names_sql(table_metadata_field_names),
            clp::get_placeholders_sql(table_metadata_field_names.size())
    );
    SPDLOG_DEBUG("{:.{}}", statement_buffer.data(), statement_buffer.size());
    m_insert_field_statement = std::make_unique<MySQLPreparedStatement>(
            m_db.prepare_statement(statement_buffer.data(), statement_buffer.size())
    );

    m_is_init = true;
}

void MySQLTableMetadataDB::add_field(std::string const& field_name, NodeType field_type) {
    if (false == m_is_init) {
        throw OperationFailed(ErrorCodeNotReady, __FILENAME__, __LINE__);
    }

    auto& statement_bindings = m_insert_field_statement->get_statement_bindings();
    statement_bindings.bind_varchar(
            clp::enum_to_underlying_type(TableMetadataFieldIndexes::Name),
            field_name.c_str(),
            field_name.length()
    );

    uint64_t field_type_value = static_cast<uint64_t>(field_type);
    statement_bindings.bind_uint64(
            clp::enum_to_underlying_type(TableMetadataFieldIndexes::Type),
            field_type_value
    );

    if (false == m_insert_field_statement->execute()) {
        throw OperationFailed(ErrorCodeFailure, __FILENAME__, __LINE__);
    }
}

void MySQLTableMetadataDB::close() {
    m_insert_field_statement.reset();
    m_db.close();
    m_is_open = false;
    m_is_init = false;
}
}  // namespace clp_s::metadata_uploader
