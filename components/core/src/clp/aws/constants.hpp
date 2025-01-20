#ifndef CLP_AWS_CONSTANTS_HPP
#define CLP_AWS_CONSTANTS_HPP

#include <string_view>

namespace clp::aws {
// Endpoint
constexpr std::string_view cAwsEndpoint{"amazonaws.com"};

// Query String Parameter Names
constexpr std::string_view cXAmzAlgorithm{"X-Amz-Algorithm"};
constexpr std::string_view cXAmzCredential{"X-Amz-Credential"};
constexpr std::string_view cXAmzDate{"X-Amz-Date"};
constexpr std::string_view cXAmzExpires{"X-Amz-Expires"};
constexpr std::string_view cAmzSecurityToken{"X-Amz-Security-Token"};
constexpr std::string_view cXAmzSignature{"X-Amz-Signature"};
constexpr std::string_view cXAmzSignedHeaders{"X-Amz-SignedHeaders"};

// Other Constants
constexpr std::string_view cAws4{"AWS4"};
constexpr std::string_view cAws4Request{"aws4_request"};
constexpr std::string_view cAws4HmacSha256{"AWS4-HMAC-SHA256"};
constexpr std::string_view cDefaultSignedHeaders{"host"};
constexpr std::string_view cDefaultRegion{"us-east-1"};
constexpr std::string_view cS3Service{"s3"};
constexpr std::string_view cUnsignedPayload{"UNSIGNED-PAYLOAD"};
}  // namespace clp::aws

#endif  // CLP_AWS_CONSTANTS_HPP
