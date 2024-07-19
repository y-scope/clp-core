#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include <Catch2/single_include/catch2/catch.hpp>
#include <error_handling/ErrorCode.hpp>

using clp::error_handling::ErrorCategory;
using clp::error_handling::ErrorCode;
using std::string;
using std::string_view;

namespace {
enum class AlwaysSuccessErrorCodeEnum : uint8_t {
    Success = 0
};

enum class BinaryErrorCodeEnum : uint8_t {
    Success = 0,
    Failure
};

using AlwaysSuccessErrorCode = ErrorCode<AlwaysSuccessErrorCodeEnum>;
using AlwaysSuccessErrorCategory = ErrorCategory<AlwaysSuccessErrorCodeEnum>;
using BinaryErrorCode = ErrorCode<BinaryErrorCodeEnum>;
using BinaryErrorCategory = ErrorCategory<BinaryErrorCodeEnum>;

constexpr string_view cAlwaysSuccessErrorCategoryName{"Always Success Error Code"};
constexpr string_view cBinaryTestErrorCategoryName{"Binary Error Code"};
constexpr string_view cSuccessErrMsg{"Success"};
constexpr string_view cFailureErrMsg{"Failure"};
constexpr string_view cUnrecognizedErrorCode{"Unrecognized Error Code"};
constexpr std::array cFailureConditions{std::errc::not_connected, std::errc::timed_out};
constexpr std::array cNoneFailureConditions{std::errc::broken_pipe, std::errc::address_in_use};
}  // namespace

namespace std {
template <>
struct is_error_code_enum<BinaryErrorCode> : std::true_type {};

template <>
struct is_error_code_enum<AlwaysSuccessErrorCode> : std::true_type {};
}  // namespace std

template <>
auto AlwaysSuccessErrorCategory::name() const noexcept -> char const* {
    return cAlwaysSuccessErrorCategoryName.data();
}

template <>
auto AlwaysSuccessErrorCategory::message(AlwaysSuccessErrorCodeEnum error_enum) const -> string {
    switch (error_enum) {
        case AlwaysSuccessErrorCodeEnum::Success:
            return string{cSuccessErrMsg};
        default:
            return string{cUnrecognizedErrorCode};
    }
}

template <>
auto BinaryErrorCategory::name() const noexcept -> char const* {
    return cBinaryTestErrorCategoryName.data();
}

template <>
auto BinaryErrorCategory::message(BinaryErrorCodeEnum error_enum) const -> string {
    switch (error_enum) {
        case BinaryErrorCodeEnum::Success:
            return string{cSuccessErrMsg};
        case BinaryErrorCodeEnum::Failure:
            return string{cFailureErrMsg};
        default:
            return string{cUnrecognizedErrorCode};
    }
}

template <>
auto BinaryErrorCategory::equivalent(
        BinaryErrorCodeEnum error_enum,
        std::error_condition const& condition
) const noexcept -> bool {
    switch (error_enum) {
        case BinaryErrorCodeEnum::Failure:
            return std::any_of(
                    cFailureConditions.cbegin(),
                    cFailureConditions.cend(),
                    [&](auto failure_condition) -> bool { return condition == failure_condition; }
            );
        default:
            return false;
    }
}

TEST_CASE("test_error_code_implementation", "[error_handling][ErrorCode]") {
    // Test error codes within the same error category
    BinaryErrorCode const success{BinaryErrorCodeEnum::Success};
    std::error_code const success_error_code{success};
    REQUIRE((success == success_error_code));
    REQUIRE((cSuccessErrMsg == success_error_code.message()));
    REQUIRE((BinaryErrorCode::get_category() == success_error_code.category()));
    REQUIRE((cBinaryTestErrorCategoryName == success_error_code.category().name()));

    BinaryErrorCode const failure{BinaryErrorCodeEnum::Failure};
    std::error_code const failure_error_code{failure};
    REQUIRE((failure == failure_error_code));
    REQUIRE((cFailureErrMsg == failure_error_code.message()));
    REQUIRE((BinaryErrorCode::get_category() == failure_error_code.category()));
    REQUIRE((cBinaryTestErrorCategoryName == failure_error_code.category().name()));
    std::for_each(
            cFailureConditions.cbegin(),
            cFailureConditions.cend(),
            [&](auto failure_condition) { REQUIRE((failure_error_code == failure_condition)); }
    );
    std::for_each(
            cNoneFailureConditions.cbegin(),
            cNoneFailureConditions.cend(),
            [&](auto none_failure_condition) {
                REQUIRE((failure_error_code != none_failure_condition));
            }
    );

    REQUIRE((success_error_code != failure_error_code));
    REQUIRE((success_error_code.category() == failure_error_code.category()));

    AlwaysSuccessErrorCode const always_success{AlwaysSuccessErrorCodeEnum::Success};
    std::error_code const always_success_error_code{always_success};
    REQUIRE((always_success_error_code == always_success));
    REQUIRE((cSuccessErrMsg == always_success_error_code.message()));
    REQUIRE((AlwaysSuccessErrorCode::get_category() == always_success_error_code.category()));
    REQUIRE((cAlwaysSuccessErrorCategoryName == always_success_error_code.category().name()));

    // Compare error codes from different error category
    // Error codes that have the same value or message won't be the same with each other if they are
    // from different error categories.
    REQUIRE((success_error_code.value() == always_success_error_code.value()));
    REQUIRE((success_error_code.message() == always_success_error_code.message()));
    REQUIRE((success_error_code.category() != always_success_error_code.category()));
    REQUIRE((success_error_code != always_success_error_code));
    REQUIRE((AlwaysSuccessErrorCode{AlwaysSuccessErrorCodeEnum::Success} != success_error_code));
    REQUIRE((BinaryErrorCode{BinaryErrorCodeEnum::Success} != always_success_error_code));
}
