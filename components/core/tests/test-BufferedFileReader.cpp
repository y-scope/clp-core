#include <array>

#include <boost/filesystem.hpp>
#include <Catch2/single_include/catch2/catch.hpp>

#include "../src/clp/BufferedFileReader.hpp"
#include "../src/clp/FileDescriptorReader.hpp"
#include "../src/clp/FileReader.hpp"
#include "../src/clp/FileWriter.hpp"

using clp::BufferedFileReader;
using clp::ErrorCode;
using clp::ErrorCode_EndOfFile;
using clp::ErrorCode_Success;
using clp::ErrorCode_Unsupported;
using clp::FileDescriptorReader;
using clp::FileReader;
using clp::FileWriter;
using std::make_unique;
using std::string;

static constexpr size_t cNumAlphabets = 'z' - 'a';

TEST_CASE("Test reading data", "[BufferedFileReader]") {
    // Initialize data for testing
    size_t const test_data_size = 4L * 1024 * 1024 + 1;  // 4MB + 1
    auto test_data_uniq_ptr = make_unique<std::array<char, test_data_size>>();
    auto& test_data = *test_data_uniq_ptr;
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<char>('a' + (i % (cNumAlphabets)));
    }

    string const test_file_path{"BufferedFileReader.test"};
    // Write to test file
    FileWriter file_writer;
    file_writer.open(test_file_path, FileWriter::OpenMode::CREATE_FOR_WRITING);
    file_writer.write(test_data.cbegin(), test_data_size);
    file_writer.close();

    auto read_buf_uniq_ptr = make_unique<std::array<char, test_data_size>>();
    auto& read_buf = *read_buf_uniq_ptr;
    size_t const base_buffer_size = BufferedFileReader::cMinBufferSize << 4;
    BufferedFileReader reader{make_unique<FileDescriptorReader>(test_file_path), base_buffer_size};

    size_t num_bytes_read{0};
    size_t buf_pos{0};
    size_t num_bytes_to_read{0};

    SECTION("General read testing") {
        // Read a small chunk of data;
        num_bytes_to_read = base_buffer_size >> 6;
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.begin() + buf_pos, num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data(), buf_pos));

        // Read a large chunk of data, so BufferedFileReader will refill the
        // internal buffer
        num_bytes_to_read = base_buffer_size + 2;
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.begin() + buf_pos, num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data(), buf_pos));

        // Read remaining data
        num_bytes_to_read = test_data_size - buf_pos;
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.begin() + buf_pos, num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data(), buf_pos));

        // Ensure the file reaches EOF
        num_bytes_to_read = 1;
        REQUIRE(ErrorCode_EndOfFile
                == reader.try_read(read_buf.begin() + buf_pos, num_bytes_to_read, num_bytes_read));
    }

    SECTION("Simple Seek without a checkpoint") {
        num_bytes_to_read = base_buffer_size + 4;

        // Seek to some random position
        size_t seek_pos{245};
        REQUIRE(ErrorCode_Success == reader.try_seek_from_begin(seek_pos));
        buf_pos = seek_pos;
        REQUIRE(reader.get_pos() == buf_pos);

        // Do a read
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + seek_pos, num_bytes_to_read));

        // Seek forwards to another random position
        seek_pos = 345'212;
        REQUIRE(ErrorCode_Success == reader.try_seek_from_begin(seek_pos));
        buf_pos = seek_pos;
        REQUIRE(reader.get_pos() == buf_pos);

        // Do a read
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + seek_pos, num_bytes_to_read));

        // Ensure we can't seek backwards when there's no checkpoint
        REQUIRE(ErrorCode_Unsupported == reader.try_seek_from_begin(seek_pos));
    }

    SECTION("Seek with a checkpoint") {
        // Read some data to advance the read head
        num_bytes_to_read = base_buffer_size + 4;
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + buf_pos, num_bytes_to_read));
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);

        auto checkpoint_pos = reader.set_checkpoint();

        // Read some more data
        num_bytes_to_read = 345'212;
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + buf_pos, num_bytes_to_read));
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);

        size_t highest_file_pos = reader.get_pos();

        // Seek backwards to somewhere between the checkpoint and the read head
        size_t const seek_pos_1 = checkpoint_pos + 500;
        REQUIRE(ErrorCode_Success == reader.try_seek_from_begin(seek_pos_1));
        buf_pos = seek_pos_1;
        REQUIRE(reader.get_pos() == buf_pos);

        // Read some data
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + buf_pos, num_bytes_to_read));
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);

        highest_file_pos = std::max(highest_file_pos, reader.get_pos());

        // Ensure we can't seek to a position that's before the checkpoint
        REQUIRE(ErrorCode_Unsupported == reader.try_seek_from_begin(checkpoint_pos - 1));
        REQUIRE(reader.get_pos() == buf_pos);

        // Seek back to the highest file pos
        REQUIRE(ErrorCode_Success == reader.try_seek_from_begin(highest_file_pos));
        buf_pos = highest_file_pos;
        REQUIRE(reader.get_pos() == buf_pos);

        // Do a read
        num_bytes_to_read = 4096;
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + buf_pos, num_bytes_to_read));
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);

        highest_file_pos = reader.get_pos();

        // Seek to somewhere between the checkpoint and latest data
        size_t const seek_pos_2 = (highest_file_pos + checkpoint_pos) / 2;
        reader.seek_from_begin(seek_pos_2);
        buf_pos = seek_pos_2;
        REQUIRE(reader.get_pos() == buf_pos);

        // Set a new checkpoint
        reader.set_checkpoint();

        // Ensure we can't seek to seek_pos_1
        REQUIRE(ErrorCode_Unsupported == reader.try_seek_from_begin(seek_pos_1));

        // Do a read
        num_bytes_to_read = 4096;
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + buf_pos, num_bytes_to_read));
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);

        reader.clear_checkpoint();
        buf_pos = highest_file_pos;
        REQUIRE(reader.get_pos() == buf_pos);

        // Do a read
        num_bytes_to_read = base_buffer_size;
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + buf_pos, num_bytes_to_read));
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);
    }

    SECTION("Seek with delayed read") {
        // Advance to some random position
        size_t seek_pos = 45'313;
        REQUIRE(ErrorCode_Success == reader.try_seek_from_begin(seek_pos));
        buf_pos = seek_pos;
        REQUIRE(reader.get_pos() == buf_pos);

        auto checkpoint_pos = reader.set_checkpoint();

        // Do a read
        num_bytes_to_read = 345'212;
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + buf_pos, num_bytes_to_read));
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);

        // Seek to somewhere between the checkpoint and the read head
        seek_pos = reader.get_pos() / 2;
        REQUIRE(ErrorCode_Success == reader.try_seek_from_begin(seek_pos));
        buf_pos = seek_pos;
        REQUIRE(reader.get_pos() == buf_pos);

        // Do a read
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + buf_pos, num_bytes_to_read));
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);

        // Seek close to the end of the file
        seek_pos = test_data_size - 500;
        REQUIRE(ErrorCode_Success == reader.try_seek_from_begin(seek_pos));
        buf_pos = seek_pos;
        REQUIRE(reader.get_pos() == buf_pos);

        // Do a read
        num_bytes_to_read = test_data_size - seek_pos;
        REQUIRE(ErrorCode_Success
                == reader.try_read(read_buf.data(), num_bytes_to_read, num_bytes_read));
        REQUIRE(num_bytes_to_read == num_bytes_read);
        REQUIRE(0 == memcmp(read_buf.data(), test_data.data() + buf_pos, num_bytes_to_read));
        buf_pos += num_bytes_read;
        REQUIRE(reader.get_pos() == buf_pos);
    }

    boost::filesystem::remove(test_file_path);
}

