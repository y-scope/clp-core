#include "Utils.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <set>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <log_surgeon/SchemaParser.hpp>
#include <spdlog/spdlog.h>
#include <string_utils/string_utils.hpp>

#include "spdlog_with_specializations.hpp"

using std::list;
using std::make_unique;
using std::string;
using std::unique_ptr;
using std::vector;

namespace clp {
ErrorCode create_directory(string const& path, mode_t mode, bool exist_ok) {
    int retval = mkdir(path.c_str(), mode);
    if (0 != retval) {
        if (EEXIST != errno) {
            return ErrorCode_errno;
        } else if (false == exist_ok) {
            return ErrorCode_FileExists;
        }
    }

    return ErrorCode_Success;
}

ErrorCode create_directory_structure(string const& path, mode_t mode) {
    assert(!path.empty());

    // Check if entire path already exists
    struct stat s = {};
    if (0 == stat(path.c_str(), &s)) {
        // Deepest directory exists, so can return here
        return ErrorCode_Success;
    } else if (ENOENT != errno) {
        // Unexpected error
        return ErrorCode_errno;
    }

    // Find deepest directory which exists, starting from the (2nd) deepest directory
    size_t path_end_pos = path.find_last_of('/');
    size_t last_path_end_pos = path.length();
    string dir_path;
    while (string::npos != path_end_pos) {
        if (last_path_end_pos - path_end_pos > 1) {
            dir_path.assign(path, 0, path_end_pos);
            if (0 == stat(dir_path.c_str(), &s)) {
                break;
            } else if (ENOENT != errno) {
                // Unexpected error
                return ErrorCode_errno;
            }
        }

        last_path_end_pos = path_end_pos;
        path_end_pos = path.find_last_of('/', path_end_pos - 1);
    }

    if (string::npos == path_end_pos) {
        // NOTE: Since the first path we create below contains more than one character, this assumes
        // the path "/" already exists
        path_end_pos = 0;
    }
    while (string::npos != path_end_pos) {
        path_end_pos = path.find_first_of('/', path_end_pos + 1);
        dir_path.assign(path, 0, path_end_pos);
        // Technically the directory shouldn't exist at this point in the code, but it may have been
        // created concurrently.
        auto error_code = create_directory(dir_path, mode, true);
        if (ErrorCode_Success != error_code) {
            return error_code;
        }
    }

    return ErrorCode_Success;
}

string get_parent_directory_path(string const& path) {
    string dirname = get_unambiguous_path(path);

    size_t last_slash_pos = dirname.find_last_of('/');
    if (0 == last_slash_pos) {
        dirname = "/";
    } else if (string::npos == last_slash_pos) {
        dirname = ".";
    } else {
        dirname.resize(last_slash_pos);
    }

    return dirname;
}

string get_unambiguous_path(string const& path) {
    string unambiguous_path;
    if (path.empty()) {
        return unambiguous_path;
    }

    // Break path into components
    vector<string> path_components;
    boost::split(path_components, path, boost::is_any_of("/"), boost::token_compress_on);

    // Remove ambiguous components
    list<string> unambiguous_components;
    size_t num_components_to_ignore = 0;
    for (size_t i = path_components.size(); i-- > 0;) {
        if (".." == path_components[i]) {
            ++num_components_to_ignore;
        } else if ("." == path_components[i] || path_components[i].empty()) {
            // Do nothing
        } else if (num_components_to_ignore > 0) {
            --num_components_to_ignore;
        } else {
            unambiguous_components.emplace_front(path_components[i]);
        }
    }

    // Assemble unambiguous path from leading slash (if any) and the unambiguous components
    if ('/' == path[0]) {
        unambiguous_path += '/';
    }
    if (!unambiguous_components.empty()) {
        unambiguous_path += boost::join(unambiguous_components, "/");
    }

    return unambiguous_path;
}

ErrorCode read_list_of_paths(string const& list_path, vector<string>& paths) {
    unique_ptr<FileReader> file_reader;
    try {
        file_reader = make_unique<FileReader>(list_path);
    } catch (FileReader::OperationFailed const& err) {
        return err.get_error_code();
    }

    // Read file
    string line;
    ErrorCode error_code{ErrorCode_Success};
    while (true) {
        error_code = file_reader->try_read_to_delimiter('\n', false, false, line);
        if (ErrorCode_Success != error_code) {
            break;
        }
        // Only add non-empty paths
        if (line.empty() == false) {
            paths.push_back(line);
        }
    }
    // Check for any unexpected errors
    if (ErrorCode_EndOfFile != error_code) {
        return error_code;
    }

    return ErrorCode_Success;
}

// TODO: duplicates code in log_surgeon/parser.tpp, should implement a
// SearchParser in log_surgeon instead and use it here. Specifically, initialization of
// lexer.m_symbol_id, contains_delimiter error, and add_rule logic.
void load_lexer_from_file(
        std::string const& schema_file_path,
        bool reverse,
        log_surgeon::lexers::ByteLexer& lexer
) {
    std::unique_ptr<log_surgeon::SchemaAST> schema_ast
            = log_surgeon::SchemaParser::try_schema_file(schema_file_path);
    if (!lexer.m_symbol_id.empty()) {
        throw std::runtime_error("Error: symbol_ids initialized before setting enum symbol_ids");
    }

    // cTokenEnd and cTokenUncaughtString never need to be added as a rule to the lexer as they are
    // not parsed
    lexer.m_symbol_id[log_surgeon::cTokenEnd] = static_cast<int>(log_surgeon::SymbolID::TokenEndID);
    lexer.m_symbol_id[log_surgeon::cTokenUncaughtString]
            = static_cast<int>(log_surgeon::SymbolID::TokenUncaughtStringID);
    // cTokenInt, cTokenFloat, cTokenFirstTimestamp, and cTokenNewlineTimestamp each have unknown
    // rule(s) until specified by the user so can't be explicitly added and are done by looping over
    // schema_vars (user schema)
    lexer.m_symbol_id[log_surgeon::cTokenInt] = static_cast<int>(log_surgeon::SymbolID::TokenIntId);
    lexer.m_symbol_id[log_surgeon::cTokenFloat]
            = static_cast<int>(log_surgeon::SymbolID::TokenFloatId);
    lexer.m_symbol_id[log_surgeon::cTokenFirstTimestamp]
            = static_cast<int>(log_surgeon::SymbolID::TokenFirstTimestampId);
    lexer.m_symbol_id[log_surgeon::cTokenNewlineTimestamp]
            = static_cast<int>(log_surgeon::SymbolID::TokenNewlineTimestampId);
    // cTokenNewline is not added in schema_vars and can be explicitly added as '\n' to catch the
    // end of non-timestamped log messages
    lexer.m_symbol_id[log_surgeon::cTokenNewline]
            = static_cast<int>(log_surgeon::SymbolID::TokenNewlineId);

    lexer.m_id_symbol[static_cast<int>(log_surgeon::SymbolID::TokenEndID)] = log_surgeon::cTokenEnd;
    lexer.m_id_symbol[static_cast<int>(log_surgeon::SymbolID::TokenUncaughtStringID)]
            = log_surgeon::cTokenUncaughtString;
    lexer.m_id_symbol[static_cast<int>(log_surgeon::SymbolID::TokenIntId)] = log_surgeon::cTokenInt;
    lexer.m_id_symbol[static_cast<int>(log_surgeon::SymbolID::TokenFloatId)]
            = log_surgeon::cTokenFloat;
    lexer.m_id_symbol[static_cast<int>(log_surgeon::SymbolID::TokenFirstTimestampId)]
            = log_surgeon::cTokenFirstTimestamp;
    lexer.m_id_symbol[static_cast<int>(log_surgeon::SymbolID::TokenNewlineTimestampId)]
            = log_surgeon::cTokenNewlineTimestamp;
    lexer.m_id_symbol[static_cast<int>(log_surgeon::SymbolID::TokenNewlineId)]
            = log_surgeon::cTokenNewline;

    lexer.add_rule(
            lexer.m_symbol_id["newLine"],
            std::move(std::make_unique<log_surgeon::finite_automata::RegexASTLiteral<
                              log_surgeon::finite_automata::RegexNFAByteState>>(
                    log_surgeon::finite_automata::RegexASTLiteral<
                            log_surgeon::finite_automata::RegexNFAByteState>('\n')
            ))
    );

    for (auto const& delimiters_ast : schema_ast->m_delimiters) {
        auto* delimiters_ptr = dynamic_cast<log_surgeon::DelimiterStringAST*>(delimiters_ast.get());
        if (delimiters_ptr != nullptr) {
            lexer.add_delimiters(delimiters_ptr->m_delimiters);
        }
    }
    vector<uint32_t> delimiters;
    for (uint32_t i = 0; i < log_surgeon::cSizeOfByte; i++) {
        if (lexer.is_delimiter(i)) {
            delimiters.push_back(i);
        }
    }
    for (std::unique_ptr<log_surgeon::ParserAST> const& parser_ast : schema_ast->m_schema_vars) {
        auto* rule = dynamic_cast<log_surgeon::SchemaVarAST*>(parser_ast.get());

        if (lexer.m_symbol_id.find(rule->m_name) == lexer.m_symbol_id.end()) {
            lexer.m_symbol_id[rule->m_name] = lexer.m_symbol_id.size();
            lexer.m_id_symbol[lexer.m_symbol_id[rule->m_name]] = rule->m_name;
        }

        // transform '.' from any-character into any non-delimiter character
        rule->m_regex_ptr->remove_delimiters_from_wildcard(delimiters);

        bool is_possible_input[log_surgeon::cUnicodeMax] = {false};
        rule->m_regex_ptr->set_possible_inputs_to_true(is_possible_input);
        bool contains_delimiter = false;
        uint32_t delimiter_name;
        for (uint32_t delimiter : delimiters) {
            if (is_possible_input[delimiter]) {
                contains_delimiter = true;
                delimiter_name = delimiter;
                break;
            }
        }

        if (contains_delimiter && "timestamp" != rule->m_name) {
            FileReader schema_reader{schema_ast->m_file_path};
            // more detailed debugging based on looking at the file
            string line;
            for (uint32_t i = 0; i <= rule->m_line_num; i++) {
                schema_reader.read_to_delimiter('\n', false, false, line);
            }
            int colon_pos = 0;
            for (char i : line) {
                colon_pos++;
                if (i == ':') {
                    break;
                }
            }
            string indent(10, ' ');
            string spaces(colon_pos, ' ');
            string arrows(line.size() - colon_pos, '^');

            throw std::runtime_error(
                    schema_file_path + ":" + std::to_string(rule->m_line_num + 1) + ": error: '"
                    + rule->m_name + "' has regex pattern which contains delimiter '"
                    + char(delimiter_name) + "'.\n" + indent + line + "\n" + indent + spaces
                    + arrows + "\n"
            );
        }
        lexer.add_rule(lexer.m_symbol_id[rule->m_name], std::move(rule->m_regex_ptr));
    }
    if (reverse) {
        lexer.generate_reverse();
    } else {
        lexer.generate();
    }
}
}  // namespace clp
