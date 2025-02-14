#include <iostream>
#include <system_error>

#include <boost/foreach.hpp>
#include <boost/range/combine.hpp>
#include <Catch2/single_include/catch2/catch.hpp>
#include <string_utils/string_utils.hpp>

using clp::string_utils::clean_up_wildcard_search_string;
using clp::string_utils::convert_string_to_int;
using clp::string_utils::ValidatingUtf8Parser;
using clp::string_utils::wildcard_match_unsafe;
using clp::string_utils::wildcard_match_unsafe_case_sensitive;
using std::chrono::duration;
using std::chrono::high_resolution_clock;
using std::cout;
using std::endl;
using std::string;
using std::vector;

TEST_CASE("to_lower", "[to_lower]") {
    string str = "test123TEST";
    clp::string_utils::to_lower(str);
    REQUIRE(str == "test123test");
}

TEST_CASE("clean_up_wildcard_search_string", "[clean_up_wildcard_search_string]") {
    string str;

    // No wildcards
    str = "test";
    REQUIRE(clean_up_wildcard_search_string(str) == "test");

    // Only '?'
    str = "?est";
    REQUIRE(clean_up_wildcard_search_string(str) == "?est");

    // Normal case
    str = "***t**\\*s\\?t?**";
    REQUIRE(clean_up_wildcard_search_string(str) == "*t*\\*s\\?t?*");

    // Abnormal cases
    str = "***";
    REQUIRE(clean_up_wildcard_search_string(str) == "*");

    str = "*?*";
    REQUIRE(clean_up_wildcard_search_string(str) == "*?*");

    str = "?";
    REQUIRE(clean_up_wildcard_search_string(str) == "?");

    str = "?";
    REQUIRE(clean_up_wildcard_search_string(str) == "?");

    str = "a\\bc\\";
    REQUIRE(clean_up_wildcard_search_string(str) == "abc");
}

