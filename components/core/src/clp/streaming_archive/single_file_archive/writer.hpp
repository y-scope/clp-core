#ifndef CLP_STREAMING_ARCHIVE_SINGLE_FILE_ARCHIVE_WRITER_HPP
#define CLP_STREAMING_ARCHIVE_SINGLE_FILE_ARCHIVE_WRITER_HPP

#include <filesystem>
#include <string>

#include <msgpack.hpp>

#include "../../Defs.h"
#include "../../ErrorCode.hpp"
#include "../../TraceableException.hpp"
#include "../ArchiveMetadata.hpp"

namespace clp::streaming_archive::single_file_archive {

class OperationFailed : public TraceableException {
public:
    // Constructors
    OperationFailed(
            ErrorCode error_code,
            char const* const filename,
            int line_number,
            std::string message = "streaming_archive::single_file_archive operation failed"
    )
            : TraceableException{error_code, filename, line_number},
              m_message{std::move(message)} {}

    // Methods
    [[nodiscard]] auto what() const noexcept -> char const* override { return m_message.c_str(); }

private:
    std::string m_message;
};

/**
 * Writes header, metadata and archive files in single-file format then
 * removes existing multi-file archive.
 *
 * @param multi_file_archive_metadata
 * @param multi_file_archive_path
 * @param next_segment_id ID of the next segment to be created in the archive.
 * @throws OperationFailed if single-file archive path already exists.
 */
auto write_single_file_archive(
        ArchiveMetadata const& multi_file_archive_metadata,
        std::filesystem::path const& multi_file_archive_path,
        segment_id_t next_segment_id
) -> void;

}  // namespace clp::streaming_archive::single_file_archive

#endif  // CLP_STREAMING_ARCHIVE_SINGLE_FILE_ARCHIVE_WRITER_HPP
