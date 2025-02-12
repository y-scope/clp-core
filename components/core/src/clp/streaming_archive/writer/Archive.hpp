#ifndef CLP_STREAMING_ARCHIVE_WRITER_ARCHIVE_HPP
#define CLP_STREAMING_ARCHIVE_WRITER_ARCHIVE_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <log_surgeon/LogEvent.hpp>
#include <log_surgeon/ReaderParser.hpp>

#include "../../ArrayBackedPosIntSet.hpp"
#include "../../ErrorCode.hpp"
#include "../../GlobalMetadataDB.hpp"
#include "../../ir/LogEvent.hpp"
#include "../../LogTypeDictionaryWriter.hpp"
#include "../../VariableDictionaryWriter.hpp"
#include "../ArchiveMetadata.hpp"
#include "../MetadataDB.hpp"

namespace clp::streaming_archive::writer {
class Archive {
public:
    // Types
    /**
     * Structure used to pass settings when opening a new archive
     * @param id
     * @param creator_id
     * @param creation_num
     * @param target_segment_uncompressed_size
     * @param compression_level Compression level of the compressor being opened
     * @param output_dir Output directory
     * @param global_metadata_db
     * @param print_archive_stats_progress Enable printing statistics about the archive as it's
     * compressed
     */
    struct UserConfig {
        boost::uuids::uuid id;
        boost::uuids::uuid creator_id;
        size_t creation_num;
        size_t target_segment_uncompressed_size;
        int compression_level;
        std::string output_dir;
        GlobalMetadataDB* global_metadata_db;
        bool print_archive_stats_progress;
        bool use_single_file_archive;
    };

    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        char const* what() const noexcept override {
            return "streaming_archive::writer::Archive operation failed";
        }
    };

    TimestampPattern* m_old_ts_pattern;
    size_t m_target_data_size_of_dicts;
    UserConfig m_archive_user_config;
    std::string m_path_for_compression;
    group_id_t m_group_id;
    size_t m_target_encoded_file_size;
    std::string m_schema_file_path;

    // Constructors
    Archive()
            : m_segments_dir_fd(-1),
              m_compression_level(0),
              m_global_metadata_db(nullptr),
              m_old_ts_pattern(nullptr),
              m_schema_file_path() {}

    // Destructor
    ~Archive();

    // Methods
    /**
     * Creates the directory structure for the archive and opens writers for the dictionaries
     * @param user_config Settings configurable by the user
     * @throw FileWriter::OperationFailed if any dictionary writer could not be opened
     * @throw streaming_archive::writer::Archive::OperationFailed if archive already exists, if
     * it could not be stat-ed, if the directory structure could not be created, if the file is
     * not reset or problems with metadata.
     */
    void open(UserConfig const& user_config);
    /**
     * Writes a final snapshot of the archive, closes all open files, and closes the
     * dictionaries
     * @throw FileWriter::OperationFailed if any writer could not be closed
     * @throw streaming_archive::writer::Archive::OperationFailed if any empty directories could
     * not be removed
     * @throw streaming_archive::writer::Archive::OperationFailed if the file is not reset
     * @throw Same as streaming_archive::writer::SegmentManager::close
     * @throw Same as streaming_archive::writer::Archive::write_dir_snapshot
     */
    void close();

    /**
     * Creates and opens a file with the given path
     * @param path
     * @param group_id
     * @param orig_file_id
     * @param split_ix
     * @param begin_message_ix
     * @return Pointer to the new file
     */
    void create_and_open_file(
            std::string const& path,
            group_id_t group_id,
            boost::uuids::uuid const& orig_file_id,
            size_t split_ix = 0,
            size_t begin_message_ix = 0
    );

    void close_file();

    File const& get_file() const;

    /**
     * Sets the split status of the current encoded file
     * @param is_split
     */
    void set_file_is_split(bool is_split);

