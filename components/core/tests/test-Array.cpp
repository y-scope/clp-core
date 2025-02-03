#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include <ystdlib/container/Array.hpp>

#include <Catch2/single_include/catch2/catch.hpp>

using std::vector;
using ystdlib::container::Array;

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("array_fundamental", "[clp::Array]") {
    Array<int> clp_array_empty{0};
    REQUIRE(clp_array_empty.empty());
    // NOLINTNEXTLINE(readability-container-size-empty)
    REQUIRE((0 == clp_array_empty.size()));
    REQUIRE((clp_array_empty.begin() == clp_array_empty.end()));

    constexpr size_t cBufferSize{1024};

    vector<int> std_vector;
    for (int i{0}; i < cBufferSize; ++i) {
        std_vector.push_back(i);
    }

    Array<int> clp_array{cBufferSize};
    auto const& clp_array_const_ref = clp_array;
    std::for_each(clp_array_const_ref.begin(), clp_array_const_ref.end(), [](int i) -> void {
        REQUIRE((0 == i));
    });

    std::copy(std_vector.cbegin(), std_vector.cend(), clp_array.begin());

    size_t idx{0};
    for (auto const val : clp_array) {
        REQUIRE((val == clp_array.at(idx)));
        REQUIRE((val == std_vector.at(idx)));
        ++idx;
    }
    REQUIRE((cBufferSize == idx));
    REQUIRE_THROWS(clp_array.at(idx));
}

TEST_CASE("array_default_initializable", "[clp::Array]") {
    Array<std::string> clp_array_empty{0};
    REQUIRE(clp_array_empty.empty());
    // NOLINTNEXTLINE(readability-container-size-empty)
    REQUIRE((0 == clp_array_empty.size()));
    REQUIRE((clp_array_empty.begin() == clp_array_empty.end()));

    vector<std::string> const std_vector{"yscope", "clp", "clp::Array", "default_initializable"};
    Array<std::string> clp_array{std_vector.size()};
    std::copy(std_vector.cbegin(), std_vector.cend(), clp_array.begin());
    REQUIRE(std::equal(std_vector.begin(), std_vector.end(), clp_array.begin(), clp_array.end()));
}
