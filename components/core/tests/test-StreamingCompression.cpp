#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <ystdlib/container/Array.hpp>

#include <boost/filesystem/operations.hpp>
#include <Catch2/single_include/catch2/catch.hpp>
#include <zstd.h>

#include "../src/clp/ErrorCode.hpp"
#include "../src/clp/FileWriter.hpp"
#include "../src/clp/ReadOnlyMemoryMappedFile.hpp"
#include "../src/clp/streaming_compression/Compressor.hpp"
#include "../src/clp/streaming_compression/Decompressor.hpp"
#include "../src/clp/streaming_compression/lzma/Compressor.hpp"
#include "../src/clp/streaming_compression/lzma/Decompressor.hpp"
#include "../src/clp/streaming_compression/passthrough/Compressor.hpp"
#include "../src/clp/streaming_compression/passthrough/Decompressor.hpp"
#include "../src/clp/streaming_compression/zstd/Compressor.hpp"
#include "../src/clp/streaming_compression/zstd/Decompressor.hpp"

using clp::ErrorCode_Success;
using clp::FileWriter;
using clp::streaming_compression::Compressor;
using clp::streaming_compression::Decompressor;
using std::string;
using std::string_view;
using ystdlib::container::Array;

namespace {
constexpr string_view cCompressedFilePath{"test_streaming_compressed_file.bin"};
constexpr size_t cBufferSize{128L * 1024 * 1024};  // 128MB
constexpr auto cCompressionChunkSizes = std::to_array<size_t>(
        {0,
         cBufferSize / 100,
         cBufferSize / 50,
         cBufferSize / 25,
         cBufferSize / 10,
         cBufferSize / 5,
         cBufferSize / 2,
         cBufferSize}
);

auto compress(std::unique_ptr<Compressor> compressor, char const* src) -> void;

auto decompress_and_compare(
        std::unique_ptr<Decompressor> decompressor,
        Array<char> const& uncompressed_buffer,
        Array<char>& decompressed_buffer
) -> void;

auto compress(std::unique_ptr<Compressor> compressor, char const* src) -> void {
    FileWriter file_writer;
    file_writer.open(string(cCompressedFilePath), FileWriter::OpenMode::CREATE_FOR_WRITING);
    compressor->open(file_writer);
    for (auto const chunk_size : cCompressionChunkSizes) {
        compressor->write(src, chunk_size);
    }
    compressor->close();
    file_writer.close();
}

auto decompress_and_compare(
        std::unique_ptr<Decompressor> decompressor,
        Array<char> const& uncompressed_buffer,
        Array<char>& decompressed_buffer
) -> void {
    clp::ReadOnlyMemoryMappedFile const memory_mapped_compressed_file{string(cCompressedFilePath)};
    auto const compressed_file_view{memory_mapped_compressed_file.get_view()};
    decompressor->open(compressed_file_view.data(), compressed_file_view.size());

    size_t num_uncompressed_bytes{0};
    for (auto const chunk_size : cCompressionChunkSizes) {
        // Clear the buffer to ensure that we are not comparing values from a previous test
        std::ranges::fill(decompressed_buffer.begin(), decompressed_buffer.end(), 0);
        REQUIRE(
                (ErrorCode_Success
                 == decompressor->get_decompressed_stream_region(
                         num_uncompressed_bytes,
                         decompressed_buffer.data(),
                         chunk_size
                 ))
        );
        REQUIRE(std::equal(
                uncompressed_buffer.begin(),
                uncompressed_buffer.begin() + chunk_size,
                decompressed_buffer.begin()
        ));
        num_uncompressed_bytes += chunk_size;
    }

    REQUIRE(
            (std::accumulate(
                     cCompressionChunkSizes.cbegin(),
                     cCompressionChunkSizes.cend(),
                     size_t{0}
             )
             == num_uncompressed_bytes)
    );
}
}  // namespace

TEST_CASE("StreamingCompression", "[StreamingCompression]") {
    constexpr size_t cAlphabetLength{26};

    std::unique_ptr<Compressor> compressor;
    std::unique_ptr<Decompressor> decompressor;

    Array<char> decompressed_buffer{cBufferSize};
    Array<char> uncompressed_buffer{cBufferSize};
    for (size_t i{0}; i < cBufferSize; ++i) {
        uncompressed_buffer.at(i) = static_cast<char>(('a' + (i % cAlphabetLength)));
    }

    SECTION("ZStd single phase compression") {
        compressor = std::make_unique<clp::streaming_compression::zstd::Compressor>();
        compress(std::move(compressor), uncompressed_buffer.data());
        decompressor = std::make_unique<clp::streaming_compression::zstd::Decompressor>();
        decompress_and_compare(std::move(decompressor), uncompressed_buffer, decompressed_buffer);
    }

    SECTION("Passthrough compression") {
        compressor = std::make_unique<clp::streaming_compression::passthrough::Compressor>();
        compress(std::move(compressor), uncompressed_buffer.data());
        decompressor = std::make_unique<clp::streaming_compression::passthrough::Decompressor>();
        decompress_and_compare(std::move(decompressor), uncompressed_buffer, decompressed_buffer);
    }

    SECTION("LZMA compression") {
        compressor = std::make_unique<clp::streaming_compression::lzma::Compressor>();
        compress(std::move(compressor), uncompressed_buffer.data());
        decompressor = std::make_unique<clp::streaming_compression::lzma::Decompressor>();
    }

    boost::filesystem::remove(string(cCompressedFilePath));
}
