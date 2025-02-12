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
 * Generates metadata for the file section of a single-file archive. The metadata consists
 * of a list of file names and their corresponding starting offsets.
 *
 * @param multi_file_archive_path
 * @param num_segments
 * @return Vector containing a `FileInfo` struct for every file in the multi-file archive.
 * @throws `std::filesystem::filesystem_error` if `stat` on archive file fails.
 */
[[nodiscard]] auto
get_archive_file_infos(std::filesystem::path const& multi_file_archive_path, size_t num_segments)
        -> std::vector<FileInfo>;

/**
 * Combines file section metadata and the number of segments into single-file archive metadata,
 * then serializes the metadata into MsgPack format.
 *
 * @param multi_file_archive_path
 * @param num_segments
 * @return Packed metadata.
 */
[[nodiscard]] auto pack_single_file_archive_metadata(
        std::filesystem::path const& multi_file_archive_path,
        size_t num_segments
) -> std::stringstream;

/**
 * Writes single-file archive header.
 *
 * @param single_file_archive_writer
 * @param packed_metadata_size
 */
auto write_archive_header(FileWriter& single_file_archive_writer, size_t packed_metadata_size)
        -> void;

/**
 * Writes packed single-file archive metadata.
 *
 * @param single_file_archive_writer
 * @param packed_metadata Packed metadata.
 */
auto write_packed_archive_metadata(
        FileWriter& single_file_archive_writer,
        std::stringstream const& packed_metadata
) -> void;

/**
 * Reads the content of a file and writes it to the single-file archive.
 * @param file_path
 * @param single_file_archive_writer
 * @throws OperationFailed if reading the file fails.
 */
auto
write_archive_file(std::filesystem::path const& file_path, FileWriter& single_file_archive_writer)
        -> void;

/**
 * Iterates over files in the multi-file archive and copies their contents to the single-file
 * archive.
 *
 * @param single_file_archive_writer
 * @param multi_file_archive_path
 * @param num_segments
 */
auto write_archive_files(
        FileWriter& single_file_archive_writer,
        std::filesystem::path const& multi_file_archive_path,
        size_t num_segments
) -> void;

auto
get_archive_file_infos(std::filesystem::path const& multi_file_archive_path, size_t num_segments)
        -> std::vector<FileInfo> {
    std::vector<FileInfo> files;
    uint64_t offset{};

    for (auto const& static_archive_file_name : cStaticArchiveFileNames) {
        files.emplace_back(FileInfo{std::string(static_archive_file_name), offset});
        offset += std::filesystem::file_size(multi_file_archive_path / static_archive_file_name);
    }

    std::filesystem::path segment_dir_path = multi_file_archive_path / cSegmentsDirname;

    for (size_t i = 0; i < num_segments; ++i) {
        auto const segment_id = std::to_string(i);
        files.emplace_back(FileInfo{segment_id, offset});
        offset += std::filesystem::file_size(segment_dir_path / segment_id);
    }

    // Add sentinel indicating total size of all files.
    files.emplace_back(FileInfo{std::string(cFileInfoSentinelName), offset});

    return files;
}

auto pack_single_file_archive_metadata(
        std::filesystem::path const& multi_file_archive_path,
        size_t num_segments
) -> std::stringstream {
    SingleFileArchiveMetadata single_file_archive{
            .archive_files = get_archive_file_infos(multi_file_archive_path, num_segments),
            .num_segments = num_segments,
    };

    std::stringstream buf;
    msgpack::pack(buf, single_file_archive);

    return buf;
}

auto write_archive_header(FileWriter& single_file_archive_writer, size_t packed_metadata_size)
        -> void {
    SingleFileArchiveHeader header{
            .magic = cUnstructuredSfaMagicNumber,
            .version = cVersion,
            .metadata_size = packed_metadata_size,
            .unused{}
    };

    single_file_archive_writer.write(reinterpret_cast<char const*>(&header), sizeof(header));
}

auto write_packed_archive_metadata(
        FileWriter& single_file_archive_writer,
        std::stringstream const& packed_metadata
) -> void {
    single_file_archive_writer.write(packed_metadata.str().data(), packed_metadata.str().size());
}

auto
write_archive_file(std::filesystem::path const& file_path, FileWriter& single_file_archive_writer)
        -> void {
    FileReader reader(file_path.string());
    std::array<char, cReadBlockSize> read_buffer{};
    while (true) {
        size_t num_bytes_read{};
        auto const error_code = reader.try_read(read_buffer.data(), cReadBlockSize, num_bytes_read);
        if (ErrorCode_EndOfFile == error_code) {
            break;
        }
        if (ErrorCode_Success != error_code) {
            throw OperationFailed(error_code, __FILENAME__, __LINE__);
        }
        single_file_archive_writer.write(read_buffer.data(), num_bytes_read);
    }
}

auto write_archive_files(
        FileWriter& single_file_archive_writer,
        std::filesystem::path const& multi_file_archive_path,
        size_t num_segments
) -> void {
    for (auto const& static_archive_file_name : cStaticArchiveFileNames) {
        auto const static_archive_file_path{multi_file_archive_path / static_archive_file_name};
        write_archive_file(static_archive_file_path, single_file_archive_writer);
    }

    auto const segment_dir_path{multi_file_archive_path / cSegmentsDirname};
    for (size_t i = 0; i < num_segments; ++i) {
        auto const segment_path{segment_dir_path / std::to_string(i)};
        write_archive_file(segment_path, single_file_archive_writer);
    }
}
}  // namespace

auto
write_single_file_archive(std::filesystem::path const& multi_file_archive_path, size_t num_segments)
        -> void {
    FileWriter single_file_archive_writer;
    std::filesystem::path single_file_archive_path{
            multi_file_archive_path.string()
            + std::string(single_file_archive::cUnstructuredSfaExtension)
    };

    if (std::filesystem::exists(single_file_archive_path)) {
        throw OperationFailed(ErrorCode_Failure, __FILENAME__, __LINE__);
    }

    single_file_archive_writer.open(
            single_file_archive_path.string(),
            FileWriter::OpenMode::CREATE_FOR_WRITING
    );

    auto const packed_metadata
            = pack_single_file_archive_metadata(multi_file_archive_path, num_segments);

    write_archive_header(single_file_archive_writer, packed_metadata.str().size());
    write_packed_archive_metadata(single_file_archive_writer, packed_metadata);
    write_archive_files(single_file_archive_writer, multi_file_archive_path, num_segments);

    single_file_archive_writer.close();
    try {
        std::filesystem::remove_all(multi_file_archive_path);
    } catch (std::filesystem::filesystem_error& e) {
        SPDLOG_WARN("Failed to delete multi-file archive: {}", e.what());
    }
}
}  // namespace clp::streaming_archive::single_file_archive
