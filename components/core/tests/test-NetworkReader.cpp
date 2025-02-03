#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <ystdlib/container/Array.hpp>

#include <Catch2/single_include/catch2/catch.hpp>
#include <curl/curl.h>
#include <fmt/core.h>
#include <json/single_include/nlohmann/json.hpp>

#include "../src/clp/CurlDownloadHandler.hpp"
#include "../src/clp/CurlGlobalInstance.hpp"
#include "../src/clp/ErrorCode.hpp"
#include "../src/clp/FileReader.hpp"
#include "../src/clp/NetworkReader.hpp"
#include "../src/clp/Platform.hpp"
#include "../src/clp/ReaderInterface.hpp"

namespace {
constexpr size_t cDefaultReaderBufferSize{1024};

[[nodiscard]] auto get_test_input_local_path() -> std::string;

[[nodiscard]] auto get_test_input_remote_url() -> std::string;

[[nodiscard]] auto get_test_input_path_relative_to_tests_dir() -> std::filesystem::path;

/**
 * @param reader
 * @param read_buf_size The size of the buffer to use for individual reads from the reader.
 * @return All data read from the given reader.
 */
auto get_content(clp::ReaderInterface& reader, size_t read_buf_size = cDefaultReaderBufferSize)
        -> std::vector<char>;

/**
 * Asserts whether the given `CURLcode` and the CURL return code stored in the given `NetworkReader`
 * instance are the same, and prints a log message if not.
 * @param expected
 * @param reader
 * @return Whether the the assertion succeeded.
 */
[[nodiscard]] auto assert_curl_error_code(CURLcode expected, clp::NetworkReader const& reader)
        -> bool;

auto get_test_input_local_path() -> std::string {
    std::filesystem::path const current_file_path{__FILE__};
    auto const tests_dir{current_file_path.parent_path()};
    return (tests_dir / get_test_input_path_relative_to_tests_dir()).string();
}

auto get_test_input_remote_url() -> std::string {
    auto const input_path_relative_to_repo = std::filesystem::path{"components"} / "core" / "tests"
                                             / get_test_input_path_relative_to_tests_dir();
    return std::string{"https://raw.githubusercontent.com/y-scope/clp/main/"}
           + input_path_relative_to_repo.string();
}

auto get_test_input_path_relative_to_tests_dir() -> std::filesystem::path {
    return std::filesystem::path{"test_network_reader_src"} / "random.log";
}

auto get_content(clp::ReaderInterface& reader, size_t read_buf_size) -> std::vector<char> {
    std::vector<char> buf;
    ystdlib::container::Array<char> read_buf{read_buf_size};
    for (bool has_more_content{true}; has_more_content;) {
        size_t num_bytes_read{};
        has_more_content = reader.read(read_buf.data(), read_buf_size, num_bytes_read);
        std::string_view const view{read_buf.data(), num_bytes_read};
        buf.insert(buf.cend(), view.cbegin(), view.cend());
    }
    return buf;
}

auto assert_curl_error_code(CURLcode expected, clp::NetworkReader const& reader) -> bool {
    auto const ret_code{reader.get_curl_ret_code()};
    if (false == ret_code.has_value()) {
        WARN("The CURL error code hasn't been set yet in the given reader.");
        return false;
    }
    auto const actual{ret_code.value()};
    if (expected == actual) {
        return true;
    }
    std::string message_to_log{
            "Unexpected CURL error code: " + std::to_string(actual)
            + "; expected: " + std::to_string(expected)
    };
    auto const curl_error_message{reader.get_curl_error_msg()};
    if (curl_error_message.has_value()) {
        message_to_log += "\nError message:\n" + std::string{curl_error_message.value()};
    }
    WARN(message_to_log);
    return false;
}
}  // namespace

TEST_CASE("network_reader_basic", "[NetworkReader]") {
    clp::FileReader ref_reader{get_test_input_local_path()};
    auto const expected{get_content(ref_reader)};

    clp::CurlGlobalInstance const curl_global_instance;
    clp::NetworkReader reader{get_test_input_remote_url()};
    auto const actual{get_content(reader)};
    REQUIRE(assert_curl_error_code(CURLE_OK, reader));
    REQUIRE((actual == expected));
}

TEST_CASE("network_reader_with_offset_and_seek", "[NetworkReader]") {
    constexpr size_t cOffset{319};
    clp::FileReader ref_reader{get_test_input_local_path()};
    ref_reader.seek_from_begin(cOffset);
    auto const expected{get_content(ref_reader)};
    auto const ref_end_pos{ref_reader.get_pos()};

    // Read from an offset onwards by starting the download from that offset.
    {
        clp::CurlGlobalInstance const curl_global_instance;
        clp::NetworkReader reader{get_test_input_remote_url(), cOffset};
        auto const actual{get_content(reader)};
        REQUIRE(assert_curl_error_code(CURLE_OK, reader));
        REQUIRE((reader.get_pos() == ref_end_pos));
        REQUIRE((actual == expected));
    }

    // Read from an offset onwards by seeking to that offset.
    {
        clp::CurlGlobalInstance const curl_global_instance;
        clp::NetworkReader reader(get_test_input_remote_url());
        reader.seek_from_begin(cOffset);
        auto const actual{get_content(reader)};
        REQUIRE(assert_curl_error_code(CURLE_OK, reader));
        REQUIRE((reader.get_pos() == ref_end_pos));
        REQUIRE((actual == expected));
    }
}

