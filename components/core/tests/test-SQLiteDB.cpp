#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Catch2/single_include/catch2/catch.hpp>
#include <fmt/core.h>
#include <fmt/format.h>

#include "../src/clp/database_utils.hpp"
#include "../src/clp/SQLiteDB.hpp"

using epochtime_t = int64_t;
using clp::SQLiteDB;
using std::pair;
using std::string;
using std::string_view;
using std::unordered_map;
using std::vector;

namespace {
/**
 * A row in the test table.
 */
class Row {
public:
    Row(string path,
        epochtime_t begin_ts,
        epochtime_t end_ts,
        size_t segment_id,
        size_t segment_ts_pos,
        size_t segment_var_pos)
            : m_path{std::move(path)},
              m_begin_ts{begin_ts},
              m_end_ts{end_ts},
              m_segment_id{segment_id},
              m_segment_ts_pos{segment_ts_pos},
              m_segment_var_pos{segment_var_pos} {}

    Row(string path,
        epochtime_t begin_ts,
        epochtime_t end_ts,
        size_t segment_id,
        size_t segment_x_pos)
            : Row(std::move(path), begin_ts, end_ts, segment_id, segment_x_pos, segment_x_pos) {}

    [[nodiscard]] auto get_path() const -> string const& { return m_path; }

    [[nodiscard]] auto get_begin_ts() const -> epochtime_t { return m_begin_ts; }

    [[nodiscard]] auto get_end_ts() const -> epochtime_t { return m_end_ts; }

    [[nodiscard]] auto get_segment_id() const -> size_t { return m_segment_id; }

    [[nodiscard]] auto get_segment_ts_pos() const -> size_t { return m_segment_ts_pos; }

    [[nodiscard]] auto get_segment_var_pos() const -> size_t { return m_segment_var_pos; }

    [[nodiscard]] auto operator==(Row const& rhs) const -> bool {
        return rhs.m_path == m_path && rhs.m_begin_ts == m_begin_ts && rhs.m_end_ts == m_end_ts
               && rhs.m_segment_id == m_segment_id && rhs.m_segment_ts_pos == m_segment_ts_pos
               && rhs.m_segment_var_pos == m_segment_var_pos;
    }

private:
    string m_path;
    epochtime_t m_begin_ts;
    epochtime_t m_end_ts;
    size_t m_segment_id;
    size_t m_segment_ts_pos;
    size_t m_segment_var_pos;
};

/**
 * A class that defines a SQLite table schema (column names and types) for testing purposes.
 */
class TestTableSchema {
public:
    static constexpr char const* cPath{"path"};
    static constexpr char const* cBeginTs{"begin_timestamp"};
    static constexpr char const* cEndTs{"end_timestamp"};
    static constexpr char const* cSegmentId{"segment_id"};
    static constexpr char const* cSegmentTsPos{"segment_timestamp_position"};
    static constexpr char const* cSegmentVarPos{"segment_variable_position"};

    TestTableSchema() {
        auto add_column = [&](string_view column_name, string_view type) -> void {
            m_column_names.emplace_back(column_name);
            m_column_names_and_types.emplace_back(column_name, type);
        };

        add_column(cPath, "TEXT PRIMARY KEY");
        add_column(cBeginTs, "INTEGER");
        add_column(cEndTs, "INTEGER");
        add_column(cSegmentId, "INTEGER");
        add_column(cSegmentTsPos, "INTEGER");
        add_column(cSegmentVarPos, "INTEGER");
    }

    [[nodiscard]] auto get_name() const -> string_view { return m_name; }

    [[nodiscard]] auto get_column_names() const -> vector<string> const& { return m_column_names; }

