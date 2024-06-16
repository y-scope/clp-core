#include "FileDescriptor.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <string_view>

#include "ErrorCode.hpp"
#include "type_utils.hpp"

namespace clp {
FileDescriptor::FileDescriptor(
        std::string_view path,
        Mode mode,
        CloseFailureCallback close_failure_callback
)
        : m_fd{open(path.data(), enum_to_underlying_type(mode))},
          m_mode{mode},
          m_close_failure_callback{close_failure_callback} {
    if (-1 == m_fd) {
        throw OperationFailed(
                ErrorCode_errno,
                __FILE__,
                __LINE__,
                "Failed to open file descriptor in path: " + std::string{path}
        );
    }
}

FileDescriptor::~FileDescriptor() {
    if (-1 == m_fd) {
        return;
    }
    if (0 != close(m_fd) && nullptr != m_close_failure_callback) {
        m_close_failure_callback(errno);
    }
}

auto FileDescriptor::get_size() const -> size_t {
    struct stat stat_result {};

    if (0 != fstat(m_fd, &stat_result)) {
        throw OperationFailed(
                ErrorCode_errno,
                __FILE__,
                __LINE__,
                "Failed to stat file using file descriptor."
        );
    }
    return static_cast<size_t>(stat_result.st_size);
}
}  // namespace clp
