#ifndef CLP_DICTIONARYREADER_HPP
#define CLP_DICTIONARYREADER_HPP

#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <string_utils/string_utils.hpp>

#include "dictionary_utils.hpp"
#include "DictionaryEntry.hpp"
#include "FileReader.hpp"
#include "streaming_compression/passthrough/Decompressor.hpp"
#include "streaming_compression/zstd/Decompressor.hpp"
#include "Utils.hpp"

namespace clp {
/**
 * Template class for reading dictionaries from disk and performing operations on them
 * @tparam DictionaryIdType
 * @tparam EntryType
 */
template <typename DictionaryIdType, typename EntryType>
class DictionaryReader {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        char const* what() const noexcept override { return "DictionaryReader operation failed"; }
    };

    // Constructors
    DictionaryReader() : m_is_open(false), m_num_segments_read_from_index(0) {
        static_assert(
                std::is_base_of<DictionaryEntry<DictionaryIdType>, EntryType>::value,
                "EntryType must be DictionaryEntry or a derivative."
        );
    }

    // Methods
    /**
     * Opens dictionary for reading
     * @param dictionary_path
     * @param segment_index_path
     */
    void open(std::string const& dictionary_path, std::string const& segment_index_path);
    /**
     * Closes the dictionary
     */
    void close();

    /**
     * Reads any new entries from disk
     */
    void read_new_entries();

    /**
     * Gets the dictionary's entries
     * @return All dictionary entries
     */
    std::vector<EntryType> const& get_entries() const { return m_entries; }

    /**
     * Gets the entry with the given ID
     * @param id
     * @return The entry with the given ID
     */
    EntryType const& get_entry(DictionaryIdType id) const;

    /**
     * Gets the value of the entry with the specified ID
     * @param id
     * @return Value of the entry with the specified ID
     */
    std::string const& get_value(DictionaryIdType id) const;
    /**
     * Gets the entry exactly matching the given search string
     * @param search_string
     * @param ignore_case
     * @return a (possibly empty) vector of entries
     */
    std::vector<EntryType const*>
    get_entry_matching_value(std::string const& search_string, bool ignore_case) const;
    /**
     * Gets the entries that match a given wildcard string
     * @param wildcard_string
     * @param ignore_case
     * @param entries Set in which to store found entries
     */
    void get_entries_matching_wildcard_string(
            std::string const& wildcard_string,
            bool ignore_case,
            std::unordered_set<EntryType const*>& entries
    ) const;

protected:
    // Methods
    /**
     * Reads a segment's worth of IDs from the segment index
     */
    void read_segment_ids();

    // Variables
    bool m_is_open;
    std::unique_ptr<FileReader> m_dictionary_file_reader;
    std::unique_ptr<FileReader> m_segment_index_file_reader;
#if USE_PASSTHROUGH_COMPRESSION
    streaming_compression::passthrough::Decompressor m_dictionary_decompressor;
    streaming_compression::passthrough::Decompressor m_segment_index_decompressor;
#elif USE_ZSTD_COMPRESSION
    streaming_compression::zstd::Decompressor m_dictionary_decompressor;
    streaming_compression::zstd::Decompressor m_segment_index_decompressor;
#else
    static_assert(false, "Unsupported compression mode.");
#endif
    size_t m_num_segments_read_from_index;
    std::vector<EntryType> m_entries;
};

template <typename DictionaryIdType, typename EntryType>
void DictionaryReader<DictionaryIdType, EntryType>::open(
        std::string const& dictionary_path,
        std::string const& segment_index_path
) {
    if (m_is_open) {
        throw OperationFailed(ErrorCode_NotReady, __FILENAME__, __LINE__);
    }

    constexpr size_t cDecompressorFileReadBufferCapacity = 64 * 1024;  // 64 KB

    m_dictionary_file_reader = make_unique<FileReader>(dictionary_path);

    // Skip header and then open the decompressor
    m_dictionary_file_reader->seek_from_begin(sizeof(uint64_t));
    m_dictionary_decompressor.open(*m_dictionary_file_reader, cDecompressorFileReadBufferCapacity);

    m_segment_index_file_reader = make_unique<FileReader>(segment_index_path);

    // Skip header and then open the decompressor
    m_segment_index_file_reader->seek_from_begin(sizeof(uint64_t));
    m_segment_index_decompressor.open(
            *m_segment_index_file_reader,
            cDecompressorFileReadBufferCapacity
    );

    m_is_open = true;
}

