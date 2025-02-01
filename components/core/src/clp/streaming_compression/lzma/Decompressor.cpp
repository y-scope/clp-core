#include "Decompressor.hpp"

#include <cstddef>

#include "../../ErrorCode.hpp"
#include "../../ReaderInterface.hpp"
#include "../../TraceableException.hpp"

namespace clp::streaming_compression::lzma {
auto Decompressor::try_read(
        [[maybe_unused]] char* buf,
        [[maybe_unused]] size_t num_bytes_to_read,
        [[maybe_unused]] size_t& num_bytes_read
) -> ErrorCode {
    return ErrorCode_Unsupported;
}

auto Decompressor::try_seek_from_begin([[maybe_unused]] size_t pos) -> ErrorCode {
    return ErrorCode_Unsupported;
}

auto Decompressor::try_get_pos([[maybe_unused]] size_t& pos) -> ErrorCode {
    return ErrorCode_Unsupported;
}

auto Decompressor::open(
        [[maybe_unused]] char const* compressed_data_buffer,
        [[maybe_unused]] size_t compressed_data_buffer_size
) -> void {
    throw OperationFailed(ErrorCode_Unsupported, __FILENAME__, __LINE__);
}

auto Decompressor::open(
        [[maybe_unused]] ReaderInterface& reader,
        [[maybe_unused]] size_t read_buffer_capacity
) -> void {
    throw OperationFailed(ErrorCode_Unsupported, __FILENAME__, __LINE__);
}

auto Decompressor::close() -> void {
    throw OperationFailed(ErrorCode_Unsupported, __FILENAME__, __LINE__);
}

auto Decompressor::get_decompressed_stream_region(
        [[maybe_unused]] size_t decompressed_stream_pos,
        [[maybe_unused]] char* extraction_buf,
        [[maybe_unused]] size_t extraction_len
) -> ErrorCode {
    return ErrorCode_Unsupported;
}
}  // namespace clp::streaming_compression::lzma
