#ifndef CLP_AGGREGATION_RECORD_GROUP_SERDES_HPP
#define CLP_AGGREGATION_RECORD_GROUP_SERDES_HPP

#include <iostream>
#include <utility>
#include <vector>

#include <json/single_include/nlohmann/json.hpp>

#include "ConstRecordIterator.hpp"
#include "JsonArrayRecordIterator.hpp"
#include "JsonRecord.hpp"
#include "Record.hpp"
#include "RecordGroup.hpp"
#include "RecordTypedKeyIterator.hpp"

namespace reducer {
/**
 * Class which converts serialized data into a RecordGroup and exposes iterators to the underlying
 * data.
 *
 * The serialized data comes from the "serialize" function declared in this file.
 */
class DeserializedRecordGroup : public RecordGroup {
public:
    explicit DeserializedRecordGroup(std::vector<uint8_t>& serialized_data);
    DeserializedRecordGroup(char* buf, size_t len);

    [[nodiscard]] ConstRecordIterator& record_iter() override {
        return m_record_it;
    }

    [[nodiscard]] GroupTags const& get_tags() const override {
        return m_tags;
    }

private:
    void init_tags_from_json();

    GroupTags m_tags;
    nlohmann::json m_record_group;
    JsonArrayRecordIterator m_record_it;
};

std::vector<uint8_t> serialize(
        GroupTags const& tags,
        ConstRecordIterator& record_it,
        std::vector<uint8_t>(ser)(nlohmann::json const& j) = nlohmann::json::to_msgpack
);

std::vector<uint8_t> serialize_timeline(GroupTags const& tags, ConstRecordIterator& record_it);

DeserializedRecordGroup deserialize(std::vector<uint8_t>& data);

DeserializedRecordGroup deserialize(char* buf, size_t len);
}  // namespace reducer

#endif  // CLP_AGGREGATION_RECORD_GROUP_SERDES_HPP
