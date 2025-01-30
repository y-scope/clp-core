#include "ArchiveMetadata.hpp"

#include <sys/stat.h>

#include <fmt/core.h>

#include "../Array.hpp"

namespace clp::streaming_archive {
ArchiveMetadata::ArchiveMetadata(
        archive_format_version_t archive_format_version,
        std::string creator_id,
        uint64_t creation_idx
)
        : m_archive_format_version(archive_format_version),
          m_creator_id(std::move(creator_id)),
          m_creation_idx(creation_idx) {
    if (m_creator_id.length() > UINT16_MAX) {
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__);
    }
    m_creator_id_len = m_creator_id.length();

    // NOTE: We set this to the size of this metadata on disk; when adding new members that will be
    // written to disk, you must update this
    m_compressed_size += sizeof(m_archive_format_version) + sizeof(m_creator_id_len)
                         + m_creator_id.length() + sizeof(m_creation_idx)
                         + sizeof(m_uncompressed_size) + sizeof(m_begin_timestamp)
                         + sizeof(m_end_timestamp) + sizeof(m_compressed_size);
}

auto ArchiveMetadata::create_from_file_reader(FileReader& file_reader) -> ArchiveMetadata {
    struct stat file_stat{};
    if (auto const clp_rc = file_reader.try_fstat(file_stat);
        clp::ErrorCode::ErrorCode_Success != clp_rc)
    {
        throw OperationFailed(clp_rc, __FILENAME__, __LINE__);
    }

    clp::Array<char> buf(file_stat.st_size);
    if (auto const clp_rc = file_reader.try_read_exact_length(buf.data(), buf.size());
        clp::ErrorCode::ErrorCode_Success != clp_rc)
    {
        throw OperationFailed(clp_rc, __FILENAME__, __LINE__);
    }

    ArchiveMetadata metadata;
    msgpack::object_handle oh = msgpack::unpack(buf.data(), buf.size());
    msgpack::object obj = oh.get();
    obj.convert(metadata);
    return metadata;
}

void ArchiveMetadata::expand_time_range(epochtime_t begin_timestamp, epochtime_t end_timestamp) {
    if (begin_timestamp < m_begin_timestamp) {
        m_begin_timestamp = begin_timestamp;
    }
    if (end_timestamp > m_end_timestamp) {
        m_end_timestamp = end_timestamp;
    }
}

void ArchiveMetadata::write_to_file(FileWriter& file_writer) const {
    std::ostringstream buf;
    msgpack::pack(buf, *this);
    auto const& string_buf = buf.str();
    file_writer.write(string_buf.data(), string_buf.size());
}
}  // namespace clp::streaming_archive
