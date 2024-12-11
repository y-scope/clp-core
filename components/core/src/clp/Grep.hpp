#ifndef CLP_GREP_HPP
#define CLP_GREP_HPP

#include <optional>
#include <string>

#include <log_surgeon/Lexer.hpp>

#include "Defs.h"
#include "Query.hpp"
#include "QueryInterpretation.hpp"
#include "streaming_archive/reader/Archive.hpp"
#include "streaming_archive/reader/File.hpp"
#include "WildcardExpression.hpp"

namespace clp {

class Grep {
public:
    // Types
    /**
     * Handles search result
     * @param orig_file_path Path of uncompressed file
     * @param compressed_msg
     * @param decompressed_msg
     * @param custom_arg Custom argument for the output function
     */
    using OutputFunc = void (*)(
            std::string const& orig_file_path,
            streaming_archive::reader::Message const& compressed_msg,
            std::string const& decompressed_msg,
            void* custom_arg
    );

    // Methods
    /**
     * Processes a raw user query into a Query
     * @param archive
     * @param search_string
     * @param search_begin_ts
     * @param search_end_ts
     * @param ignore_case
     * @param lexer DFA for determining if input is in the schema
     * @param use_heuristic
     * @return Query if it may match a message, std::nullopt otherwise
     */
    static std::optional<Query> process_raw_query(
            streaming_archive::reader::Archive const& archive,
            std::string const& search_string,
            epochtime_t search_begin_ts,
            epochtime_t search_end_ts,
            bool ignore_case,
            log_surgeon::lexers::ByteLexer& lexer,
            bool use_heuristic
    );

    /**
     * Returns bounds of next potential variable (either a definite variable or a token with
     * wildcards)
     * @param value String containing token
     * @param begin_pos Begin position of last token, changes to begin position of next token
     * @param end_pos End position of last token, changes to end position of next token
     * @param is_var Whether the token is definitely a variable
     * @return true if another potential variable was found, false otherwise
     */
    static bool get_bounds_of_next_potential_var(
            std::string const& value,
            size_t& begin_pos,
            size_t& end_pos,
            bool& is_var
    );

    /**
     * Marks which sub-queries in each query are relevant to the given file
     * @param compressed_file
     * @param queries
     */
    static void calculate_sub_queries_relevant_to_file(
            streaming_archive::reader::File const& compressed_file,
            std::vector<Query>& queries
    );

    /**
     * Searches a file with the given query and outputs any results using the given method
     * @param query
     * @param limit
     * @param archive
     * @param compressed_file
     * @param output_func
     * @param output_func_arg
     * @return Number of matches found
     * @throw streaming_archive::reader::Archive::OperationFailed if decompression unexpectedly
     * fails
     * @throw TimestampPattern::OperationFailed if failed to insert timestamp into message
     */
    static size_t search_and_output(
            Query const& query,
            size_t limit,
            streaming_archive::reader::Archive& archive,
            streaming_archive::reader::File& compressed_file,
            OutputFunc output_func,
            void* output_func_arg
    );
    static bool search_and_decompress(
            Query const& query,
            streaming_archive::reader::Archive& archive,
            streaming_archive::reader::File& compressed_file,
            streaming_archive::reader::Message& compressed_msg,
            std::string& decompressed_msg
    );

    /**
     * Searches a file with the given query without outputting the results
     * @param query
     * @param limit
     * @param archive
     * @param compressed_file
     * @return Number of matches found
     * @throw streaming_archive::reader::Archive::OperationFailed if decompression unexpectedly
     * fails
     * @throw TimestampPattern::OperationFailed if failed to insert timestamp into message
     */
    static size_t search(
            Query const& query,
            size_t limit,
            streaming_archive::reader::Archive& archive,
            streaming_archive::reader::File& compressed_file
    );

    /**
     * Generates all possible logtypes that can match each substr(0,n) of the search string.
     * Requires that processed_search_string is valid, meaning that only wildcards are escaped
     * and the string does not end with an escape character.
     * @param processed_search_string
     * @param lexer
     * @return a vector of all QueryInterpretations that can match the query in
     * processed_search_string.
     */
    static std::set<QueryInterpretation> generate_query_substring_interpretations(
            WildcardExpression const& processed_search_string,
            log_surgeon::lexers::ByteLexer& lexer
    );

    /**
     * Computes the tokens (static text or different types of variables) that the given wildcard
     * expression (as a whole) could be interpreted as, generates a `QueryInterpretation` for each
     * one, and returns the `QueryInterpretation`s.
     * @param wildcard_expr
     * @param lexer
     * @return The `QueryInterpretation`s.
     */
    static std::vector<QueryInterpretation> get_interpretations_for_whole_wildcard_expr(
            WildcardExpressionView const& wildcard_expr,
            log_surgeon::lexers::ByteLexer& lexer
    );

    /**
     * Gets the variable types that the given wildcard expression could match.
     * @param wildcard_expr
     * @param lexer
     * @return A tuple:
     * - The set of variable types that the wildcard expression could match.
     * - Whether the wildcard expression contains a wildcard.
     */
    static std::tuple<std::set<uint32_t>, bool> get_matching_variable_types(
            WildcardExpressionView const& wildcard_expr,
            log_surgeon::lexers::ByteLexer const& lexer
    );

    /**
     * Compare all possible query logtypes against the archive to determine all possible sub queries
     * that can match against messages in the archive.
     * @param query_interpretations
     * @param archive
     * @param lexer
     * @param ignore_case
     * @param sub_queries
     */
    static void generate_sub_queries(
            std::set<QueryInterpretation> const& query_interpretations,
            streaming_archive::reader::Archive const& archive,
            log_surgeon::lexers::ByteLexer& lexer,
            bool ignore_case,
            std::vector<SubQuery>& sub_queries
    );
};
}  // namespace clp

#endif  // CLP_GREP_HPP