SCENARIO("Test case sensitive wild card match in all possible ways", "[wildcard]") {
    std::string tameString, wildString;

    WHEN("Match is expected if wild card character is \"*\"") {
        GIVEN("Single wild with no suffix char") {
            tameString = "abcd", wildString = "a*";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Single wild with no prefix char") {
            tameString = "abcd", wildString = "*d";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Single wild on both side & has 1st char as literal") {
            tameString = "abcd", wildString = "*a*";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Single wild on both side & has middle char as literal") {
            tameString = "abcd", wildString = "*b*";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Single wild on both side & has last char as literal") {
            tameString = "abcd", wildString = "*d*";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Single wild only") {
            tameString = "abcd", wildString = "*";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }
    }

    WHEN("Match is expected if Wild card character is \"?\"") {
        GIVEN("Single wild in the middle") {
            tameString = "abcd", wildString = "a?cd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Single wild in the beginning") {
            tameString = "abcd", wildString = "?bcd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Single wild at the end") {
            tameString = "abcd", wildString = "abc?";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Multiple wild in the middle") {
            tameString = "abcd", wildString = "a??d";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Multiple wild in the beginning") {
            tameString = "abcd", wildString = "??cd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Multiple wild in the end") {
            tameString = "abcd", wildString = "ab??";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Single wild in the beginning and end") {
            tameString = "abcd", wildString = "?bc?";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Multiple wild anywhere") {
            tameString = "abcdef", wildString = "a?c?ef";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("All wild") {
            tameString = "abcd", wildString = "????";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }
    }

    WHEN("Match is expected if wild card character has both \"*\" and \"?\"") {
        GIVEN("Wild begins with \"*?\" pattern with 0 matched char for \"*\"") {
            tameString = "abcd", wildString = "*?bcd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Wild begins with \"?*\" pattern with 0 matched char for \"*\"") {
            tameString = "abcd", wildString = "?*bcd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Wild begins with \"*?\" pattern with 1 matched char for \"*\"") {
            tameString = "abcd", wildString = "*?cd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Wild begins with \"?*\" pattern with 1 matched char for \"*\"") {
            tameString = "abcd", wildString = "*?cd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Wild ends with \"*?\" pattern with 0 matched char for \"*\"") {
            tameString = "abcd", wildString = "abc*?";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Wild ends with \"?*\" pattern with 0 matched char for \"*\"") {
            tameString = "abcd", wildString = "abc*?";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Wild ends with \"*?\" pattern with 1 matched char for \"*\"") {
            tameString = "abcd", wildString = "ab*?";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Wild ends with \"?*\" pattern with 1 matched char for \"*\"") {
            tameString = "abcd", wildString = "ab?*";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Wild begins with exactly \"*?\" pattern") {
            tameString = "abcd", wildString = "*?";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Wild begins with exactly \"?*\" pattern") {
            tameString = "abcd", wildString = "?*";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }
    }

    WHEN("Match unexpected containing wild card character(s)") {
        GIVEN("Missing literal character w/ \"*\"") {
            tameString = "abcd", wildString = "ac*";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == false);
        }

        GIVEN("More literals in wild than tame w/ \"*\" in the middle") {
            tameString = "abcd", wildString = "abc*de";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == false);
        }

        GIVEN("MISSING matching literals in the beginning with \"*\" in the middle") {
            tameString = "abcd", wildString = "b**d";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == false);
        }

        GIVEN("MISSING matching literals in the end with \"*\" in the middle") {
            tameString = "abcd", wildString = "a**c";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == false);
        }

        GIVEN("MISSING matching literals in the beginning with both \"*\" and \"?\" in the middle"
        ) {
            tameString = "abcd", wildString = "b*?d";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == false);
        }

        GIVEN("MISSING matching literals in the beginning with \"?\" at the beginning") {
            tameString = "abcd", wildString = "?cd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == false);
        }

        GIVEN("MISSING matching literals in the end with both \"?\" at the end") {
            tameString = "abcd", wildString = "ab?";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == false);
        }
    }

    WHEN("Match is expected when escape character(s) are used") {
        GIVEN("Escaping \"*\"") {
            tameString = "a*cd", wildString = "a\\*cd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Escaping \"?\"") {
            tameString = "a?cd", wildString = "a\\?cd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Escaping \"*\" and \"?\"") {
            tameString = "a?c*e", wildString = "a\\?c\\*e";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Escaping \"\\\"") {
            tameString = "a\\cd", wildString = "a\\\\cd";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Escaping \"?\" when fast forwarding") {
            tameString = "abc?e", wildString = "a*\\?e";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Escaping \"*\" when fast forwarding") {
            tameString = "abc*e", wildString = "a*\\*e";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Escaping \"\\\" when fast forwarding") {
            tameString = "abc\\e", wildString = "a*\\\\e";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Escaping \"?\" when rewinding") {
            tameString = "\\ab\\ab\\c?ef", wildString = "*ab\\\\c\\?*";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Escaping \"*\" when rewinding") {
            tameString = "\\ab\\ab\\c*ef", wildString = "*ab\\\\c\\**";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Escaping \"\\\" when rewinding") {
            tameString = "\\ab\\ab\\c\\ef", wildString = "*ab\\\\c\\\\*";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }

        GIVEN("Silently ignore unsupported escape sequence \\a") {
            tameString = "ab?d", wildString = "\\ab?d";
            REQUIRE(wildcard_match_unsafe_case_sensitive(tameString, wildString) == true);
        }
    }

    WHEN("Case wild card match is case insensitive") {
        // This test is meant to supplement the case sensitive wild card implementation
        // The case insensitive implementation is exactly the same as case sensitive except it
        // automatically adjust the inputs to lower case when needed before doing comparison. It is
        // rarely used due to lower performance
        bool isCaseSensitive = false;
        GIVEN("All lower case tame and all upper case wild") {
            tameString = "abcde", wildString = "A?C*";
            REQUIRE(wildcard_match_unsafe(tameString, wildString, isCaseSensitive) == true);
        }

        GIVEN("All lower case tame and mixed lower and upper case wild") {
            tameString = "abcde", wildString = "A?c*";
            REQUIRE(wildcard_match_unsafe(tameString, wildString, isCaseSensitive) == true);

            tameString = "abcde", wildString = "A?c*";
            REQUIRE(wildcard_match_unsafe(tameString, wildString, isCaseSensitive) == true);
        }
    }

    WHEN("Tested with a bunch of additional test cases found online") {
        bool allPassed = true;

        GIVEN("Case with repeating character sequences") {
            allPassed &= wildcard_match_unsafe_case_sensitive("abcccd", "*ccd");
            allPassed &= wildcard_match_unsafe_case_sensitive("mississipissippi", "*issip*ss*");
            allPassed
                    &= !wildcard_match_unsafe_case_sensitive("xxxx*zzzzzzzzy*f", "xxxx*zzy*fffff");
            allPassed &= wildcard_match_unsafe_case_sensitive("xxxx*zzzzzzzzy*f", "xxx*zzy*f");
            allPassed &= !wildcard_match_unsafe_case_sensitive("xxxxzzzzzzzzyf", "xxxx*zzy*fffff");
            allPassed &= wildcard_match_unsafe_case_sensitive("xxxxzzzzzzzzyf", "xxxx*zzy*f");
            allPassed &= wildcard_match_unsafe_case_sensitive("xyxyxyzyxyz", "xy*z*xyz");
            allPassed &= wildcard_match_unsafe_case_sensitive("mississippi", "*sip*");
            allPassed &= wildcard_match_unsafe_case_sensitive("xyxyxyxyz", "xy*xyz");
            allPassed &= wildcard_match_unsafe_case_sensitive("mississippi", "mi*sip*");
            allPassed &= wildcard_match_unsafe_case_sensitive("ababac", "*abac*");
            allPassed &= wildcard_match_unsafe_case_sensitive("ababac", "*abac*");
            allPassed &= wildcard_match_unsafe_case_sensitive("aaazz", "a*zz*");
            allPassed &= !wildcard_match_unsafe_case_sensitive("a12b12", "*12*23");
            allPassed &= !wildcard_match_unsafe_case_sensitive("a12b12", "a12b");
            allPassed &= wildcard_match_unsafe_case_sensitive("a12b12", "*12*12*");
            REQUIRE(allPassed == true);
        }

        GIVEN("Additional cases where the '*' char appears in the tame string") {
            allPassed &= wildcard_match_unsafe_case_sensitive("*", "*");
            allPassed &= wildcard_match_unsafe_case_sensitive("a*abab", "a*b");
            allPassed &= wildcard_match_unsafe_case_sensitive("a*r", "a*");
            allPassed &= !wildcard_match_unsafe_case_sensitive("a*ar", "a*aar");
            REQUIRE(allPassed == true);
        }

        GIVEN("More double wildcard scenarios") {
            allPassed &= wildcard_match_unsafe_case_sensitive("XYXYXYZYXYz", "XY*Z*XYz");
            allPassed &= wildcard_match_unsafe_case_sensitive("missisSIPpi", "*SIP*");
            allPassed &= wildcard_match_unsafe_case_sensitive("mississipPI", "*issip*PI");
            allPassed &= wildcard_match_unsafe_case_sensitive("xyxyxyxyz", "xy*xyz");
            allPassed &= wildcard_match_unsafe_case_sensitive("miSsissippi", "mi*sip*");
            allPassed &= !wildcard_match_unsafe_case_sensitive("miSsissippi", "mi*Sip*");
            allPassed &= wildcard_match_unsafe_case_sensitive("abAbac", "*Abac*");
            allPassed &= wildcard_match_unsafe_case_sensitive("abAbac", "*Abac*");
            allPassed &= wildcard_match_unsafe_case_sensitive("aAazz", "a*zz*");
            allPassed &= !wildcard_match_unsafe_case_sensitive("A12b12", "*12*23");
            allPassed &= wildcard_match_unsafe_case_sensitive("a12B12", "*12*12*");
            allPassed &= wildcard_match_unsafe_case_sensitive("oWn", "*oWn*");
            REQUIRE(allPassed == true);
        }

        GIVEN("Completely tame (no wildcards) cases") {
            allPassed &= wildcard_match_unsafe_case_sensitive("bLah", "bLah");
            allPassed &= !wildcard_match_unsafe_case_sensitive("bLah", "bLaH");
            REQUIRE(allPassed == true);
        }

        GIVEN("Simple mixed wildcard tests suggested by IBMer Marlin Deckert") {
            allPassed &= wildcard_match_unsafe_case_sensitive("a", "*?");
            allPassed &= wildcard_match_unsafe_case_sensitive("ab", "*?");
            allPassed &= wildcard_match_unsafe_case_sensitive("abc", "*?");
            REQUIRE(allPassed == true);
        }

        GIVEN("More mixed wildcard tests including coverage for false positives") {
            allPassed &= !wildcard_match_unsafe_case_sensitive("a", "??");
            allPassed &= wildcard_match_unsafe_case_sensitive("ab", "?*?");
            allPassed &= wildcard_match_unsafe_case_sensitive("ab", "*?*?*");
            allPassed &= wildcard_match_unsafe_case_sensitive("abcd", "?b*??");
            allPassed &= !wildcard_match_unsafe_case_sensitive("abcd", "?a*??");
            allPassed &= wildcard_match_unsafe_case_sensitive("abcde", "?*b*?*d*?");
            REQUIRE(allPassed == true);
        }

        GIVEN("Single-character-match cases") {
            allPassed &= wildcard_match_unsafe_case_sensitive("bLah", "bL?h");
            allPassed &= !wildcard_match_unsafe_case_sensitive("bLaaa", "bLa?");
            allPassed &= wildcard_match_unsafe_case_sensitive("bLah", "bLa?");
            allPassed &= !wildcard_match_unsafe_case_sensitive("bLaH", "?Lah");
            allPassed &= wildcard_match_unsafe_case_sensitive("bLaH", "?LaH");
            REQUIRE(allPassed == true);
        }

        GIVEN("Many-wildcard scenarios") {
            allPassed &= wildcard_match_unsafe_case_sensitive(
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                    "aaaaaaaaaaaab",
                    "a*a*a*a*a*a*aa*aaa*a*a*b"
            );
            allPassed &= wildcard_match_unsafe_case_sensitive(
                    "abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaa"
                    "aaaaaaaaaaffafagaagggagaaaaaaaab",
                    "*a*b*ba*ca*a*aa*aaa*fa*ga*b*"
            );
            allPassed &= !wildcard_match_unsafe_case_sensitive(
                    "abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaa"
                    "aaaaaaaaaaffafagaagggagaaaaaaaab",
                    "*a*b*ba*ca*a*x*aaa*fa*ga*b*"
            );
            allPassed &= !wildcard_match_unsafe_case_sensitive(
                    "abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaa"
                    "aaaaaaaaaaffafagaagggagaaaaaaaab",
                    "*a*b*ba*ca*aaaa*fa*ga*gggg*b*"
            );
            allPassed &= wildcard_match_unsafe_case_sensitive(
                    "abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaa"
                    "aaaaaaaaaaffafagaagggagaaaaaaaab",
                    "*a*b*ba*ca*aaaa*fa*ga*ggg*b*"
            );
            allPassed &= wildcard_match_unsafe_case_sensitive("aaabbaabbaab", "*aabbaa*a*");
            allPassed &= wildcard_match_unsafe_case_sensitive(
                    "a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*",
                    "a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*"
            );
            allPassed &= wildcard_match_unsafe_case_sensitive(
                    "aaaaaaaaaaaaaaaaa",
                    "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*"
            );
            allPassed &= !wildcard_match_unsafe_case_sensitive(
                    "aaaaaaaaaaaaaaaa",
                    "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*"
            );
            allPassed &= !wildcard_match_unsafe_case_sensitive(
                    "abc*abcd*abcde*abcdef*abcdefg*abcdefgh*abcdefghi*abcdefghij*abcdefghijk*"
                    "abcdefghijkl*abcdefghijklm*abcdefghijklmn",
                    "abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*"
            );
            allPassed &= wildcard_match_unsafe_case_sensitive(
                    "abc*abcd*abcde*abcdef*abcdefg*abcdefgh*abcdefghi*abcdefghij*abcdefghijk*"
                    "abcdefghijkl*abcdefghijklm*abcdefghijklmn",
                    "abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*"
            );
            allPassed &= !wildcard_match_unsafe_case_sensitive(
                    "abc*abcd*abcd*abc*abcd",
                    "abc*abc*abc*abc*abc"
            );
            allPassed &= wildcard_match_unsafe_case_sensitive(
                    "abc*abcd*abcd*abc*abcd*abcd*abc*abcd*abc*abc*abcd",
                    "abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abcd"
            );
            REQUIRE(allPassed == true);
        }

        GIVEN("A case-insensitive algorithm test") {
            bool isCaseSensitive = false;
            allPassed &= wildcard_match_unsafe("mississippi", "*issip*PI", isCaseSensitive);
            REQUIRE(allPassed == true);
        }
    }
}

