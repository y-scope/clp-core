#include <unistd.h>

#include <Catch2/single_include/catch2/catch.hpp>

#include "../src/clp/EncodedVariableInterpreter.hpp"
#include "../src/clp/ir/types.hpp"
#include "../src/clp/streaming_archive/Constants.hpp"

using clp::cVariableDictionaryIdMax;
using clp::encoded_variable_t;
using clp::EncodedVariableInterpreter;
using clp::enum_to_underlying_type;
using clp::ir::VariablePlaceholder;
using std::string;
using std::to_string;
using std::vector;

TEST_CASE("EncodedVariableInterpreter", "[EncodedVariableInterpreter]") {
    SECTION("Test convert_string_to_representable_integer_var") {
        string value;
        encoded_variable_t encoded_var;

        // Test basic conversions
        value = "0";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));
        REQUIRE(0 == encoded_var);

        value = "-1";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));
        REQUIRE(-1 == encoded_var);

        value = "1";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));
        REQUIRE(1 == encoded_var);

        // Test edges of representable range
        encoded_variable_t representable_int_range_begin = INT64_MAX;
        value = to_string(representable_int_range_begin);
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));
        REQUIRE(representable_int_range_begin == encoded_var);

        encoded_variable_t representable_int_range_end = INT64_MIN;
        value = to_string(representable_int_range_end);
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));
        REQUIRE(representable_int_range_end == encoded_var);

        // Test non-integers
        value = "";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "a";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "-";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "+";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "-a";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "+a";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "--";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "++";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        // Test unrepresentable values
        value = " 1";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "- 1";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "1 ";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "01";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "+1";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "1u";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "1U";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "1l";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "1L";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "1ll";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "1LL";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "0.0";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));

        value = "-0";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_integer_var(
                value,
                encoded_var
        ));
    }

    SECTION("Test convert_string_to_representable_float_var") {
        string value;
        encoded_variable_t encoded_var;
        double var_as_double;

        // Test basic conversions
        value = "0.0";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));
        EncodedVariableInterpreter::convert_encoded_float_to_string(encoded_var, value);
        REQUIRE("0.0" == value);

        value = "-1.0";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));
        EncodedVariableInterpreter::convert_encoded_float_to_string(encoded_var, value);
        REQUIRE("-1.0" == value);

        value = "1.0";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));
        EncodedVariableInterpreter::convert_encoded_float_to_string(encoded_var, value);
        REQUIRE("1.0" == value);

        value = ".1";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));
        EncodedVariableInterpreter::convert_encoded_float_to_string(encoded_var, value);
        REQUIRE(".1" == value);

        value = "-00.00";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));
        EncodedVariableInterpreter::convert_encoded_float_to_string(encoded_var, value);
        REQUIRE("-00.00" == value);

        // Test edges of representable range
        value = "-999999999999999.9";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));
        EncodedVariableInterpreter::convert_encoded_float_to_string(encoded_var, value);
        REQUIRE("-999999999999999.9" == value);

        value = "-.9999999999999999";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));
        EncodedVariableInterpreter::convert_encoded_float_to_string(encoded_var, value);
        REQUIRE("-.9999999999999999" == value);

        value = ".9999999999999999";
        REQUIRE(EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));
        EncodedVariableInterpreter::convert_encoded_float_to_string(encoded_var, value);
        REQUIRE(".9999999999999999" == value);

        // Test non-doubles
        value = "";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "a";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "-";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "+";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "-a";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "+a";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "--";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "++";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        // Test unrepresentable values
        value = ".";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "1.";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = " 1.0";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "- 1.0";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "1.0 ";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "+1.0";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "1.0f";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "1.0F";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "1.0l";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = "1.0L";
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));

        value = to_string(UINT64_MAX);
        REQUIRE(!EncodedVariableInterpreter::convert_string_to_representable_float_var(
                value,
                encoded_var
        ));
    }

    SECTION("Test multiple metching values") {
        char const cVarDictPath[] = "var.dict";
        char const cVarSegmentIndexPath[] = "var.segindex";
        vector<string> var_strs = {"python2.7.3", "Python2.7.3", "PyThOn2.7.3", "PYTHON2.7.3"};
        clp::VariableDictionaryWriter var_dict_writer;

        var_dict_writer.open(cVarDictPath, cVarSegmentIndexPath, cVariableDictionaryIdMax);

        vector<encoded_variable_t> encoded_vars;
        vector<clp::variable_dictionary_id_t> var_ids;
        clp::LogTypeDictionaryEntry logtype_dict_entry;
        string const msg_template = "here is a string with a dictionary var: ";

        for (auto const& var_str : var_strs) {
            EncodedVariableInterpreter::encode_and_add_to_dictionary(
                    msg_template + var_str,
                    logtype_dict_entry,
                    var_dict_writer,
                    encoded_vars,
                    var_ids
            );
        }
        var_dict_writer.close();

        clp::VariableDictionaryReader var_dict_reader;
        var_dict_reader.open(cVarDictPath, cVarSegmentIndexPath);
        var_dict_reader.read_new_entries();

        REQUIRE(var_dict_reader.get_entry_matching_value(var_strs.at(0), true).size()
                == var_strs.size());
        REQUIRE(var_dict_reader.get_entry_matching_value(var_strs.at(0), false).size() == 1);

        var_dict_reader.close();

        // Clean-up
        int retval = unlink(cVarDictPath);
        REQUIRE(0 == retval);
        retval = unlink(cVarSegmentIndexPath);
        REQUIRE(0 == retval);
    }

    SECTION("Test encoding and decoding") {
        string msg;

        char const cVarDictPath[] = "var.dict";
        char const cVarSegmentIndexPath[] = "var.segindex";

        // Open writer
        clp::VariableDictionaryWriter var_dict_writer;
        var_dict_writer.open(cVarDictPath, cVarSegmentIndexPath, cVariableDictionaryIdMax);

        // Test encoding
        vector<encoded_variable_t> encoded_vars;
        vector<clp::variable_dictionary_id_t> var_ids;

        string large_val_str = to_string(cVariableDictionaryIdMax) + "0";
        vector<string> var_strs
                = {"4938", large_val_str, "-25.5196868642755", "-00.00", "python2.7.3"};
        // clang-format off
        msg = "here is a string with a small int " + var_strs[0]
              + " and a very large int " + var_strs[1]
              + " and a double " + var_strs[2]
              + " and a weird double " + var_strs[3]
              + " and a str with numbers "
              + var_strs[4] + " and an escape "
              + enum_to_underlying_type(VariablePlaceholder::Escape)
              + " and an int placeholder "
              + enum_to_underlying_type(VariablePlaceholder::Integer)
              + " and a float placeholder "
              + enum_to_underlying_type(VariablePlaceholder::Float)
              + " and a dictionary placeholder "
              + enum_to_underlying_type(VariablePlaceholder::Dictionary);
        // clang-format on

        clp::LogTypeDictionaryEntry logtype_dict_entry;
        EncodedVariableInterpreter::encode_and_add_to_dictionary(
                msg,
                logtype_dict_entry,
                var_dict_writer,
                encoded_vars,
                var_ids
        );
        var_dict_writer.close();

        // Test var_ids is correctly populated
        size_t encoded_var_id_ix = 0;
        VariablePlaceholder var_placeholder;
        for (auto placeholder_ix = 0; placeholder_ix < logtype_dict_entry.get_num_placeholders();
             placeholder_ix++)
        {
            std::ignore = logtype_dict_entry.get_placeholder_info(placeholder_ix, var_placeholder);
            if (VariablePlaceholder::Dictionary == var_placeholder) {
                auto var = encoded_vars[placeholder_ix];
                REQUIRE(var_ids.size() > encoded_var_id_ix);
                REQUIRE(EncodedVariableInterpreter::decode_var_dict_id(var)
                        == var_ids[encoded_var_id_ix]);
                encoded_var_id_ix++;
            }
        }
        REQUIRE(var_ids.size() == encoded_var_id_ix);

        // Open reader
        clp::VariableDictionaryReader var_dict_reader;
        var_dict_reader.open(cVarDictPath, cVarSegmentIndexPath);
        var_dict_reader.read_new_entries();

        // Test searching
        string search_logtype = "here is a string with a small int ";
        clp::SubQuery sub_query;
        REQUIRE(EncodedVariableInterpreter::encode_and_search_dictionary(
                var_strs[0],
                var_dict_reader,
                false,
                search_logtype,
                sub_query
        ));
        search_logtype += " and a very large int ";
        REQUIRE(EncodedVariableInterpreter::encode_and_search_dictionary(
                var_strs[1],
                var_dict_reader,
                false,
                search_logtype,
                sub_query
        ));
        search_logtype += " and a double ";
        REQUIRE(EncodedVariableInterpreter::encode_and_search_dictionary(
                var_strs[2],
                var_dict_reader,
                false,
                search_logtype,
                sub_query
        ));
        search_logtype += " and a weird double ";
        REQUIRE(EncodedVariableInterpreter::encode_and_search_dictionary(
                var_strs[3],
                var_dict_reader,
                false,
                search_logtype,
                sub_query
        ));
        search_logtype += " and a str with numbers ";
        REQUIRE(EncodedVariableInterpreter::encode_and_search_dictionary(
                var_strs[4],
                var_dict_reader,
                false,
                search_logtype,
                sub_query
        ));
        search_logtype += " and an escape ";
        search_logtype += enum_to_underlying_type(VariablePlaceholder::Escape);
        search_logtype += enum_to_underlying_type(VariablePlaceholder::Escape);
        search_logtype += " and an int placeholder ";
        search_logtype += enum_to_underlying_type(VariablePlaceholder::Escape);
        search_logtype += enum_to_underlying_type(VariablePlaceholder::Integer);
        search_logtype += " and a float placeholder ";
        search_logtype += enum_to_underlying_type(VariablePlaceholder::Escape);
        search_logtype += enum_to_underlying_type(VariablePlaceholder::Float);
        search_logtype += " and a dictionary placeholder ";
        search_logtype += enum_to_underlying_type(VariablePlaceholder::Escape);
        search_logtype += enum_to_underlying_type(VariablePlaceholder::Dictionary);
        auto& vars = sub_query.get_vars();
        REQUIRE(vars.size() == encoded_vars.size());
        for (size_t i = 0; i < vars.size(); ++i) {
            REQUIRE(vars[i].matches(encoded_vars[i]));
        }

        // Test search for unknown variable
        REQUIRE(false
                == EncodedVariableInterpreter::encode_and_search_dictionary(
                        "abc123",
                        var_dict_reader,
                        false,
                        search_logtype,
                        sub_query
                ));

        REQUIRE(logtype_dict_entry.get_value() == search_logtype);

        // Test decoding
        string decompressed_msg;
        REQUIRE(EncodedVariableInterpreter::decode_variables_into_message(
                logtype_dict_entry,
                var_dict_reader,
                encoded_vars,
                decompressed_msg
        ));
        REQUIRE(msg == decompressed_msg);

        var_dict_reader.close();

        // Clean-up
        int retval = unlink(cVarDictPath);
        REQUIRE(0 == retval);
        retval = unlink(cVarSegmentIndexPath);
        REQUIRE(0 == retval);
    }
}