TEST_CASE("Test delimiter", "[BufferedFileReader]") {
    // Initialize random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> uniformly_distributed_alphabet('a', 'a' + cNumAlphabets - 1);

    // Initialize data for testing
    size_t const test_data_size = 1L * 1024 * 1024 + 1;  // 1MB
    auto test_data_uniq_ptr = make_unique<std::array<char, test_data_size>>();
    auto& test_data = *test_data_uniq_ptr;

    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i] = static_cast<char>(uniformly_distributed_alphabet(gen));
    }

    // Write to test file
    string const test_file_path{"BufferedFileReader.delimiter.test"};
    FileWriter file_writer;
    file_writer.open(test_file_path, FileWriter::OpenMode::CREATE_FOR_WRITING);
    file_writer.write(test_data.data(), test_data_size);
    file_writer.close();

    size_t const reader_begin_offset = GENERATE(0, 127);

    // Instantiate BufferedFileReader and the reference FileReader from a non-zero pos
    auto fd_reader = make_unique<FileDescriptorReader>(test_file_path);
    fd_reader->seek_from_begin(reader_begin_offset);
    BufferedFileReader buffered_file_reader{std::move(fd_reader)};
    string test_string;

    FileReader ref_file_reader{test_file_path};
    ref_file_reader.seek_from_begin(reader_begin_offset);
    string ref_string;

    // Validate a clearing a checkpoint without any reading wouldn't change the beginning offset
    buffered_file_reader.clear_checkpoint();
    REQUIRE(reader_begin_offset == buffered_file_reader.get_pos());

    // Validate that a FileReader and a BufferedFileReader return the same strings (split by
    // delimiters)
    ErrorCode error_code{ErrorCode_Success};
    auto delimiter = static_cast<char>(uniformly_distributed_alphabet(gen));
    while (ErrorCode_EndOfFile != error_code) {
        error_code = ref_file_reader.try_read_to_delimiter(delimiter, true, false, ref_string);
        auto const error_code2
                = buffered_file_reader.try_read_to_delimiter(delimiter, true, false, test_string);
        REQUIRE(error_code2 == error_code);
        REQUIRE(test_string == ref_string);
    }

    boost::filesystem::remove(test_file_path);
}
