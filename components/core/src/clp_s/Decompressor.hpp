// Code from CLP

#ifndef CLP_S_DECOMPRESSOR_HPP
#define CLP_S_DECOMPRESSOR_HPP

#include <string>

#include "../clp/FileReader.hpp"
#include "../clp/ReaderInterface.hpp"
#include "TraceableException.hpp"

namespace clp_s {
class Decompressor {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}
    };

    enum class CompressorType : uint8_t {
        ZSTD = 0x10,
        Passthrough = 0xFF,
    };

    // Constructor
    explicit Decompressor(CompressorType type) : m_type(type) {}

    // Destructor
    ~Decompressor() = default;

    // Explicitly disable copy and move constructor/assignment
    Decompressor(Decompressor const&) = delete;

    Decompressor& operator=(Decompressor const&) = delete;

    // Methods
    /**
     * Initializes streaming decompressor to decompress from the specified compressed data buffer
     * @param compressed_data_buffer
     * @param compressed_data_buffer_size
     */
    virtual void open(char const* compressed_data_buffer, size_t compressed_data_buffer_size) = 0;

    /**
     * Initializes the decompressor to decompress from an open file
     * @param file_reader
     * @param file_read_buffer_capacity The maximum amount of data to read from a file at a time
     */
    virtual void open(clp::FileReader& file_reader, size_t file_read_buffer_capacity) = 0;

    /**
     * Initializes the decompressor to decompress from an open clp reader
     * @param reader
     * @param read_buffer_capacity The maximum amount of data to read at a time
     */
    virtual void open(clp::ReaderInterface& reader, size_t read_buffer_capacity) = 0;

    /**
     * Closes decompression stream
     */
    virtual void close() = 0;

protected:
    // Variables
    CompressorType m_type;
};
}  // namespace clp_s

#endif  // CLP_S_DECOMPRESSOR_HPP
