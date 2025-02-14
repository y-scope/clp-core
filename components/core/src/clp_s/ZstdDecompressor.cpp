// Code from CLP

#include "ZstdDecompressor.hpp"

#include <algorithm>
#include <filesystem>

#include <boost/iostreams/device/mapped_file.hpp>
#include <spdlog/spdlog.h>

namespace clp_s {
ZstdDecompressor::ZstdDecompressor()
        : Decompressor(CompressorType::ZSTD),
          m_input_type(InputType::NotInitialized),
          m_decompression_stream(nullptr),
          m_file_reader(nullptr),
          m_file_reader_initial_pos(0),
          m_file_read_buffer_length(0),
          m_file_read_buffer_capacity(0),
          m_decompressed_stream_pos(0),
          m_unused_decompressed_stream_block_size(0) {
    m_decompression_stream = ZSTD_createDStream();
    if (nullptr == m_decompression_stream) {
        SPDLOG_ERROR("ZstdDecompressor: ZSTD_createDStream() error");
        throw OperationFailed(ErrorCodeFailure, __FILENAME__, __LINE__);
    }

    // Create block to hold unused decompressed data
    m_unused_decompressed_stream_block_size = ZSTD_DStreamOutSize();
    m_unused_decompressed_stream_block_buffer
            = std::make_unique<char[]>(m_unused_decompressed_stream_block_size);
}

ZstdDecompressor::~ZstdDecompressor() {
    ZSTD_freeDStream(m_decompression_stream);
}

ErrorCode
ZstdDecompressor::try_read(char const* buf, size_t num_bytes_to_read, size_t& num_bytes_read) {
    if (InputType::NotInitialized == m_input_type) {
        throw OperationFailed(ErrorCodeNotInit, __FILENAME__, __LINE__);
    }
    if (nullptr == buf) {
        throw OperationFailed(ErrorCodeBadParam, __FILENAME__, __LINE__);
    }

    num_bytes_read = 0;

    ZSTD_outBuffer decompressed_stream_block = {(void*)buf, num_bytes_to_read, 0};
    while (decompressed_stream_block.pos < num_bytes_to_read) {
        // Check if there's data that can be decompressed
        if (m_compressed_stream_block.pos == m_compressed_stream_block.size) {
            switch (m_input_type) {
                case InputType::CompressedDataBuf:
                    // Fall through
                case InputType::MemoryMappedCompressedFile:
                    num_bytes_read = decompressed_stream_block.pos;
                    if (0 == decompressed_stream_block.pos) {
                        return ErrorCodeEndOfFile;
                    } else {
                        return ErrorCodeSuccess;
                    }
                case InputType::File: {
                    auto error_code = m_file_reader->try_read(
                            reinterpret_cast<char*>(m_file_read_buffer.get()),
                            m_file_read_buffer_capacity,
                            m_file_read_buffer_length
                    );
                    if (clp::ErrorCode::ErrorCode_Success != error_code) {
                        if (clp::ErrorCode::ErrorCode_EndOfFile == error_code) {
                            num_bytes_read = decompressed_stream_block.pos;
                            if (0 == decompressed_stream_block.pos) {
                                return ErrorCodeEndOfFile;
                            } else {
                                return ErrorCodeSuccess;
                            }
                        } else {
                            return static_cast<ErrorCode>(error_code);
                        }
                    }

                    m_compressed_stream_block.pos = 0;
                    m_compressed_stream_block.size = m_file_read_buffer_length;
                    break;
                }
                case InputType::ClpReader: {
                    auto error_code = m_reader->try_read(
                            reinterpret_cast<char*>(m_file_read_buffer.get()),
                            m_file_read_buffer_capacity,
                            m_file_read_buffer_length
                    );
                    if (clp::ErrorCode::ErrorCode_Success != error_code) {
                        if (clp::ErrorCode::ErrorCode_EndOfFile == error_code) {
                            num_bytes_read = decompressed_stream_block.pos;
                            if (0 == decompressed_stream_block.pos) {
                                return ErrorCodeEndOfFile;
                            } else {
                                return ErrorCodeSuccess;
                            }
                        } else {
                            // TODO: attempt to translate clp error codes
                            return ErrorCodeFailure;
                        }
                    }

                    m_compressed_stream_block.pos = 0;
                    m_compressed_stream_block.size = m_file_read_buffer_length;
                    break;
                }
                default:
                    throw OperationFailed(ErrorCodeUnsupported, __FILENAME__, __LINE__);
            }
        }

        // Decompress
        size_t error = ZSTD_decompressStream(
                m_decompression_stream,
                &decompressed_stream_block,
                &m_compressed_stream_block
        );
        if (ZSTD_isError(error)) {
            SPDLOG_ERROR(
                    "ZstdDecompressor: ZSTD_decompressStream() error: {}",
                    ZSTD_getErrorName(error)
            );
            return ErrorCodeFailure;
        }
    }

    // Update decompression stream position
    m_decompressed_stream_pos += decompressed_stream_block.pos;

    num_bytes_read = decompressed_stream_block.pos;
    return ErrorCodeSuccess;
}

ErrorCode ZstdDecompressor::try_read_string(size_t str_length, std::string& str) {
    str.resize(str_length);

    return try_read_exact_length(&str[0], str_length);
}

ErrorCode ZstdDecompressor::try_read_exact_length(char* buf, size_t num_bytes) {
    size_t num_bytes_read;
    auto error_code = try_read(buf, num_bytes, num_bytes_read);
    if (ErrorCodeSuccess != error_code) {
        return error_code;
    }
    if (num_bytes_read < num_bytes) {
        return ErrorCodeTruncated;
    }

    return ErrorCodeSuccess;
}

void ZstdDecompressor::open(char const* compressed_data_buf, size_t compressed_data_buf_size) {
    if (InputType::NotInitialized != m_input_type) {
        throw OperationFailed(ErrorCodeNotReady, __FILENAME__, __LINE__);
    }
    m_input_type = InputType::CompressedDataBuf;

    m_compressed_stream_block = {compressed_data_buf, compressed_data_buf_size, 0};

    reset_stream();
}

void ZstdDecompressor::open(clp::FileReader& file_reader, size_t file_read_buffer_capacity) {
    if (InputType::NotInitialized != m_input_type) {
        throw OperationFailed(ErrorCodeNotReady, __FILENAME__, __LINE__);
    }
    m_input_type = InputType::File;

    m_file_reader = &file_reader;
    m_file_reader_initial_pos = m_file_reader->get_pos();

    // Avoid reallocating the internal buffer if this instance is being re-used with an
    // unchanged buffer size.
    if (file_read_buffer_capacity != m_file_read_buffer_capacity) {
        m_file_read_buffer_capacity = file_read_buffer_capacity;
        m_file_read_buffer = std::make_unique<char[]>(m_file_read_buffer_capacity);
    }
    m_file_read_buffer_length = 0;

    m_compressed_stream_block = {m_file_read_buffer.get(), m_file_read_buffer_length, 0};

    reset_stream();
}

void ZstdDecompressor::open(clp::ReaderInterface& reader, size_t file_read_buffer_capacity) {
    if (InputType::NotInitialized != m_input_type) {
        throw OperationFailed(ErrorCodeNotReady, __FILENAME__, __LINE__);
    }
    m_input_type = InputType::ClpReader;

    m_reader = &reader;
    m_file_reader_initial_pos = m_reader->get_pos();

    // Avoid reallocating the internal buffer if this instance is being re-used with an
    // unchanged buffer size.
    if (file_read_buffer_capacity != m_file_read_buffer_capacity) {
        m_file_read_buffer_capacity = file_read_buffer_capacity;
        m_file_read_buffer = std::make_unique<char[]>(m_file_read_buffer_capacity);
    }
    m_file_read_buffer_length = 0;

    m_compressed_stream_block = {m_file_read_buffer.get(), m_file_read_buffer_length, 0};

    reset_stream();
}

void ZstdDecompressor::close() {
    switch (m_input_type) {
        case InputType::MemoryMappedCompressedFile:
            if (m_memory_mapped_compressed_file.is_open()) {
                // An existing file is memory mapped by the decompressor
                m_memory_mapped_compressed_file.close();
            }
            break;
        case InputType::File:
        case InputType::ClpReader:
            m_file_read_buffer.reset();
            m_file_read_buffer_capacity = 0;
            m_file_read_buffer_length = 0;
            m_file_reader = nullptr;
            m_reader = nullptr;
            break;
        case InputType::CompressedDataBuf:
        case InputType::NotInitialized:
            // Do nothing
            break;
        default:
            throw OperationFailed(ErrorCodeUnsupported, __FILENAME__, __LINE__);
    }
    m_input_type = InputType::NotInitialized;
}

void ZstdDecompressor::close_for_reuse() {
    if (false == (InputType::File == m_input_type || InputType::ClpReader == m_input_type)) {
        close();
        return;
    }
    m_file_read_buffer_length = 0;
    m_file_reader = nullptr;
    m_input_type = InputType::NotInitialized;
}

ErrorCode ZstdDecompressor::open(std::string const& compressed_file_path) {
    if (InputType::NotInitialized != m_input_type) {
        throw OperationFailed(ErrorCodeNotReady, __FILENAME__, __LINE__);
    }
    m_input_type = InputType::MemoryMappedCompressedFile;

    // Create memory mapping for compressed_file_path, use boost read only memory mapped file
    std::error_code error_code;
    size_t compressed_file_size = std::filesystem::file_size(compressed_file_path, error_code);
    if (error_code) {
        SPDLOG_ERROR(
                "ZstdDecompressor: Unable to obtain file size for '{}' - {}.",
                compressed_file_path.c_str(),
                error_code.message().c_str()
        );
        return ErrorCodeFailure;
    }

    boost::iostreams::mapped_file_params memory_map_params;
    memory_map_params.path = compressed_file_path;
    memory_map_params.flags = boost::iostreams::mapped_file::readonly;
    memory_map_params.length = compressed_file_size;
    memory_map_params.hint = m_memory_mapped_compressed_file.data(
    );  // Try to map it to the same memory location as previous memory mapped file
    m_memory_mapped_compressed_file.open(memory_map_params);
    if (false == m_memory_mapped_compressed_file.is_open()) {
        SPDLOG_ERROR(
                "ZstdDecompressor: Unable to memory map the compressed file with path: {}",
                compressed_file_path.c_str()
        );
        return ErrorCodeFailure;
    }

    // Configure input stream
    m_compressed_stream_block = {m_memory_mapped_compressed_file.data(), compressed_file_size, 0};

    reset_stream();

    return ErrorCodeSuccess;
}

void ZstdDecompressor::reset_stream() {
    if (InputType::File == m_input_type) {
        if (auto rc = m_file_reader->try_seek_from_begin(m_file_reader_initial_pos);
            clp::ErrorCode::ErrorCode_Success != rc && clp::ErrorCode::ErrorCode_EndOfFile != rc)
        {
            throw OperationFailed(static_cast<ErrorCode>(rc), __FILENAME__, __LINE__);
        }
        m_file_read_buffer_length = 0;
        m_compressed_stream_block.size = m_file_read_buffer_length;
    } else if (InputType::ClpReader == m_input_type) {
        auto rc = m_reader->try_seek_from_begin(m_file_reader_initial_pos);
        m_file_read_buffer_length = 0;
        m_compressed_stream_block.size = m_file_read_buffer_length;
        if (false
            == (clp::ErrorCode::ErrorCode_Success == rc || clp::ErrorCode::ErrorCode_EndOfFile == rc
            ))
        {
            throw OperationFailed(static_cast<ErrorCode>(rc), __FILENAME__, __LINE__);
        }
    }

    ZSTD_initDStream(m_decompression_stream);
    m_decompressed_stream_pos = 0;

    m_compressed_stream_block.pos = 0;
}
}  // namespace clp_s
