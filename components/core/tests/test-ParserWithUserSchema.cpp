// TODO: move this test to log_surgeon
// TODO: move load_lexer_from_file into SearchParser in log_surgeon

#include <sys/stat.h>

#include <string>
#include <utility>

#include <boost/filesystem.hpp>
#include <Catch2/single_include/catch2/catch.hpp>
#include <log_surgeon/LogParser.hpp>

#include "../src/clp/clp/run.hpp"
#include "../src/clp/GlobalMySQLMetadataDB.hpp"
#include "../src/clp/LogSurgeonReader.hpp"
#include "../src/clp/Utils.hpp"

using clp::FileReader;
using clp::load_lexer_from_file;
using clp::LogSurgeonReader;
using log_surgeon::DelimiterStringAST;
using log_surgeon::LALR1Parser;
using log_surgeon::lexers::ByteLexer;
using log_surgeon::LogParser;
using log_surgeon::ParserAST;
using log_surgeon::SchemaAST;
using log_surgeon::SchemaParser;
using log_surgeon::SchemaVarAST;
using log_surgeon::Token;

std::unique_ptr<SchemaAST> generate_schema_ast(std::string const& schema_file) {
    std::unique_ptr<SchemaAST> schema_ast = SchemaParser::try_schema_file(schema_file);
    REQUIRE(schema_ast.get() != nullptr);
    return schema_ast;
}

std::unique_ptr<LogParser> generate_log_parser(std::string const& schema_file) {
    std::unique_ptr<SchemaAST> schema_ast = generate_schema_ast(schema_file);
    std::unique_ptr<LogParser> log_parser = std::make_unique<LogParser>(schema_file);
    REQUIRE(log_parser.get() != nullptr);
    return log_parser;
}

void compress(
        std::string const& output_dir,
        std::string const& file_to_compress,
        std::string schema_file,
        bool old = false
) {
    std::vector<std::string> arguments;
    if (old) {
        arguments = {"main.cpp", "c", output_dir, file_to_compress};
    } else {
        arguments
                = {"main.cpp",
                   "c",
                   output_dir,
                   file_to_compress,
                   "--schema-path",
                   std::move(schema_file)};
    }
    std::vector<char const*> argv;
    for (auto const& arg : arguments) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    clp::clp::run(argv.size() - 1, argv.data());
}

void decompress(std::string archive_dir, std::string output_dir) {
    std::vector<std::string> arguments
            = {"main.cpp", "x", std::move(archive_dir), std::move(output_dir)};
    std::vector<char const*> argv;
    for (auto const& arg : arguments) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    clp::clp::run(argv.size() - 1, argv.data());
}

TEST_CASE("Test error for missing schema file", "[LALR1Parser][SchemaParser]") {
    std::string file_path = "../tests/test_schema_files/missing_schema.txt";
    std::string file_name = boost::filesystem::weakly_canonical(file_path).string();
    REQUIRE_THROWS_WITH(
            generate_schema_ast(file_path),
            "Failed to read '" + file_path + "', error_code="
                    + std::to_string(static_cast<int>(log_surgeon::ErrorCode::FileNotFound))
    );
}

TEST_CASE("Test error for empty schema file", "[LALR1Parser][SchemaParser]") {
    std::string file_path = "../tests/test_schema_files/empty_schema.txt";
    REQUIRE_THROWS_WITH(
            generate_schema_ast(file_path),
            "Schema:1:1: error: empty file\n"
            "          \n"
            "^\n"
    );
}

TEST_CASE("Test error for colon missing schema file", "[LALR1Parser][SchemaParser]") {
    std::string file_path = "../tests/test_schema_files/colon_missing_schema.txt";
    REQUIRE_THROWS_WITH(
            generate_schema_ast(file_path),
            "Schema:3:4: error: expected ':','AlphaNumeric' before ' ' token\n"
            "          int [0-9]+\n"
            "             ^\n"
    );
}

TEST_CASE("Test error for multi-character tokens in schema file", "[LALR1Parser][SchemaParser]") {
    std::string file_path = "../tests/test_schema_files/schema_with_multicharacter_token_error.txt";
    REQUIRE_THROWS_WITH(
            generate_schema_ast(file_path),
            "Schema:2:11: error: expected ':' before ' ' token\n"
            "          delimiters : \\r\\n\n"
            "                    ^\n"
    );
}

TEST_CASE("Test creating schema parser", "[LALR1Parser][SchemaParser]") {
    generate_schema_ast("../tests/test_schema_files/easy_schema.txt");
}