SCENARIO("Test wild card performance", "[wildcard performance]") {
    // This test is to ensure there is no performance regression
    // We use our current implementation vs the next best implementation as a reference
    // If performance becomes slower than our next best implementation, then it is considered a fail

    high_resolution_clock::time_point t1, t2;
    string tameStr, wildStr;

    int const nReps = 1'000'000;
    int testReps;
    bool allPassed_currentImplementation = true;
    bool allPassed_nextBestImplementation = true;

    /***********************************************************************************************
     * Inputs Begin
     **********************************************************************************************/
    vector<string> tameVec, wildVec;

    // Typical apache log
    tameVec.push_back("64.242.88.10 - - [07/Mar/2004:16:06:51 -0800] \"GET "
                      "/twiki/bin/rdiff/TWiki/NewUserTemplate?rev1=1"
                      ".3&rev2=1.2 HTTP/1.1\" 200 4523");
    wildVec.push_back("*64.242.88.10*Mar/2004*GET*200*");

    /***********************************************************************************************
     * Inputs End
     **********************************************************************************************/

    // Profile current implementation
    testReps = nReps;
    t1 = high_resolution_clock::now();
    while (testReps--) {
        BOOST_FOREACH (boost::tie(tameStr, wildStr), boost::combine(tameVec, wildVec)) {
            allPassed_currentImplementation
                    &= wildcard_match_unsafe_case_sensitive(tameStr, wildStr);
        }
    }
    t2 = high_resolution_clock::now();
    duration<double> timeSpan_currentImplementation = t2 - t1;

    // Profile next best implementation
    testReps = nReps;
    t1 = high_resolution_clock::now();
    while (testReps--) {
        // Replace this part with slow implementation
        BOOST_FOREACH (boost::tie(tameStr, wildStr), boost::combine(tameVec, wildVec)) {
            allPassed_currentImplementation
                    &= wildcard_match_unsafe_case_sensitive(tameStr, wildStr);
        }
    }
    t2 = high_resolution_clock::now();
    duration<double> timeSpan_nextBestImplementation = t2 - t1;
    REQUIRE(allPassed_currentImplementation == true);

    if (allPassed_currentImplementation) {
        cout << "Passed performance test in " << (timeSpan_currentImplementation.count() * 1000)
             << " milliseconds." << endl;
    } else {
        cout << "Failed performance test in " << (timeSpan_currentImplementation.count() * 1000)
             << " milliseconds." << endl;
    }
}

