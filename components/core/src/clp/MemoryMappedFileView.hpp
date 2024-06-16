#ifndef CLP_MEMORYMAPPEDFILEVIEW_HPP
#define CLP_MEMORYMAPPEDFILEVIEW_HPP

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "ErrorCode.hpp"
#include "TraceableException.hpp"

namespace clp {
/**
 * A class for mapping a read-only file into memory. It maintains the memory buffer created by the
 * underlying `mmap` system call and provides methods to get a view of the memory buffer.
 */
class MemoryMappedFileView {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        OperationFailed(
                ErrorCode error_code,
                char const* const filename,
                int line_number,
                std::string msg
        )
                : TraceableException{error_code, filename, line_number},
                  m_msg{std::move(msg)} {}

        [[nodiscard]] auto what() const noexcept -> char const* override { return m_msg.c_str(); }

    private:
        std::string m_msg;
    };

    // Constructors
    /**
     * @param path The path of the file to map.
     */
    MemoryMappedFileView(std::string_view path);

    // Destructor
    ~MemoryMappedFileView();

    // Disable copy/move constructors/assignment operators
    MemoryMappedFileView(MemoryMappedFileView const&) = delete;
    MemoryMappedFileView(MemoryMappedFileView&&) = delete;
    auto operator=(MemoryMappedFileView const&) -> MemoryMappedFileView& = delete;
    auto operator=(MemoryMappedFileView&&) -> MemoryMappedFileView& = delete;

    /**
     * @return A view to the mapped file in memory.
     */
    [[nodiscard]] auto get_view() const -> std::span<char> {
        return std::span<char>{static_cast<char*>(m_data), m_buf_size};
    }

private:
    void* m_data{nullptr};
    size_t m_buf_size{0};
};
}  // namespace clp

#endif