    [[nodiscard]] auto get_column_names_and_types() const -> vector<pair<string, string>> const& {
        return m_column_names_and_types;
    }

private:
    string m_name{"CLP_TEST_TABLE"};
    vector<string> m_column_names;
    vector<pair<string, string>> m_column_names_and_types;
};

/**
 * Creates the table using the test table schemas.
 * @param db A SQLite database.
 * @param table_schema
 */
auto create_table(SQLiteDB& db, TestTableSchema const& table_schema) -> void {
    fmt::memory_buffer statement_buffer;
    auto stmt_buf_it{std::back_inserter(statement_buffer)};

    fmt::format_to(
            stmt_buf_it,
            "CREATE TABLE IF NOT EXISTS {} ({}) WITHOUT ROWID",
            table_schema.get_name(),
            clp::get_field_names_and_types_sql(table_schema.get_column_names_and_types())
    );
    auto create_table_stmt{db.prepare_statement(statement_buffer.data(), statement_buffer.size())};
    create_table_stmt.step();
    statement_buffer.clear();
}

/**
 * Creates indices in the table.
 * @param db A SQLite database.
 * @param table_schema
 */
auto create_indices(SQLiteDB& db, TestTableSchema const& table_schema) -> void {
    fmt::memory_buffer statement_buffer;
    auto stmt_buf_it{std::back_inserter(statement_buffer)};

    fmt::format_to(
            stmt_buf_it,
            "CREATE INDEX IF NOT EXISTS files_begin_timestamp ON {} ({})",
            table_schema.get_name(),
            TestTableSchema::cBeginTs
    );
    auto create_begin_ts_idx_stmt{
            db.prepare_statement(statement_buffer.data(), statement_buffer.size())
    };
    create_begin_ts_idx_stmt.step();
    statement_buffer.clear();

    fmt::format_to(
            stmt_buf_it,
            "CREATE INDEX IF NOT EXISTS files_end_timestamp ON {} ({})",
            table_schema.get_name(),
            TestTableSchema::cEndTs
    );
    auto create_end_ts_idx_stmt{
            db.prepare_statement(statement_buffer.data(), statement_buffer.size())
    };
    create_end_ts_idx_stmt.step();
    statement_buffer.clear();

    fmt::format_to(
            stmt_buf_it,
            "CREATE INDEX IF NOT EXISTS files_segment_order ON {} ({},{})",
            table_schema.get_name(),
            TestTableSchema::cSegmentId,
            TestTableSchema::cSegmentTsPos
    );
    auto create_segment_order_idx_stmt{
            db.prepare_statement(statement_buffer.data(), statement_buffer.size())
    };
    create_segment_order_idx_stmt.step();
    statement_buffer.clear();
}

/**
 * @return The absolute path of the generated test SQLite database.
 */
[[nodiscard]] auto get_test_db_abs_path() -> std::filesystem::path {
    return std::filesystem::current_path() / "sqlite-test.db";
}

/**
 * Creates a SQLite database using the given table schema, and inserts the given rows into the
 * table.
 * @param table_schema
 * @param rows
 */
auto create_db(TestTableSchema const& table_schema, vector<Row> const& rows) -> void {
    auto const db_path{get_test_db_abs_path()};
    if (std::filesystem::exists(db_path)) {
        REQUIRE((std::filesystem::remove(db_path)));
    }

    SQLiteDB sqlite_db;
    sqlite_db.open(db_path.string());

    create_table(sqlite_db, table_schema);
    create_indices(sqlite_db, table_schema);

    // We want to test a prepared insertion statement where the template parameters for the values
    // are arbitrary numeric values and at least one of the template parameters is used twice for
    // different columns. E.g. `INSERT INTO table (col2, col3, col1) VALUES (?99, ?34, ?99)`.
    // To test this, we shuffle the table's columns and then assign an ID to each column, starting
    // from an arbitrary value. To test using a parameter repeatedly, we use a single template
    // parameter for the segment_xxx_position columns.

    // Shuffle the columns
    auto table_columns{table_schema.get_column_names()};
    // NOLINTNEXTLINE(cert-msc32-c, cert-msc51-cpp)
    std::shuffle(table_columns.begin(), table_columns.end(), std::default_random_engine{});

    // Assign an ID, starting from 2, to each column
    int next_id{2};
    auto segment_xxx_pos_column_param_id{next_id++};
    unordered_map<string, int> column_name_to_param_id;
    fmt::memory_buffer param_id_buf;
    auto param_id_buf_it{std::back_insert_iterator(param_id_buf)};
    bool is_first_param{true};
    for (auto const& column : table_columns) {
        int param_id{};
        if (TestTableSchema::cSegmentTsPos == column || TestTableSchema::cSegmentVarPos == column) {
            param_id = segment_xxx_pos_column_param_id;
        } else {
            param_id = next_id++;
        }

        if (is_first_param) {
            is_first_param = false;
        } else {
            // Add a comma to join multiple parameters
            fmt::format_to(param_id_buf_it, ",");
        }
        fmt::format_to(param_id_buf_it, "?{}", param_id);

        column_name_to_param_id.emplace(column, param_id);
    }

    // Prepare the statements
    auto transaction_begin_stmt{sqlite_db.prepare_statement("BEGIN TRANSACTION")};
    auto transaction_end_stmt{sqlite_db.prepare_statement("END TRANSACTION")};
    fmt::memory_buffer stmt_buf;
    auto stmt_buf_it{std::back_inserter(stmt_buf)};
    fmt::format_to(
            stmt_buf_it,
            "INSERT INTO {} ({}) VALUES ({})",
            table_schema.get_name(),
            clp::get_field_names_sql(table_columns),
            fmt::to_string(param_id_buf)
    );
    auto insert_stmt{sqlite_db.prepare_statement(stmt_buf.data(), stmt_buf.size())};
    stmt_buf.clear();

    // Insert rows into the table
    transaction_begin_stmt.step();
    for (auto const& row : rows) {
        // Bind a value to each column using its corresponding parameter ID
        auto const path_param_id_it{column_name_to_param_id.find(TestTableSchema::cPath)};
        REQUIRE((column_name_to_param_id.cend() != path_param_id_it));
        insert_stmt.bind_text(path_param_id_it->second, row.get_path(), false);

        auto const begin_ts_param_id_it{column_name_to_param_id.find(TestTableSchema::cBeginTs)};
        REQUIRE((column_name_to_param_id.cend() != begin_ts_param_id_it));
        insert_stmt.bind_int64(begin_ts_param_id_it->second, row.get_begin_ts());

        auto const end_ts_param_id_it{column_name_to_param_id.find(TestTableSchema::cEndTs)};
        REQUIRE((column_name_to_param_id.cend() != end_ts_param_id_it));
        insert_stmt.bind_int64(end_ts_param_id_it->second, row.get_end_ts());

        auto const seg_id_param_id_it{column_name_to_param_id.find(TestTableSchema::cSegmentId)};
        REQUIRE((column_name_to_param_id.cend() != seg_id_param_id_it));
        insert_stmt.bind_int64(
                seg_id_param_id_it->second,
                static_cast<int64_t>(row.get_segment_id())
        );

        auto const seg_ts_pos_param_id_it{
                column_name_to_param_id.find(TestTableSchema::cSegmentTsPos)
        };
        REQUIRE((column_name_to_param_id.cend() != seg_ts_pos_param_id_it));
        insert_stmt.bind_int64(
                seg_ts_pos_param_id_it->second,
                static_cast<int64_t>(row.get_segment_ts_pos())
        );

        // We don't need to bind segment_var_pos explicitly since it has the same
        // parameter ID as segment_ts_pos
        auto const seg_var_pos_param_id_it{
                column_name_to_param_id.find(TestTableSchema::cSegmentVarPos)
        };
        REQUIRE((column_name_to_param_id.cend() != seg_var_pos_param_id_it));
        REQUIRE((seg_ts_pos_param_id_it->second == seg_var_pos_param_id_it->second));
        REQUIRE((row.get_segment_ts_pos() == row.get_segment_var_pos()));

        insert_stmt.step();
        insert_stmt.reset();
    }
    transaction_end_stmt.step();
    transaction_begin_stmt.reset();
    transaction_end_stmt.reset();

    sqlite_db.close();
}
}  // namespace

