#ifndef CLP_AWS_AWSAUTHENTICATIONSIGNER_HPP
#define CLP_AWS_AWSAUTHENTICATIONSIGNER_HPP

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../ErrorCode.hpp"
#include "../TraceableException.hpp"

namespace clp::aws {
/**
 * Class for a parsed S3 URL.
 */
class S3Url {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(
                ErrorCode error_code,
                char const* const filename,
                int line_number,
                std::string message = "S3Url operation failed"
        )
                : TraceableException{error_code, filename, line_number},
                  m_message{std::move(message)} {}

        // Methods
        [[nodiscard]] auto what() const noexcept -> char const* override {
            return m_message.c_str();
        }

    private:
        std::string m_message;
    };

    // Constructor
    explicit S3Url(std::string const& url);

    // Methods
    [[nodiscard]] auto get_region() const -> std::string_view { return m_region; }

    [[nodiscard]] auto get_bucket() const -> std::string_view { return m_bucket; }

    [[nodiscard]] auto get_key() const -> std::string_view { return m_key; }

    [[nodiscard]] auto get_host() const -> std::string_view { return m_host; }

private:
    std::string m_region;
    std::string m_end_point;
    std::string m_bucket;
    std::string m_key;
    std::string m_host;
};

/**
 * Class for signing AWS requests based on AWS Signature Version 4.
 */
class AwsAuthenticationSigner {
public:
    // Constants
    // Default expire time of presigned URL in seconds
    static constexpr std::chrono::seconds cDefaultExpireTime{86'400};
    static constexpr std::string_view cHttpGetMethod{"GET"};

    // Constructors
    AwsAuthenticationSigner(
            std::string access_key_id,
            std::string secret_access_key,
            std::optional<std::string> session_token
    )
            : m_access_key_id{std::move(access_key_id)},
              m_secret_access_key{std::move(secret_access_key)},
              m_session_token{std::move(session_token)} {}

    // Methods
    /**
     * Generates a presigned S3 URL using AWS Signature Version 4 protocol.
     * NOTE: the current implementation only supports generating URLs for HTTP GET operations.
     * @param s3_url
     * @param presigned_url Returns the generated presigned URL.
     * @return ErrorCode_Success on success.
     * @return Same as `get_sha256_hash` and `AwsAuthenticationSigner::get_signature` on failure.
     */
    [[nodiscard]] auto generate_presigned_url(S3Url const& s3_url, std::string& presigned_url) const
            -> ErrorCode;

private:
    /**
     * Generates the canonical query string.
     * @param scope
     * @param timestamp
     * @return The canonical query string.
     */
    [[nodiscard]] auto
    get_canonical_query_string(std::string_view scope, std::string_view timestamp) const
            -> std::string;

    /**
     * Gets the signature signing key for the request.
     * @param region
     * @param date
     * @param signing_key Returns the signing key.
     * @return ErrorCode_Success on success.
     * @return Same as `get_hmac_sha256_hash` on Failure.
     */
    [[nodiscard]] auto get_signing_key(
            std::string_view region,
            std::string_view date,
            std::vector<unsigned char>& signing_key
    ) const -> ErrorCode;

    /**
     * Signs the `string_to_sign` with a generated signing key.
     * @param region
     * @param date
     * @param string_to_sign `StringToSign` required by AWS Signature Version 4 protocol.
     * @param signature Returns the signature.
     * @return ErrorCode_Success on success.
     * @return Same as `get_hmac_sha256_hash` on Failure.
     */
    [[nodiscard]] auto get_signature(
            std::string_view region,
            std::string_view date,
            std::string_view string_to_sign,
            std::vector<unsigned char>& signature
    ) const -> ErrorCode;

    // Variables
    std::string m_access_key_id;
    std::string m_secret_access_key;
    std::optional<std::string> m_session_token;
};
}  // namespace clp::aws

#endif  // CLP_AWS_AWSAUTHENTICATIONSIGNER_HPP