TEST_CASE("network_reader_destruct", "[NetworkReader]") {
    // We sleep to fill out all the buffers, and then we delete the reader. The destructor will try
    // to abort the underlying download and then destroy the instance. So should ensure destructor
    // is successfully executed without deadlock or exceptions.
    bool no_exception{true};
    try {
        // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
        clp::CurlGlobalInstance const curl_global_instance;
        auto reader{std::make_unique<clp::NetworkReader>(
                get_test_input_remote_url(),
                0,
                true,
                clp::CurlDownloadHandler::cDefaultOverallTimeout,
                clp::CurlDownloadHandler::cDefaultConnectionTimeout,
                3,
                512
        )};
        std::this_thread::sleep_for(std::chrono::milliseconds{1500});
        // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
        REQUIRE(reader->is_download_in_progress());
        reader.reset(nullptr);
    } catch (clp::NetworkReader::OperationFailed const& ex) {
        no_exception = false;
    }
    REQUIRE(no_exception);
}

TEST_CASE("network_reader_illegal_offset", "[NetworkReader]") {
    // Try to read from an out-of-bound offset.
    constexpr size_t cIllegalOffset{UINT32_MAX};
    clp::CurlGlobalInstance const curl_global_instance;
    clp::NetworkReader reader{get_test_input_remote_url(), cIllegalOffset};
    while (false == reader.get_curl_ret_code().has_value()) {
        // Wait until the return code is ready
    }

    if constexpr (clp::Platform::MacOs == clp::cCurrentPlatform) {
        // On macOS, HTTP response code 416 is not handled as `CURL_HTTP_RETURNED_ERROR` in some
        // `libcurl` versions.
        REQUIRE(
                (assert_curl_error_code(CURLE_HTTP_RETURNED_ERROR, reader)
                 || assert_curl_error_code(CURLE_RECV_ERROR, reader))
        );
    } else {
        REQUIRE(assert_curl_error_code(CURLE_HTTP_RETURNED_ERROR, reader));
    }
    size_t pos{};
    REQUIRE((clp::ErrorCode_Failure == reader.try_get_pos(pos)));
}

TEST_CASE("network_reader_with_valid_http_header_kv_pairs", "[NetworkReader]") {
    std::unordered_map<std::string, std::string> valid_http_header_kv_pairs;
    // We use httpbin (https://httpbin.org/) to test the user-specified headers. On success, it is
    // supposed to respond all the user-specified headers as key-value pairs in JSON form.
    constexpr size_t cNumHttpHeaderKeyValuePairs{10};
    for (size_t i{0}; i < cNumHttpHeaderKeyValuePairs; ++i) {
        valid_http_header_kv_pairs.emplace(
                fmt::format("Unit-Test-Key{}", i),
                fmt::format("Unit-Test-Value{}", i)
        );
    }
    std::optional<std::vector<char>> optional_content;
    // Retry the unit test a limited number of times to handle transient server-side HTTP errors.
    // This ensures the test is not marked as failed due to temporary issues beyond our control.
    constexpr size_t cNumMaxTrials{10};
    for (size_t i{0}; i < cNumMaxTrials; ++i) {
        clp::NetworkReader reader{
                "https://httpbin.org/headers",
                0,
                false,
                clp::CurlDownloadHandler::cDefaultOverallTimeout,
                clp::CurlDownloadHandler::cDefaultConnectionTimeout,
                clp::NetworkReader::cDefaultBufferPoolSize,
                clp::NetworkReader::cDefaultBufferSize,
                valid_http_header_kv_pairs
        };
        auto const content = get_content(reader);
        if (assert_curl_error_code(CURLE_OK, reader)) {
            optional_content.emplace(content);
            break;
        }
    }
    REQUIRE(optional_content.has_value());
    auto const parsed_content = nlohmann::json::parse(optional_content.value());
    auto const& headers{parsed_content.at("headers")};
    for (auto const& [key, value] : valid_http_header_kv_pairs) {
        REQUIRE((value == headers.at(key).get<std::string_view>()));
    }
}

TEST_CASE("network_reader_with_illegal_http_header_kv_pairs", "[NetworkReader]") {
    auto illegal_header_kv_pairs = GENERATE(
            // The following headers are determined by offset and disable_cache, which should not be
            // overridden by user-defined headers.
            std::unordered_map<std::string, std::string>{{"Range", "bytes=100-"}},
            std::unordered_map<std::string, std::string>{{"RAnGe", "bytes=100-"}},
            std::unordered_map<std::string, std::string>{{"Cache-Control", "no-cache"}},
            std::unordered_map<std::string, std::string>{{"Pragma", "no-cache"}},
            // The CRLF-terminated headers should be rejected.
            std::unordered_map<std::string, std::string>{{"Legal-Name", "CRLF\r\n"}}
    );
    clp::NetworkReader reader{
            "https://httpbin.org/headers",
            0,
            false,
            clp::CurlDownloadHandler::cDefaultOverallTimeout,
            clp::CurlDownloadHandler::cDefaultConnectionTimeout,
            clp::NetworkReader::cDefaultBufferPoolSize,
            clp::NetworkReader::cDefaultBufferSize,
            illegal_header_kv_pairs
    };
    auto const content = get_content(reader);
    REQUIRE(content.empty());
    REQUIRE(assert_curl_error_code(CURLE_BAD_FUNCTION_ARGUMENT, reader));
}