template <typename DictionaryIdType, typename EntryType>
void DictionaryReader<DictionaryIdType, EntryType>::close() {
    if (false == m_is_open) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }

    m_segment_index_decompressor.close();
    m_segment_index_file_reader.reset();
    m_dictionary_decompressor.close();
    m_dictionary_file_reader.reset();

    m_num_segments_read_from_index = 0;
    m_entries.clear();

    m_is_open = false;
}

template <typename DictionaryIdType, typename EntryType>
void DictionaryReader<DictionaryIdType, EntryType>::read_new_entries() {
    if (false == m_is_open) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }

    // Read dictionary header
    auto num_dictionary_entries = read_dictionary_header(*m_dictionary_file_reader);

    // Validate dictionary header
    if (num_dictionary_entries < m_entries.size()) {
        throw OperationFailed(ErrorCode_Corrupt, __FILENAME__, __LINE__);
    }

    // Read new dictionary entries
    if (num_dictionary_entries > m_entries.size()) {
        auto prev_num_dictionary_entries = m_entries.size();
        m_entries.resize(num_dictionary_entries);

        for (size_t i = prev_num_dictionary_entries; i < num_dictionary_entries; ++i) {
            auto& entry = m_entries[i];

            entry.read_from_file(m_dictionary_decompressor);
        }
    }

    // Read segment index header
    auto num_segments = read_segment_index_header(*m_segment_index_file_reader);

    // Validate segment index header
    if (num_segments < m_num_segments_read_from_index) {
        throw OperationFailed(ErrorCode_Corrupt, __FILENAME__, __LINE__);
    }

    // Read new segments from index
    if (num_segments > m_num_segments_read_from_index) {
        for (size_t i = m_num_segments_read_from_index; i < num_segments; ++i) {
            read_segment_ids();
        }
    }
}

template <typename DictionaryIdType, typename EntryType>
EntryType const& DictionaryReader<DictionaryIdType, EntryType>::get_entry(DictionaryIdType id
) const {
    if (false == m_is_open) {
        throw OperationFailed(ErrorCode_NotInit, __FILENAME__, __LINE__);
    }
    if (id >= m_entries.size()) {
        throw OperationFailed(ErrorCode_BadParam, __FILENAME__, __LINE__);
    }

    return m_entries[id];
}

template <typename DictionaryIdType, typename EntryType>
std::string const& DictionaryReader<DictionaryIdType, EntryType>::get_value(DictionaryIdType id
) const {
    if (id >= m_entries.size()) {
        throw OperationFailed(ErrorCode_Corrupt, __FILENAME__, __LINE__);
    }
    return m_entries[id].get_value();
}

template <typename DictionaryIdType, typename EntryType>
std::vector<EntryType const*>
DictionaryReader<DictionaryIdType, EntryType>::get_entry_matching_value(
        std::string const& search_string,
        bool ignore_case
) const {
    std::vector<EntryType const*> entries;
    if (false == ignore_case) {
        for (auto const& entry : m_entries) {
            if (entry.get_value() == search_string) {
                entries.push_back(&entry);
                // early exit for case sensitive branch
                return entries;
            }
        }
    } else {
        auto const& search_string_uppercase = boost::algorithm::to_upper_copy(search_string);
        for (auto const& entry : m_entries) {
            if (boost::algorithm::to_upper_copy(entry.get_value()) == search_string_uppercase) {
                entries.push_back(&entry);
            }
        }
    }

    return entries;
}

template <typename DictionaryIdType, typename EntryType>
void DictionaryReader<DictionaryIdType, EntryType>::get_entries_matching_wildcard_string(
        std::string const& wildcard_string,
        bool ignore_case,
        std::unordered_set<EntryType const*>& entries
) const {
    for (auto const& entry : m_entries) {
        if (string_utils::wildcard_match_unsafe(
                    entry.get_value(),
                    wildcard_string,
                    false == ignore_case
            ))
        {
            entries.insert(&entry);
        }
    }
}

template <typename DictionaryIdType, typename EntryType>
void DictionaryReader<DictionaryIdType, EntryType>::read_segment_ids() {
    segment_id_t segment_id;
    m_segment_index_decompressor.read_numeric_value(segment_id, false);

    uint64_t num_ids;
    m_segment_index_decompressor.read_numeric_value(num_ids, false);
    for (uint64_t i = 0; i < num_ids; ++i) {
        DictionaryIdType id;
        m_segment_index_decompressor.read_numeric_value(id, false);
        if (id >= m_entries.size()) {
            throw OperationFailed(ErrorCode_Corrupt, __FILENAME__, __LINE__);
        }

        m_entries[id].add_segment_containing_entry(segment_id);
    }
}
}  // namespace clp

#endif  // CLP_DICTIONARYREADER_HPP
