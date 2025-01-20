#include "AwsAuthenticationSigner.hpp"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include <boost/regex.hpp>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <string_utils/string_utils.hpp>

#include "../ErrorCode.hpp"
#include "../hash_utils.hpp"
#include "../type_utils.hpp"
#include "constants.hpp"

using clp::string_utils::is_alphabet;
using clp::string_utils::is_decimal_digit;
using std::string;
using std::string_view;
using std::vector;

namespace clp::aws {
namespace {
/**
 * @param c
 * @return Whether `c` is an unreserved character AWS Signature Version 4 protocol specifies.
 */
[[nodiscard]] auto is_unreserved_characters(char c) -> bool;

/**
 * Gets the formatted timestamp string specified by AWS Signature Version 4 format.
 * @param timestamp
 * @return The formatted timestamp string.
 */
[[nodiscard]] auto get_formatted_timestamp_string(
        std::chrono::system_clock::time_point const& timestamp
) -> string;

/**
 * Gets the formatted date string specified by AWS Signature Version 4 format.
 * @param timestamp
 * @return The formatted date string.
 */
[[nodiscard]] auto get_formatted_date_string(std::chrono::system_clock::time_point const& timestamp)
        -> string;

/**
 * Gets the string to sign required by AWS Signature Version 4 protocol.
 * @param scope
 * @param timestamp
 * @param canonical_request
 * @param string_to_sign Outputs the string to sign.
 * @return ErrorCode_Success on success.
 * @return Same as `get_sha256_hash` on failure.
 */
[[nodiscard]] auto get_string_to_sign(
        string_view scope,
        string_view timestamp,
        string_view canonical_request,
        string& string_to_sign
) -> ErrorCode;

/**
 * Encodes the Canonical URI as specified by AWS Signature Version 4's UriEncode.
 * This method encodes the forward slash character, '/', everywhere except in the object key.
 * @param uri
 * @param is_object_key
 * @return The encoded URI.
 */
[[nodiscard]] auto encode_uri(string_view uri, bool is_object_key) -> string;

/**
 * @param date
 * @param region
 * @return The formatted scope required by ASW Signature Version 4 protocol.
 */
[[nodiscard]] auto get_scope(string_view date, string_view region) -> string;

/**
 * @param url
 * @param query_string
 * @return Formatted canonical request string.
 */
[[nodiscard]] auto get_canonical_request(S3Url const& url, string_view query_string) -> string;

auto is_unreserved_characters(char c) -> bool {
    return is_alphabet(c) || is_decimal_digit(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

auto get_formatted_timestamp_string(std::chrono::system_clock::time_point const& timestamp)
        -> string {
    return fmt::format("{:%Y%m%dT%H%M%SZ}", timestamp);
}

auto get_formatted_date_string(std::chrono::system_clock::time_point const& timestamp) -> string {
    return fmt::format("{:%Y%m%d}", timestamp);
}

auto get_string_to_sign(
        string_view scope,
        string_view timestamp,
        string_view canonical_request,
        string& string_to_sign
) -> ErrorCode {
    vector<unsigned char> signed_canonical_request;
    if (auto const error_code = get_sha256_hash(
                {size_checked_pointer_cast<unsigned char const>(canonical_request.data()),
                 canonical_request.size()},
                signed_canonical_request
        );
        ErrorCode_Success != error_code)
    {
        return error_code;
    }
    auto const signed_canonical_request_str = convert_to_hex_string(
            {signed_canonical_request.data(), signed_canonical_request.size()}
    );
    string_to_sign = fmt::format(
            "{}\n{}\n{}\n{}",
            cAws4HmacSha256,
            timestamp,
            scope,
            signed_canonical_request_str
    );
    return ErrorCode_Success;
}

auto encode_uri(string_view uri, bool is_object_key) -> string {
    string encoded_uri;

    for (auto const c : uri) {
        if (is_unreserved_characters(c) || ('/' == c) && is_object_key) {
            encoded_uri += c;
        } else {
            encoded_uri += fmt::format("%{:02X}", c);
        }
    }

    return encoded_uri;
}

auto get_scope(string_view date, string_view region) -> string {
    return fmt::format("{}/{}/{}/{}", date, region, cS3Service, cAws4Request);
}

auto get_canonical_request(S3Url const& url, string_view query_string) -> string {
    auto const uri_to_encode = fmt::format("/{}", url.get_key());
    return fmt::format(
            "{}\n{}\n{}\n{}:{}\n\n{}\n{}",
            AwsAuthenticationSigner::cHttpGetMethod,
            encode_uri(uri_to_encode, true),
            query_string,
            cDefaultSignedHeaders,
            url.get_host(),
            cDefaultSignedHeaders,
            cUnsignedPayload
    );
}
}  // namespace

S3Url::S3Url(string const& url) {
    // Virtual-hosted-style HTTP URL format: https://[bucket].s3.[region].[endpoint]/[key]
    boost::regex const host_style_url_regex{
            R"(https://(?<bucket>[a-z0-9.-]+)\.s3(\.(?<region>[a-z0-9-]+))?)"
            R"(\.(?<endpoint>[a-z0-9.-]+)/(?<key>[^?]+).*)"
    };
    // Path-style HTTP URL format: https://s3.[region].[endpoint]/[bucket]/[key]
    boost::regex const path_style_url_regex{
            R"(https://s3(\.(?<region>[a-z0-9-]+))?)"
            R"(\.(?<endpoint>[a-z0-9.-]+)/(?<bucket>[a-z0-9.-]+)/(?<key>[^?]+).*)"
    };

    if (boost::smatch match; boost::regex_match(url, match, host_style_url_regex)
                             || boost::regex_match(url, match, path_style_url_regex))
    {
        m_region = match["region"];
        m_bucket = match["bucket"];
        m_key = match["key"];
        m_end_point = match["endpoint"];
    } else {
        throw OperationFailed(
                ErrorCode_BadParam,
                __FILENAME__,
                __LINE__,
                "Invalid S3 HTTP URL format: " + url
        );
    }

    if (cAwsEndpoint != m_end_point) {
        throw OperationFailed(
                ErrorCode_BadParam,
                __FILENAME__,
                __LINE__,
                "Invalid S3 endpoint: " + m_end_point
        );
    }

    if (m_region.empty()) {
        m_region = cDefaultRegion;
    }
    m_host = fmt::format("{}.s3.{}.{}", m_bucket, m_region, m_end_point);
}

auto
AwsAuthenticationSigner::generate_presigned_url(S3Url const& s3_url, string& presigned_url) const
        -> ErrorCode {
    auto const s3_region = s3_url.get_region();

    auto const now = std::chrono::system_clock::now();
    auto const timestamp = get_formatted_timestamp_string(now);
    auto const date = get_formatted_date_string(now);

    auto const scope = get_scope(date, s3_region);
    auto const canonical_query_string = get_canonical_query_string(scope, timestamp);

    auto const canonical_request = get_canonical_request(s3_url, canonical_query_string);

    string string_to_sign;
    if (auto const error_code
        = get_string_to_sign(scope, timestamp, canonical_request, string_to_sign);
        ErrorCode_Success != error_code)
    {
        return error_code;
    }

    vector<unsigned char> signature;
    if (auto const error_code = get_signature(s3_region, date, string_to_sign, signature);
        ErrorCode_Success != error_code)
    {
        return error_code;
    }
    auto const signature_str = convert_to_hex_string({signature.data(), signature.size()});

    presigned_url = fmt::format(
            "https://{}/{}?{}&{}={}",
            s3_url.get_host(),
            s3_url.get_key(),
            canonical_query_string,
            cXAmzSignature,
            signature_str
    );
    return ErrorCode_Success;
}

auto
AwsAuthenticationSigner::get_canonical_query_string(string_view scope, string_view timestamp) const
        -> string {
    auto const uri = fmt::format("{}/{}", m_access_key_id, scope);
    std::string session_token_parameter;
    if (m_session_token.has_value()) {
        session_token_parameter = fmt::format(
                "&{}={}",
                cAmzSecurityToken,
                encode_uri(m_session_token.value(), false)
        );
    }
    return fmt::format(
            "{}={}&{}={}&{}={}&{}={}{}&{}={}",
            cXAmzAlgorithm,
            cAws4HmacSha256,
            cXAmzCredential,
            encode_uri(uri, false),
            cXAmzDate,
            timestamp,
            cXAmzExpires,
            cDefaultExpireTime.count(),
            session_token_parameter,
            cXAmzSignedHeaders,
            cDefaultSignedHeaders
    );
}

auto AwsAuthenticationSigner::get_signing_key(
        string_view region,
        string_view date,
        vector<unsigned char>& signing_key
) const -> ErrorCode {
    auto const key = fmt::format("{}{}", cAws4, m_secret_access_key);

    vector<unsigned char> date_key;
    if (auto const error_code = get_hmac_sha256_hash(
                {size_checked_pointer_cast<unsigned char const>(date.data()), date.size()},
                {size_checked_pointer_cast<unsigned char const>(key.data()), key.size()},
                date_key
        );
        error_code != ErrorCode_Success)
    {
        return error_code;
    }

    vector<unsigned char> date_region_key;
    if (auto const error_code = get_hmac_sha256_hash(
                {size_checked_pointer_cast<unsigned char const>(region.data()), region.size()},
                {size_checked_pointer_cast<unsigned char const>(date_key.data()), date_key.size()},
                date_region_key
        );
        error_code != ErrorCode_Success)
    {
        return error_code;
    }

    vector<unsigned char> date_region_service_key;
    if (auto const error_code = get_hmac_sha256_hash(
                {size_checked_pointer_cast<unsigned char const>(cS3Service.data()),
                 cS3Service.size()},
                {size_checked_pointer_cast<unsigned char const>(date_region_key.data()),
                 date_region_key.size()},
                date_region_service_key
        );
        error_code != ErrorCode_Success)
    {
        return error_code;
    }

    if (auto const error_code = get_hmac_sha256_hash(
                {size_checked_pointer_cast<unsigned char const>(cAws4Request.data()),
                 cAws4Request.size()},
                {size_checked_pointer_cast<unsigned char const>(date_region_service_key.data()),
                 date_region_service_key.size()},
                signing_key
        );
        error_code != ErrorCode_Success)
    {
        return error_code;
    }

    return ErrorCode_Success;
}

auto AwsAuthenticationSigner::get_signature(
        string_view region,
        string_view date,
        string_view string_to_sign,
        vector<unsigned char>& signature
) const -> ErrorCode {
    vector<unsigned char> signing_key;
    if (auto const error_code = get_signing_key(region, date, signing_key);
        ErrorCode_Success != error_code)
    {
        return error_code;
    }

    if (auto const error_code = get_hmac_sha256_hash(
                {size_checked_pointer_cast<unsigned char const>(string_to_sign.data()),
                 string_to_sign.size()},
                {size_checked_pointer_cast<unsigned char const>(signing_key.data()),
                 signing_key.size()},
                signature
        );
        ErrorCode_Success != error_code)
    {
        return error_code;
    }
    return ErrorCode_Success;
}
}  // namespace clp::aws
