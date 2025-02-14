#ifndef CLP_S_TIMESTAMPDICTIONARYREADER_HPP
#define CLP_S_TIMESTAMPDICTIONARYREADER_HPP

#include <map>
#include <optional>

#include "search/FilterOperation.hpp"
#include "TimestampEntry.hpp"
#include "TimestampPattern.hpp"
#include "ZstdDecompressor.hpp"

namespace clp_s {
class TimestampDictionaryReader {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}
    };

    // Methods
    /**
     * Reads the timestamp dictionary from a decompressor
     * @param decompressor
     * @return ErrorCodeSuccess on success, and the relevant ErrorCode otherwise
     */
    ErrorCode read(ZstdDecompressor& decompressor);

    /**
     * Gets the string encoding for a given epoch and format ID
     * @param epoch
     * @param format_id
     */
    std::string get_string_encoding(epochtime_t epoch, uint64_t format_id) const;

    /**
     * Gets iterators for the timestamp patterns
     * @return begin and end iterators for the timestamp patterns
     */
    auto pattern_begin() const { return m_patterns.begin(); }

    auto pattern_end() const { return m_patterns.end(); }

    /**
     * Gets iterators for the column to range mappings
     * @return begin and end iterators for the column to range mappings
     */
    auto tokenized_column_to_range_begin() const { return m_tokenized_column_to_range.begin(); }

    auto tokenized_column_to_range_end() const { return m_tokenized_column_to_range.end(); }

    std::optional<std::vector<std::string>>& get_authoritative_timestamp_tokenized_column() {
        return m_authoritative_timestamp_tokenized_column;
    }

    std::unordered_set<int32_t>& get_authoritative_timestamp_column_ids() {
        return m_authoritative_timestamp_column_ids;
    }

private:
    using id_to_pattern_t = std::map<uint64_t, TimestampPattern>;
    using tokenized_column_to_range_t
            = std::vector<std::pair<std::vector<std::string>, TimestampEntry*>>;

    // Variables
    id_to_pattern_t m_patterns;
    std::vector<TimestampEntry> m_entries;
    tokenized_column_to_range_t m_tokenized_column_to_range;

    std::optional<std::vector<std::string>> m_authoritative_timestamp_tokenized_column;
    std::unordered_set<int32_t> m_authoritative_timestamp_column_ids;
};
}  // namespace clp_s

#endif  // CLP_S_TIMESTAMPDICTIONARYREADER_HPP