TEST_CASE("convert_string_to_int", "[convert_string_to_int]") {
    int64_t raw_as_int;
    string raw;
    int64_t converted;

    // Corner cases
    // Empty string
    raw = "";
    REQUIRE(false == convert_string_to_int(raw, converted));

    // Edges of representable range
    raw_as_int = INT64_MAX;
    raw = std::to_string(raw_as_int);
    REQUIRE(convert_string_to_int(raw, converted));
    REQUIRE(raw_as_int == converted);

    raw_as_int = INT64_MIN;
    raw = std::to_string(raw_as_int);
    REQUIRE(convert_string_to_int(raw, converted));
    REQUIRE(raw_as_int == converted);

    raw = "9223372036854775808";  // INT64_MAX + 1 == 2^63
    REQUIRE(false == convert_string_to_int(raw, converted));

    raw = "-9223372036854775809";  // INT64_MIN - 1 == -2^63 - 1
    REQUIRE(false == convert_string_to_int(raw, converted));

    // Non-integers
    raw = "abc";
    REQUIRE(false == convert_string_to_int(raw, converted));

    raw = "90a";
    REQUIRE(false == convert_string_to_int(raw, converted));

    raw = "0.5";
    REQUIRE(false == convert_string_to_int(raw, converted));

    // Non-decimal integers
    raw = "0x5A";
    REQUIRE(false == convert_string_to_int(raw, converted));

    // Integers
    raw = "98340";
    REQUIRE(convert_string_to_int(raw, converted));
    REQUIRE(98'340 == converted);
}

TEST_CASE("Validate UTF-8 strings", "[validate_utf8]") {
    constexpr std::string_view cValidString{"abcd1234\u1234"};
    constexpr std::string_view cInvalidString1{"abcd123"
                                               "\xFF"
                                               "4\u1234"};
    constexpr std::string_view cExpectedReplacementForInvalidString1{"abcd123\uFFFD4\u1234"};
    constexpr std::string_view cInvalidString2{"abcd123"
                                               "\xF0"
                                               "\x80"
                                               "\x80"
                                               "\xC0"
                                               "4\u1234"};
    constexpr std::string_view cExpectedReplacementForInvalidString2{"abcd123\uFFFD\uFFFD4\u1234"};
    ValidatingUtf8Parser parser;

    SECTION("Error on invalid UTF-8 policy") {
        constexpr auto cPolicy{ValidatingUtf8Parser::InvalidUtf8Policy::ReturnError};
        auto outcome = parser.validate(cValidString, cPolicy);
        REQUIRE(false == outcome.has_error());
        REQUIRE(cValidString == outcome.value());
        outcome = parser.validate<cPolicy>(cValidString);
        REQUIRE(false == outcome.has_error());
        REQUIRE(cValidString == outcome.value());

        outcome = parser.validate(cInvalidString1, cPolicy);
        REQUIRE(true == outcome.has_error());
        REQUIRE(std::errc::illegal_byte_sequence == outcome.error());
        outcome = parser.validate<cPolicy>(cInvalidString1);
        REQUIRE(true == outcome.has_error());
        REQUIRE(std::errc::illegal_byte_sequence == outcome.error());

        outcome = parser.validate(cInvalidString2, cPolicy);
        REQUIRE(true == outcome.has_error());
        REQUIRE(std::errc::illegal_byte_sequence == outcome.error());
        outcome = parser.validate<cPolicy>(cInvalidString2);
        REQUIRE(true == outcome.has_error());
        REQUIRE(std::errc::illegal_byte_sequence == outcome.error());
    }

    SECTION("Replacement character on invalid UTF-8 policy") {
        constexpr auto cPolicy{
                ValidatingUtf8Parser::InvalidUtf8Policy::SubstituteReplacementCharacter
        };
        auto outcome = parser.validate(cValidString, cPolicy);
        REQUIRE(false == outcome.has_error());
        REQUIRE(cValidString == outcome.value());
        outcome = parser.validate<cPolicy>(cValidString);
        REQUIRE(false == outcome.has_error());
        REQUIRE(cValidString == outcome.value());

        outcome = parser.validate(cInvalidString1, cPolicy);
        REQUIRE(false == outcome.has_error());
        REQUIRE(cExpectedReplacementForInvalidString1 == outcome.value());
        outcome = parser.validate<cPolicy>(cInvalidString1);
        REQUIRE(false == outcome.has_error());
        REQUIRE(cExpectedReplacementForInvalidString1 == outcome.value());

        outcome = parser.validate(cInvalidString2, cPolicy);
        REQUIRE(false == outcome.has_error());
        REQUIRE(cExpectedReplacementForInvalidString2 == outcome.value());
        outcome = parser.validate<cPolicy>(cInvalidString2);
        REQUIRE(false == outcome.has_error());
        REQUIRE(cExpectedReplacementForInvalidString2 == outcome.value());
    }
}
