#ifndef CLP_S_INPUTCONFIG_HPP
#define CLP_S_INPUTCONFIG_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace clp_s {
// Constants used for input configuration
constexpr char cAwsAccessKeyIdEnvVar[] = "AWS_ACCESS_KEY_ID";
constexpr char cAwsSecretAccessKeyEnvVar[] = "AWS_SECRET_ACCESS_KEY";
constexpr char cAwsSessionTokenEnvVar[] = "AWS_SESSION_TOKEN";

/**
 * Enum class defining the source of a resource.
 */
enum class InputSource : uint8_t {
    Filesystem,
    Network
};

/**
 * Enum class defining the authentication method required for accessing a resource.
 */
enum class AuthMethod : uint8_t {
    None,
    S3PresignedUrlV4
};

/**
 * Struct encapsulating information needed to authenticate network requests.
 */
struct NetworkAuthOption {
    AuthMethod method{AuthMethod::None};
};

/**
 * Struct representing a resource path with its source type.
 */
struct Path {
    InputSource source{InputSource::Filesystem};
    std::string path;
};

/**
 * Determines the input source for a given raw path or url.
 * @param path
 * @return the InputSource for the given path
 */
[[nodiscard]] auto get_source_for_path(std::string_view const path) -> InputSource;

/**
 * Determines the input source for a given raw path or url and converts the path into a Path object.
 * @param path
 * @return a Path object representing the raw path or url
 */
[[nodiscard]] auto get_path_object_for_raw_path(std::string_view const path) -> Path;

/**
 * Recursively collects all file paths from the given raw path, including the path itself.
 * @param path
 * @param files Returned paths
 * @return true on success, false otherwise
 */
auto get_input_files_for_raw_path(std::string_view const path, std::vector<Path>& files) -> bool;

/**
 * Recursively collects all file paths that are children of the the given Path, including the Path
 * itself.
 * @param path
 * @param files Returned paths
 * @return true on success, false otherwise
 */
[[nodiscard]] auto get_input_files_for_path(Path const& path, std::vector<Path>& files) -> bool;

/**
 * Collects all archives that are children of the given raw path, including the path itself.
 * @param path
 * @param archives Returned archives
 * @return true on success, false otherwise
 */
[[nodiscard]] auto
get_input_archives_for_raw_path(std::string_view const path, std::vector<Path>& archives) -> bool;

/**
 * Collects all archives from the given Path, including the Path itself.
 * @param path
 * @param archives Returned archives
 * @return true on success, false otherwise
 */
[[nodiscard]] auto get_input_archives_for_path(Path const& path, std::vector<Path>& archives)
        -> bool;

/**
 * Determines the archive id of an archive based on the archive path.
 * @param path
 * @param archive_id Returned archive id
 * @return true on success, false otherwise
 */
[[nodiscard]] auto get_archive_id_from_path(Path const& archive_path, std::string& archive_id)
        -> bool;
}  // namespace clp_s

#endif  // CLP_S_INPUTCONFIG_HPP