TEST_CASE("Test creating log parser with delimiters", "[LALR1Parser][LogParser]") {
    generate_log_parser("../tests/test_schema_files/schema_with_delimiters.txt");
}

TEST_CASE("Test creating log parser without delimiters", "[LALR1Parser][LogParser]") {
    REQUIRE_THROWS_WITH(
            generate_log_parser("../tests/test_schema_files/schema_without_delimiters.txt"),
            "When using --schema-path, \"delimiters:\" line must be used."
    );
}

// TODO: This test doesn't currently work because delimiters are allowed in
// schema files, and there is no option to disable this yet
// TEST_CASE("Test error for creating log file with delimiter in regex pattern",
//          "[LALR1Parser]SchemaParser]") {
//    std::string file_path = "../tests/test_schema_files/schema_with_delimiter_in_regex_error.txt";
//    std::string file_name = boost::filesystem::canonical(file_path).string();
//    REQUIRE_THROWS_WITH(generate_log_parser(file_path),
//                        file_name +
//                        ":2: error: 'equals' has regex pattern which contains delimiter '='.\n"
//                        + "          equals:.*=.*\n"
//                        + "                 ^^^^^\n");
//}

// TODO: This error check is performed correctly by CLP, but it is handled by
// something different now so this test will fail as is
// TEST_CASE("Test error for missing log file", "[LALR1Parser][LogParser]") {
//    std::string file_name = "../tests/test_log_files/missing_log.txt";
//    std::string file_path = boost::filesystem::weakly_canonical(file_name).string();
//    REQUIRE_THROWS(compress("../tests/test_archives", file_name,
//                            "../tests/test_schema_files/schema_that_does_not_exist.txt"),
//                   "Specified schema file does not exist.");
//}

TEST_CASE("Test forward lexer", "[Search]") {
    ByteLexer forward_lexer;
    std::string schema_file_name = "../tests/test_schema_files/search_schema.txt";
    std::string schema_file_path = boost::filesystem::weakly_canonical(schema_file_name).string();
    load_lexer_from_file(schema_file_path, false, forward_lexer);
    FileReader file_reader{"../tests/test_search_queries/easy.txt"};
    LogSurgeonReader reader_wrapper(file_reader);
    log_surgeon::ParserInputBuffer parser_input_buffer;
    parser_input_buffer.read_if_safe(reader_wrapper);
    forward_lexer.reset();
    Token token;
    auto error_code = forward_lexer.scan(parser_input_buffer, token);
    REQUIRE(error_code == log_surgeon::ErrorCode::Success);
    while (token.m_type_ids_ptr->at(0) != static_cast<int>(log_surgeon::SymbolID::TokenEndID)) {
        SPDLOG_INFO("token:" + token.to_string() + "\n");
        SPDLOG_INFO(
                "token.m_type_ids->back():"
                + forward_lexer.m_id_symbol[token.m_type_ids_ptr->back()] + "\n"
        );
        error_code = forward_lexer.scan(parser_input_buffer, token);
        REQUIRE(error_code == log_surgeon::ErrorCode::Success);
    }
}

TEST_CASE("Test reverse lexer", "[Search]") {
    ByteLexer reverse_lexer;
    std::string schema_file_name = "../tests/test_schema_files/search_schema.txt";
    std::string schema_file_path = boost::filesystem::weakly_canonical(schema_file_name).string();
    load_lexer_from_file(schema_file_path, false, reverse_lexer);
    FileReader file_reader{"../tests/test_search_queries/easy.txt"};
    LogSurgeonReader reader_wrapper(file_reader);
    log_surgeon::ParserInputBuffer parser_input_buffer;
    parser_input_buffer.read_if_safe(reader_wrapper);
    reverse_lexer.reset();
    Token token;
    auto error_code = reverse_lexer.scan(parser_input_buffer, token);
    REQUIRE(error_code == log_surgeon::ErrorCode::Success);
    while (token.m_type_ids_ptr->at(0) != static_cast<int>(log_surgeon::SymbolID::TokenEndID)) {
        SPDLOG_INFO("token:" + token.to_string() + "\n");
        SPDLOG_INFO(
                "token.m_type_ids->back():"
                + reverse_lexer.m_id_symbol[token.m_type_ids_ptr->back()] + "\n"
        );
        error_code = reverse_lexer.scan(parser_input_buffer, token);
        REQUIRE(error_code == log_surgeon::ErrorCode::Success);
    }
}
