#include "TimestampDictionaryWriter.hpp"

#include "Utils.hpp"

namespace clp_s {
void TimestampDictionaryWriter::write_timestamp_entries(
        std::map<std::string, TimestampEntry> const& ranges,
        ZstdCompressor& compressor
) {
    compressor.write_numeric_value<uint64_t>(ranges.size());

    for (auto const& range : ranges) {
        range.second.write_to_file(compressor);
    }
}

void TimestampDictionaryWriter::write(ZstdCompressor& compressor) {
    merge_range();
    write_timestamp_entries(m_column_key_to_range, compressor);

    compressor.write_numeric_value<uint64_t>(m_pattern_to_id.size());
    for (auto& it : m_pattern_to_id) {
        // write pattern ID
        compressor.write_numeric_value<uint64_t>(it.second);

        std::string const& pattern = it.first->get_format();
        compressor.write_numeric_value<uint64_t>(pattern.length());
        compressor.write_string(pattern);
    }
}

uint64_t TimestampDictionaryWriter::get_pattern_id(TimestampPattern const* pattern) {
    auto it = m_pattern_to_id.find(pattern);
    if (m_pattern_to_id.end() == it) {
        uint64_t id = m_next_id++;
        m_pattern_to_id.emplace(pattern, id);
        return id;
    }
    return it->second;
}

epochtime_t TimestampDictionaryWriter::ingest_entry(
        std::string const& key,
        int32_t node_id,
        std::string const& timestamp,
        uint64_t& pattern_id
) {
    epochtime_t ret;
    size_t timestamp_begin_pos = 0, timestamp_end_pos = 0;
    TimestampPattern const* pattern{nullptr};

    // Try parsing the timestamp as one of the previously seen timestamp patterns
    for (auto it : m_pattern_to_id) {
        if (it.first->parse_timestamp(timestamp, ret, timestamp_begin_pos, timestamp_end_pos)) {
            pattern = it.first;
            pattern_id = it.second;
            break;
        }
    }

    // Fall back to consulting all known timestamp patterns
    if (nullptr == pattern) {
        pattern = TimestampPattern::search_known_ts_patterns(
                timestamp,
                ret,
                timestamp_begin_pos,
                timestamp_end_pos
        );
        pattern_id = get_pattern_id(pattern);
    }

    if (nullptr == pattern) {
        throw OperationFailed(ErrorCodeFailure, __FILE__, __LINE__);
    }

    auto entry = m_column_id_to_range.find(node_id);
    if (entry == m_column_id_to_range.end()) {
        TimestampEntry new_entry(key);
        new_entry.ingest_timestamp(ret);
        m_column_id_to_range.emplace(node_id, std::move(new_entry));
    } else {
        entry->second.ingest_timestamp(ret);
    }

    return ret;
}

void TimestampDictionaryWriter::ingest_entry(
        std::string const& key,
        int32_t node_id,
        double timestamp
) {
    auto entry = m_column_id_to_range.find(node_id);
    if (entry == m_column_id_to_range.end()) {
        TimestampEntry new_entry(key);
        new_entry.ingest_timestamp(timestamp);
        m_column_id_to_range.emplace(node_id, std::move(new_entry));
    } else {
        entry->second.ingest_timestamp(timestamp);
    }
}

void TimestampDictionaryWriter::ingest_entry(
        std::string const& key,
        int32_t node_id,
        int64_t timestamp
) {
    auto entry = m_column_id_to_range.find(node_id);
    if (entry == m_column_id_to_range.end()) {
        TimestampEntry new_entry(key);
        new_entry.ingest_timestamp(timestamp);
        m_column_id_to_range.emplace(node_id, std::move(new_entry));
    } else {
        entry->second.ingest_timestamp(timestamp);
    }
}

void TimestampDictionaryWriter::merge_range() {
    for (auto const& it : m_column_id_to_range) {
        std::string key = it.second.get_key_name();
        auto entry = m_column_key_to_range.find(key);
        if (entry == m_column_key_to_range.end()) {
            TimestampEntry new_entry = it.second;
            new_entry.insert_column_id(it.first);
            m_column_key_to_range.emplace(key, std::move(new_entry));
        } else {
            entry->second.merge_range(it.second);
            entry->second.insert_column_id(it.first);
        }
    }
}

epochtime_t TimestampDictionaryWriter::get_begin_timestamp() const {
    auto it = m_column_key_to_range.begin();
    if (m_column_key_to_range.end() == it) {
        // replicate behaviour of CLP
        return 0;
    }

    return it->second.get_begin_timestamp();
}

epochtime_t TimestampDictionaryWriter::get_end_timestamp() const {
    auto it = m_column_key_to_range.begin();
    if (m_column_key_to_range.end() == it) {
        // replicate behaviour of CLP
        return 0;
    }

    return it->second.get_end_timestamp();
}

void TimestampDictionaryWriter::clear() {
    m_next_id = 0;
    m_pattern_to_id.clear();
    m_column_key_to_range.clear();
    m_column_id_to_range.clear();
}

size_t TimestampDictionaryWriter::size_in_bytes() {
    merge_range();
    size_t size{2 * sizeof(uint64_t)};
    for (auto const& range : m_column_key_to_range) {
        size += range.second.size_in_bytes();
    }

    for (auto& pattern : m_pattern_to_id) {
        size += 2 * sizeof(uint64_t);
        size += pattern.first->get_format().size();
    }
    return size;
}
}  // namespace clp_s