    /**
     * Wrapper for streaming_archive::writer::File::change_ts_pattern
     * @param pattern
     */
    void change_ts_pattern(TimestampPattern const* pattern);
    /**
     * Encodes and writes a message to the current encoded file
     * @param timestamp
     * @param message
     * @param num_uncompressed_bytes
     * @throw FileWriter::OperationFailed if any write fails
     */
    void
    write_msg(epochtime_t timestamp, std::string const& message, size_t num_uncompressed_bytes);

    /**
     * Encodes and writes a message to the given file using schema file
     * @param log_event_view
     * @throw FileWriter::OperationFailed if any write fails
     */
    void write_msg_using_schema(log_surgeon::LogEventView const& log_event_view);

    /**
     * Writes an IR log event to the current encoded file
     * @tparam encoded_variable_t The type of the encoded variables in the log event
     * @param log_event
     */
    template <typename encoded_variable_t>
    void write_log_event_ir(ir::LogEvent<encoded_variable_t> const& log_event);

    /**
     * Writes snapshot of archive to disk including metadata of all files and new dictionary
     * entries
     * @throw FileWriter::OperationFailed if failed to write or flush dictionaries
     * @throw std::out_of_range if dictionary ID unexpectedly didn't exist
     * @throw Same as streaming_archive::writer::Archive::persist_file_metadata
     */
    void write_dir_snapshot();

    /**
     * Adds the encoded file to the segment
     * @throw streaming_archive::writer::Archive::OperationFailed if failed the file is not
     * tracked by the current archive
     * @throw Same as streaming_archive::writer::Archive::persist_file_metadata
     */
    void append_file_to_segment();

    /**
     * Adds empty directories to the archive
     * @param empty_directory_paths
     * @throw streaming_archive::writer::Archive::OperationFailed if failed to insert paths to
     * the database
     */
    void add_empty_directories(std::vector<std::string> const& empty_directory_paths);

    boost::uuids::uuid const& get_id() const { return m_id; }

    std::string const& get_id_as_string() const { return m_id_as_string; }

    size_t get_data_size_of_dictionaries() const {
        return m_logtype_dict.get_data_size() + m_var_dict.get_data_size();
    }

    [[nodiscard]] auto get_use_single_file_archive() const -> bool {
        return m_use_single_file_archive;
    }

private:
    // Types
    /**
     * Custom less-than comparator for sets to:
     * - Primary sort order File pointers in increasing order of their group ID, then
     * - Secondary sort order File pointers in increasing order of their end timestamp, then
     * - Tertiary sort order File pointers in alphabetical order of their paths, then
     * - Determine uniqueness by their ID
     */
    class FileGroupIdAndEndTimestampLTSetComparator {
    public:
        // Methods
        bool operator()(File const* lhs, File const* rhs) const {
            // Primary sort by file's group ID
            if (lhs->get_group_id() != rhs->get_group_id()) {
                return lhs->get_group_id() < rhs->get_group_id();
            } else {
                // Secondary sort by file's end timestamp, from earliest to latest
                if (lhs->get_end_ts() != rhs->get_end_ts()) {
                    return lhs->get_end_ts() < rhs->get_end_ts();
                } else {
                    // Tertiary sort by file path, alphabetically
                    if (lhs->get_orig_path() != rhs->get_orig_path()) {
                        return lhs->get_orig_path() < rhs->get_orig_path();
                    } else {
                        return lhs->get_id() < rhs->get_id();
                    }
                }
            }
        }
    };

    // Methods
    void update_segment_indices(
            logtype_dictionary_id_t logtype_id,
            std::vector<variable_dictionary_id_t> const& var_ids
    );

