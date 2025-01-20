#ifndef CLP_STREAMING_ARCHIVE_SINGLE_FILE_ARCHIVE_DEFS_HPP
#define CLP_STREAMING_ARCHIVE_SINGLE_FILE_ARCHIVE_DEFS_HPP

#include <cstdint>
#include <string>

#include "../../Defs.h"
#include "../ArchiveMetadata.hpp"
#include "../Constants.hpp"
#include "msgpack.hpp"

namespace clp::streaming_archive::single_file_archive {

using single_file_archive_format_version_t = uint32_t;

// Single file archive version.
constexpr uint8_t cArchiveMajorVersion{0};
constexpr uint8_t cArchiveMinorVersion{1};
constexpr uint16_t cArchivePatchVersion{1};
constexpr single_file_archive_format_version_t cArchiveVersion{
        cArchiveMajorVersion << 24 | cArchiveMinorVersion << 16 | cArchivePatchVersion
};

static constexpr size_t cNumMagicNumberChars{4};
static constexpr std::array<uint8_t, cNumMagicNumberChars>
        cUnstructuredSfaMagicNumber{'Y', 'C', 'L', 'P'};
static constexpr std::string_view cUnstructuredSfaExtension{".clp"};
static constexpr size_t cFileSizeWarningThreshold{100L * 1024 * 1024};

static constexpr size_t cNumStaticFiles{5};
constexpr std::array<char const*, cNumStaticFiles> cStaticArchiveFileNames{
        cMetadataDBFileName,
        cLogTypeDictFilename,
        cLogTypeSegmentIndexFilename,
        cVarDictFilename,
        cVarSegmentIndexFilename
};

static constexpr size_t cNumUnused{6};

struct __attribute__((packed)) SingleFileArchiveHeader {
    std::array<uint8_t, cNumMagicNumberChars> magic;
    single_file_archive_format_version_t version;
    uint64_t metadata_size;
    std::array<uint64_t, cNumUnused> unused;
};

struct FileInfo {
    std::string name;
    uint64_t offset;
    // Variables are renamed when serialized to match single-file archive specification.
    MSGPACK_DEFINE_MAP(MSGPACK_NVP("n", name), MSGPACK_NVP("o", offset));
};

struct SingleFileArchiveMetadata {
    std::vector<FileInfo> archive_files;
    ArchiveMetadata archive_metadata;
    uint64_t num_segments;
    MSGPACK_DEFINE_MAP(archive_files, archive_metadata, num_segments);
};
}  // namespace clp::streaming_archive::single_file_archive

#endif  // CLP_STREAMING_ARCHIVE_SINGLE_FILE_ARCHIVE_DEFS_HPP
