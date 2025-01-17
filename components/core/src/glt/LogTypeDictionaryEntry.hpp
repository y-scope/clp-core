#ifndef GLT_LOGTYPEDICTIONARYENTRY_HPP
#define GLT_LOGTYPEDICTIONARYENTRY_HPP

#include <vector>

#include "Defs.h"
#include "DictionaryEntry.hpp"
#include "ErrorCode.hpp"
#include "FileReader.hpp"
#include "ir/types.hpp"
#include "streaming_compression/zstd/Compressor.hpp"
#include "streaming_compression/zstd/Decompressor.hpp"
#include "TraceableException.hpp"
#include "type_utils.hpp"

namespace glt {
/**
 * Class representing a logtype dictionary entry
 */
class LogTypeDictionaryEntry : public DictionaryEntry<logtype_dictionary_id_t> {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        char const* what() const noexcept override {
            return "LogTypeDictionaryEntry operation failed";
        }
    };

    // Constructors
    LogTypeDictionaryEntry() = default;
    // Use default copy constructor
    LogTypeDictionaryEntry(LogTypeDictionaryEntry const&) = default;

    // Assignment operators
    // Use default
    LogTypeDictionaryEntry& operator=(LogTypeDictionaryEntry const&) = default;

    // Methods
    /**
     * Adds a dictionary variable placeholder to the given logtype
     * @param logtype
     */
    static void add_dict_var(std::string& logtype) {
        logtype += enum_to_underlying_type(ir::VariablePlaceholder::Dictionary);
    }

    /**
     * Adds an integer variable placeholder to the given logtype
     * @param logtype
     */
    static void add_int_var(std::string& logtype) {
        logtype += enum_to_underlying_type(ir::VariablePlaceholder::Integer);
    }

    /**
     * Adds a float variable placeholder to the given logtype
     * @param logtype
     */
    static void add_float_var(std::string& logtype) {
        logtype += enum_to_underlying_type(ir::VariablePlaceholder::Float);
    }

    /**
     * Adds an escape character to the given logtype
     * @param logtype
     */
    static void add_escape(std::string& logtype) {
        logtype += enum_to_underlying_type(ir::VariablePlaceholder::Escape);
    }

    /**
     * @return The number of variable placeholders (including escaped ones) in the logtype.
     */
    size_t get_num_placeholders() const { return m_placeholder_positions.size(); }

    /**
     * @return The number of variable placeholders (excluding escaped ones) in the logtype.
     */
    size_t get_num_variables() const {
        return m_placeholder_positions.size() - m_num_escaped_placeholders;
    }

    /**
     * Gets all info about a variable placeholder in the logtype
     * @param placeholder_ix The index of the placeholder to get the info for
     * @param placeholder
     * @return The placeholder's position in the logtype, or SIZE_MAX if var_ix is out of bounds
     */
    size_t get_placeholder_info(size_t placeholder_ix, ir::VariablePlaceholder& placeholder) const;

    /**
     * Gets all info about a variable placeholder in the logtype
     * @param placeholder_ix The index of the placeholder to get the info for
     * @param placeholder
     * @return The placeholder's position in the logtype, or SIZE_MAX if var_ix is out of bounds
     */
    size_t get_variable_info(size_t var_ix, ir::VariablePlaceholder& placeholder) const;

    /**
     * Gets the size (in-memory) of the data contained in this entry
     * @return Size of the data contained in this entry
     */
    size_t get_data_size() const;

    /**
     * Adds a constant to the logtype
     * @param value_containing_constant
     * @param begin_pos Start of the constant in value_containing_constant
     * @param length
     */
    void
    add_constant(std::string const& value_containing_constant, size_t begin_pos, size_t length);
    /**
     * Adds an int variable placeholder
     */
    void add_int_var();
    /**
     * Adds a float variable placeholder
     */
    void add_float_var();
    /**
     * Adds a dictionary variable placeholder
     */
    void add_dictionary_var();
    /**
     * Adds an escape character
     */
    void add_escape();

    /**
     * Parses next variable from a message, constructing the constant part of the message's logtype
     * as well
     * @param msg
     * @param var_begin_pos Beginning position of last variable. Changes to beginning position of
     * current variable.
     * @param var_end_pos End position of last variable (exclusive). Changes to end position of
     * current variable.
     * @param var
     * @return true if another variable was found, false otherwise
     */
    bool parse_next_var(
            std::string const& msg,
            size_t& var_begin_pos,
            size_t& var_end_pos,
            std::string& var
    );

    /**
     * Reserves space for a constant of the given length
     * @param length
     */
    void reserve_constant_length(size_t length) { m_value.reserve(length); }

    void set_id(logtype_dictionary_id_t id) { m_id = id; }

    void clear();

    /**
     * Writes an entry to file
     * @param compressor
     */
    void write_to_file(streaming_compression::Compressor& compressor) const;
    /**
     * Tries to read an entry from the given decompressor
     * @param decompressor
     * @return Same as streaming_compression::Decompressor::try_read_numeric_value
     * @return Same as streaming_compression::Decompressor::try_read_string
     */
    ErrorCode try_read_from_file(streaming_compression::Decompressor& decompressor);
    /**
     * Reads an entry from the given decompressor
     * @param decompressor
     */
    void read_from_file(streaming_compression::Decompressor& decompressor);

    /**
     * Generate a human readable version of value.
     * @param decompressor
     */
    std::string get_human_readable_value() const;

private:
    // Variables
    std::vector<size_t> m_placeholder_positions;
    std::vector<size_t> m_variable_positions;
    size_t m_num_escaped_placeholders{0};
};
}  // namespace glt

#endif  // GLT_LOGTYPEDICTIONARYENTRY_HPP