TEST_CASE("sqlite_db_basic", "[SQLiteDB]") {
    vector<Row> ref_rows{
            // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
            {"0.log", 1000, 2000, 0, 0},
            {"1.log", 1200, 1800, 0, 30},
            {"2.log", 800, 3800, 1, 0}
            // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    };
    TestTableSchema const table_schema;
    create_db(table_schema, ref_rows);

    auto test_db_path{get_test_db_abs_path()};
    SQLiteDB sqlite_db;
    sqlite_db.open(test_db_path.string());

    // We want to test a prepared select statement where the selected columns are arbitrarily
    // ordered compared to how they were created in the table. To test this, we first shuffle the
    // table's columns. Then, to retrieve the value of each column by name, we need to record the
    // mapping between its name and index in the select statement.

    // Shuffle the columns
    auto table_columns{table_schema.get_column_names()};
    // NOLINTNEXTLINE(cert-msc32-c, cert-msc51-cpp)
    std::shuffle(table_columns.begin(), table_columns.end(), std::default_random_engine{});

    // Prepare a statement to read all columns from the database, sorted by begin_ts
    vector<string> order_clause;
    fmt::memory_buffer order_buf;
    auto order_buf_it{std::back_inserter(order_buf)};
    fmt::format_to(order_buf_it, "{} ASC", TestTableSchema::cBeginTs);
    order_clause.emplace_back(order_buf.data(), order_buf.size());
    order_buf.clear();
    auto select_stmt{sqlite_db.prepare_select_statement(
            table_columns,
            table_schema.get_name(),
            {},
            order_clause
    )};

    // Read the rows
    vector<Row> rows;
    while (true) {
        select_stmt.step();
        if (false == select_stmt.is_row_ready()) {
            break;
        }

        string path;
        select_stmt.column_string(TestTableSchema::cPath, path);

        epochtime_t const begin_ts{select_stmt.column_int64(TestTableSchema::cBeginTs)};
        epochtime_t const end_ts{select_stmt.column_int64(TestTableSchema::cEndTs)};
        auto const seg_id{static_cast<size_t>(select_stmt.column_int64(TestTableSchema::cSegmentId))
        };
        size_t const seg_ts_pos{
                static_cast<size_t>(select_stmt.column_int64(TestTableSchema::cSegmentTsPos))
        };
        size_t const seg_var_pos{
                static_cast<size_t>(select_stmt.column_int64(TestTableSchema::cSegmentVarPos))
        };

        rows.emplace_back(path, begin_ts, end_ts, seg_id, seg_ts_pos, seg_var_pos);
    }

    // Sort the reference rows by begin_ts
    std::sort(ref_rows.begin(), ref_rows.end(), [](Row const& lhs, Row const& rhs) -> bool {
        if (lhs.get_begin_ts() == rhs.get_begin_ts()) {
            return lhs.get_path() < rhs.get_path();
        }
        return lhs.get_begin_ts() < rhs.get_begin_ts();
    });

    REQUIRE((ref_rows == rows));

    sqlite_db.close();

    // Cleanup
    REQUIRE(std::filesystem::remove(test_db_path));
}

TEST_CASE("sqlite_db_select_with_filtering", "[SQLiteDB]") {
    vector<Row> ref_rows{
            // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
            {"0.log", 1000, 2000, 0, 0},
            {"1.log", 1200, 1800, 0, 30},
            {"2.log", 3, 8000, 3, 0},
            {"3.log", 800, 3800, 1, 0},
            {"4.log", 4, 10'000, 1, 0},
            {"5.log", 800, 3800, 1, 0},
            {"6.log", 800, 3800, 1, 0}
            // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    };
    TestTableSchema const table_schema;
    create_db(table_schema, ref_rows);

    auto test_db_path{get_test_db_abs_path()};
    SQLiteDB sqlite_db;
    sqlite_db.open(test_db_path.string());

    // We want to test a select statement which contains both where clause and order clause.
    // Columns used for filtering and ordering are not necessarily selected.
    // Also, the same placeholder may be used to bind different filters.

    vector<string> const column_to_select{std::string{TestTableSchema::cPath}};
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    int const target_ts_placeholder{3};
    vector<string> where_clause;
    vector<string> order_clause;
    fmt::memory_buffer fmt_buf;
    auto fmt_buf_it{std::back_inserter(fmt_buf)};

    fmt::format_to(fmt_buf_it, "{} <= ?{}", TestTableSchema::cBeginTs, target_ts_placeholder);
    where_clause.emplace_back(fmt_buf.data(), fmt_buf.size());
    fmt_buf.clear();

    fmt::format_to(fmt_buf_it, "{} >= ?{}", TestTableSchema::cEndTs, target_ts_placeholder);
    where_clause.emplace_back(fmt_buf.data(), fmt_buf.size());
    fmt_buf.clear();

    fmt::format_to(fmt_buf_it, "{} ASC", TestTableSchema::cSegmentId);
    order_clause.emplace_back(fmt_buf.data(), fmt_buf.size());
    fmt_buf.clear();

    auto select_stmt{sqlite_db.prepare_select_statement(
            column_to_select,
            table_schema.get_name(),
            where_clause,
            order_clause
    )};
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    epochtime_t const target_ts{10};
    select_stmt.bind_int64(target_ts_placeholder, target_ts);

    vector<string> selected_results;
    while (true) {
        select_stmt.step();
        if (false == select_stmt.is_row_ready()) {
            break;
        }

        string path;
        select_stmt.column_string(TestTableSchema::cPath, path);
        selected_results.emplace_back(path);
    }

    // Sort the reference rows by segment id.
    std::sort(ref_rows.begin(), ref_rows.end(), [](Row const& lhs, Row const& rhs) -> bool {
        if (lhs.get_segment_id() == rhs.get_segment_id()) {
            return lhs.get_path() < rhs.get_path();
        }
        return lhs.get_segment_id() < rhs.get_segment_id();
    });

    // Gets expected results
    vector<string> expected_results;
    for (auto const& row : ref_rows) {
        if (row.get_begin_ts() > target_ts || row.get_end_ts() < target_ts) {
            continue;
        }
        expected_results.emplace_back(row.get_path());
    }

    REQUIRE((selected_results == expected_results));

    sqlite_db.close();

    // Cleanup
    REQUIRE(std::filesystem::remove(test_db_path));
}
