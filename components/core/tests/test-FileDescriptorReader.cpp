#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <ystdlib/container/Array.hpp>

#include <Catch2/single_include/catch2/catch.hpp>

#include "../src/clp/FileDescriptorReader.hpp"
#include "../src/clp/FileReader.hpp"
#include "../src/clp/ReaderInterface.hpp"

namespace {
// Reused code starts
constexpr size_t cDefaultReaderBufferSize{1024};

[[nodiscard]] auto get_test_input_local_path() -> std::string;

[[nodiscard]] auto get_test_input_path_relative_to_tests_dir() -> std::filesystem::path;

/**
 * @param reader
 * @param read_buf_size The size of the buffer to use for individual reads from the reader.
 * @return All data read from the given reader.
 */
auto get_content(clp::ReaderInterface& reader, size_t read_buf_size = cDefaultReaderBufferSize)
        -> std::vector<char>;

auto get_test_input_local_path() -> std::string {
    std::filesystem::path const current_file_path{__FILE__};
    auto const tests_dir{current_file_path.parent_path()};
    return (tests_dir / get_test_input_path_relative_to_tests_dir()).string();
}

auto get_test_input_path_relative_to_tests_dir() -> std::filesystem::path {
    return std::filesystem::path{"test_log_files"} / "log.txt";
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
}  // namespace

// Reused code ends

TEST_CASE("file_descriptor_reader_basic", "[FileDescriptorReader]") {
    clp::FileReader ref_reader{get_test_input_local_path()};
    auto const expected{get_content(ref_reader)};

    clp::FileDescriptorReader reader{get_test_input_local_path()};
    auto const actual{get_content(reader)};
    REQUIRE((actual == expected));
}

TEST_CASE("file_descriptor_reader_with_offset_and_seek", "[FileDescriptorReader]") {
    constexpr size_t cOffset{319};

    clp::FileReader ref_reader{get_test_input_local_path()};
    ref_reader.seek_from_begin(cOffset);
    auto const expected{get_content(ref_reader)};
    auto const ref_end_pos{ref_reader.get_pos()};

    clp::FileDescriptorReader reader(get_test_input_local_path());
    reader.seek_from_begin(cOffset);
    auto const actual{get_content(reader)};
    auto const actual_end_pos{reader.get_pos()};

    REQUIRE((actual_end_pos == ref_end_pos));
    REQUIRE((actual == expected));
}