    /**
     * Appends the content of the current encoded file to the given segment
     * @param segment
     * @param logtype_ids_in_segment
     * @param var_ids_in_segment
     * @param files_in_segment
     */
    void append_file_contents_to_segment(
            Segment& segment,
            ArrayBackedPosIntSet<logtype_dictionary_id_t>& logtype_ids_in_segment,
            ArrayBackedPosIntSet<variable_dictionary_id_t>& var_ids_in_segment,
            std::vector<File*>& files_in_segment
    );
    /**
     * Writes the given files' metadata to the database using bulk writes
     * @param files
     * @throw streaming_archive::writer::Archive::OperationFailed if failed to replace old
     * metadata for any file
     * @throw mongocxx::logic_error if invalid database operation is created
     */
    void persist_file_metadata(std::vector<File*> const& files);
    /**
     * Closes a given segment, persists the metadata of the files in the segment, and cleans up
     * any data remaining outside the segment
     * @param segment
     * @param files
     * @param segment_logtype_ids
     * @param segment_var_ids
     * @throw Same as streaming_archive::writer::Segment::close
     * @throw Same as streaming_archive::writer::Archive::persist_file_metadata
     */
    void close_segment_and_persist_file_metadata(
            Segment& segment,
            std::vector<File*>& files,
            ArrayBackedPosIntSet<logtype_dictionary_id_t>& segment_logtype_ids,
            ArrayBackedPosIntSet<variable_dictionary_id_t>& segment_var_ids
    );

    /**
     * @return The size (in bytes) of compressed data whose size may change before the archive
     * is closed
     */
    uint64_t get_dynamic_compressed_size();
    /**
     * Updates the archive's metadata
     */
    void update_metadata();

    // Variables
    boost::uuids::uuid m_id;
    std::string m_id_as_string;

    // Used to order the archives created by a single thread
    // NOTE: This is necessary because files may be split across archives and we want to
    // decompress their parts in order.
    boost::uuids::uuid m_creator_id;
    std::string m_creator_id_as_string;
    size_t m_creation_num;

    std::string m_path;
    std::string m_segments_dir_path;
    int m_segments_dir_fd;

    // Holds the file being compressed
    File* m_file;

    LogTypeDictionaryWriter m_logtype_dict;
    // Holds preallocated logtype dictionary entry for performance
    LogTypeDictionaryEntry m_logtype_dict_entry;
    std::vector<encoded_variable_t> m_encoded_vars;
    std::vector<variable_dictionary_id_t> m_var_ids;
    VariableDictionaryWriter m_var_dict;

    boost::uuids::random_generator m_uuid_generator;

    // Since we batch metadata persistence operations, we need to keep track of files whose
    // metadata should be persisted Accordingly:
    // - m_files_with_timestamps_in_segment contains files that 1) have been moved to an open
    //   segment and 2) contain timestamps
    // - m_files_without_timestamps_in_segment contains files that 1) have been moved to an open
    //   segment and 2) do not contain timestamps
    segment_id_t m_next_segment_id;
    std::vector<File*> m_files_with_timestamps_in_segment;
    std::vector<File*> m_files_without_timestamps_in_segment;

    size_t m_target_segment_uncompressed_size;
    Segment m_segment_for_files_with_timestamps;
    ArrayBackedPosIntSet<logtype_dictionary_id_t>
            m_logtype_ids_in_segment_for_files_with_timestamps;
    ArrayBackedPosIntSet<variable_dictionary_id_t> m_var_ids_in_segment_for_files_with_timestamps;
    // Logtype and variable IDs for a file that hasn't yet been assigned to the timestamp or
    // timestamp-less segment
    std::unordered_set<logtype_dictionary_id_t> m_logtype_ids_for_file_with_unassigned_segment;
    std::unordered_set<variable_dictionary_id_t> m_var_ids_for_file_with_unassigned_segment;
    Segment m_segment_for_files_without_timestamps;
    ArrayBackedPosIntSet<logtype_dictionary_id_t>
            m_logtype_ids_in_segment_for_files_without_timestamps;
    ArrayBackedPosIntSet<variable_dictionary_id_t>
            m_var_ids_in_segment_for_files_without_timestamps;

    int m_compression_level;

    MetadataDB m_metadata_db;

    std::optional<ArchiveMetadata> m_local_metadata;
    FileWriter m_metadata_file_writer;

    GlobalMetadataDB* m_global_metadata_db;

    bool m_print_archive_stats_progress;
    bool m_use_single_file_archive{false};
};
}  // namespace clp::streaming_archive::writer

#endif  // CLP_STREAMING_ARCHIVE_WRITER_ARCHIVE_HPP
