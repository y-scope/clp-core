#include "writer.hpp"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

#include <fmt/core.h>
#include <msgpack.hpp>
#include <spdlog.h>

#include "../../Defs.h"
#include "../../ErrorCode.hpp"
#include "../../FileReader.hpp"
#include "../../FileWriter.hpp"
#include "../../TraceableException.hpp"
#include "../ArchiveMetadata.hpp"
#include "../Constants.hpp"
#include "Defs.hpp"

namespace clp::streaming_archive::single_file_archive {

namespace {
constexpr size_t cReadBlockSize = 4096;

/**
 * Gets the size of a file specified by `file_path` and adds it to file section `offset`.
 * @param file_path
 * @param[out] offset File section offset for the single-file archive. The returned offset
 * represents the starting position of the next file in single-file archive.
 * @throws OperationFailed if error getting file size.
 */
auto get_file_size_and_update_offset(std::filesystem::path const& file_path, uint64_t& offset)
        -> void;

/**
 * Generates metadata for the file section of a single-file archive. The metadata consists
 * of a list of file names and their corresponding starting offsets.
 *
 * @param multi_file_archive_path
 * @param next_segment_id
 * @return Vector containing a `FileInfo` struct for every file in the multi-file archive.
 * @throws Propagates `update_offset`'s exceptions.
 */
[[nodiscard]] auto
get_file_infos(std::filesystem::path const& multi_file_archive_path, segment_id_t next_segment_id)
        -> std::vector<FileInfo>;

/**
 * Combines file section metadata, multi-file archive metadata, and the number of segments into
 * single-file archive metadata. Once combined, serializes the metadata into MsgPack format.
 *
 * @param multi_file_archive_metadata
 * @param multi_file_archive_path
 * @param next_segment_id
 * @return Packed metadata.
 */
[[nodiscard]] auto pack_single_file_archive_metadata(
        ArchiveMetadata const& multi_file_archive_metadata,
        std::filesystem::path const& multi_file_archive_path,
        segment_id_t next_segment_id
) -> std::stringstream;

/**
 * Writes single-file archive header.
 *
 * @param archive_writer
 * @param packed_metadata_size
 */
auto write_archive_header(FileWriter& archive_writer, size_t packed_metadata_size) -> void;

/**
 * Writes single-file archive metadata.
 *
 * @param archive_writer
 * @param packed_metadata Packed metadata.
 */
auto write_archive_metadata(FileWriter& archive_writer, std::stringstream const& packed_metadata)
        -> void;

/**
 * Reads the content of a file and writes it to the single-file archive.
 * @param file_path
 * @param archive_writer
 * @throws OperationFailed if reading the file fails.
 */
auto write_archive_file(std::string const& file_path, FileWriter& archive_writer) -> void;

/**
 * Iterates over files in the multi-file archive copying their contents to the single-file archive.
 * Skips metadata file since already written in `write_archive_metadata`.
 *
 * @param archive_writer
 * @param multi_file_archive_path
 * @param next_segment_id
 * @throws Propagates `update_offset`'s exceptions.
 */
auto write_archive_files(
        FileWriter& archive_writer,
        std::filesystem::path const& multi_file_archive_path,
        segment_id_t next_segment_id
) -> void;

auto get_file_size_and_update_offset(std::filesystem::path const& file_path, uint64_t& offset)
        -> void {
    try {
        auto size = std::filesystem::file_size(file_path);
        offset += size;
    } catch (std::filesystem::filesystem_error const& e) {
        throw OperationFailed(
                ErrorCode_Failure,
                __FILENAME__,
                __LINE__,
                fmt::format("Failed to get file size: {}", e.what())
        );
    }
}

auto
get_file_infos(std::filesystem::path const& multi_file_archive_path, segment_id_t next_segment_id)
        -> std::vector<FileInfo> {
    std::vector<FileInfo> files;
    uint64_t offset = 0;

    for (auto const& static_archive_file_name : cStaticArchiveFileNames) {
        files.emplace_back(FileInfo{std::string(static_archive_file_name), offset});
        get_file_size_and_update_offset(multi_file_archive_path / static_archive_file_name, offset);
    }

    std::filesystem::path segment_dir_path = multi_file_archive_path / cSegmentsDirname;

    for (size_t i = 0; i < next_segment_id; ++i) {
        auto const segment_id = std::to_string(i);
        files.emplace_back(FileInfo{segment_id, offset});
        get_file_size_and_update_offset(segment_dir_path / segment_id, offset);
    }

    // Add sentinel indicating total size of all files.
    files.emplace_back(FileInfo{"", offset});

    // Decompression of large single-file archives will consume excessive memory since
    // single-file archives are not split.
    if (offset > cFileSizeWarningThreshold) {
        SPDLOG_WARN(
                "Single file archive size exceeded {}. "
                "The single-file archive format is not intended for large archives, "
                " consider using multi-file archive format instead.",
                cFileSizeWarningThreshold
        );
    }

    return files;
}

auto pack_single_file_archive_metadata(
        ArchiveMetadata const& multi_file_archive_metadata,
        std::filesystem::path const& multi_file_archive_path,
        segment_id_t next_segment_id
) -> std::stringstream {
    SingleFileArchiveMetadata single_file_archive{
            .archive_files = get_file_infos(multi_file_archive_path, next_segment_id),
            .archive_metadata = multi_file_archive_metadata,
            .num_segments = next_segment_id,
    };

    std::stringstream buf;
    msgpack::pack(buf, single_file_archive);

    return buf;
}

auto write_archive_header(FileWriter& archive_writer, size_t packed_metadata_size) -> void {
    SingleFileArchiveHeader header{
            .magic{},
            .version = cArchiveVersion,
            .metadata_size = packed_metadata_size,
            .unused{}
    };

    static_assert(cUnstructuredSfaMagicNumber.size() == header.magic.size());
    std::memcpy(
            &header.magic,
            cUnstructuredSfaMagicNumber.data(),
            cUnstructuredSfaMagicNumber.size()
    );
    archive_writer.write(reinterpret_cast<char const*>(&header), sizeof(header));
}

auto write_archive_metadata(FileWriter& archive_writer, std::stringstream const& packed_metadata)
        -> void {
    archive_writer.write(packed_metadata.str().data(), packed_metadata.str().size());
}

auto write_archive_file(std::filesystem::path const& file_path, FileWriter& archive_writer)
        -> void {
    FileReader reader(file_path.string());
    std::array<char, cReadBlockSize> read_buffer{};
    while (true) {
        size_t num_bytes_read{};
        ErrorCode const error_code
                = reader.try_read(read_buffer.data(), cReadBlockSize, num_bytes_read);
        if (ErrorCode_EndOfFile == error_code) {
            break;
        }
        if (ErrorCode_Success != error_code) {
            throw OperationFailed(error_code, __FILENAME__, __LINE__);
        }
        archive_writer.write(read_buffer.data(), num_bytes_read);
    }
}

auto write_archive_files(
        FileWriter& archive_writer,
        std::filesystem::path const& multi_file_archive_path,
        segment_id_t next_segment_id
) -> void {
    for (auto const& static_archive_file_name : cStaticArchiveFileNames) {
        std::filesystem::path static_archive_file_path
                = multi_file_archive_path / static_archive_file_name;
        write_archive_file(static_archive_file_path, archive_writer);
    }

    std::filesystem::path segment_dir_path = multi_file_archive_path / cSegmentsDirname;
    for (size_t i = 0; i < next_segment_id; ++i) {
        std::filesystem::path segment_path = segment_dir_path / std::to_string(i);
        write_archive_file(segment_path, archive_writer);
    }
}
}  // namespace

auto write_single_file_archive(
        ArchiveMetadata const& multi_file_archive_metadata,
        std::filesystem::path const& multi_file_archive_path,
        segment_id_t next_segment_id
) -> void {
    FileWriter archive_writer;
    std::filesystem::path single_file_archive_path
            = multi_file_archive_path.string()
              + std::string(single_file_archive::cUnstructuredSfaExtension);

    if (std::filesystem::exists(single_file_archive_path)) {
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }

    archive_writer.open(
            single_file_archive_path.string(),
            FileWriter::OpenMode::CREATE_FOR_WRITING
    );

    auto const packed_metadata = pack_single_file_archive_metadata(
            multi_file_archive_metadata,
            multi_file_archive_path,
            next_segment_id
    );

    write_archive_header(archive_writer, packed_metadata.str().size());
    write_archive_metadata(archive_writer, packed_metadata);
    write_archive_files(archive_writer, multi_file_archive_path, next_segment_id);

    archive_writer.close();
    try {
        std::filesystem::remove_all(multi_file_archive_path);
    } catch (std::filesystem::filesystem_error& e) {
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }
}
}  // namespace clp::streaming_archive::single_file_archive
