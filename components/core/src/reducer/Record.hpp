#ifndef CLP_AGGREGATION_RECORD_HPP
#define CLP_AGGREGATION_RECORD_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "RecordTypedKeyIterator.hpp"

namespace reducer {
/**
 * Class which describes a single Record containing data which can be accessed via typed key-value
 * pairs.
 */
class Record {
public:
    virtual ~Record() = default;

    [[nodiscard]] virtual std::string_view get_string_view([[maybe_unused]] std::string_view key
    ) const {
        return {};
    }

    [[nodiscard]] virtual int64_t get_int64_value([[maybe_unused]] std::string_view key) const {
        return 0;
    }

    [[nodiscard]] virtual double get_double_value([[maybe_unused]] std::string_view key) const {
        return 0.0;
    }

    [[nodiscard]] virtual std::unique_ptr<RecordTypedKeyIterator> typed_key_iter() const = 0;
};

/**
 * Record implementation which exposes a single string key-value pair.
 *
 * The value associated with the key can be updated allowing this class to act as an adapter for a
 * larger set of data.
 */
class StringRecordAdapter : public Record {
public:
    explicit StringRecordAdapter(std::string key_name) : m_key_name(std::move(key_name)) {}

    void set_record_value(std::string_view value) { m_value = value; }

    [[nodiscard]] std::string_view get_string_view(std::string_view key) const override {
        if (key == m_key_name) {
            return m_value;
        }
        return {};
    }

    [[nodiscard]] std::unique_ptr<RecordTypedKeyIterator> typed_key_iter() const override {
        return std::make_unique<SingleTypedKeyIterator>(m_key_name, ValueType::String);
    }

private:
    std::string m_key_name;
    std::string_view m_value;
};

/**
 * Record implementation which exposes a single integer key-value pair.
 *
 * The value associated with the key can be updated allowing this class to act as an adapter for a
 * larger set of data.
 */
class Int64RecordAdapter : public Record {
public:
    explicit Int64RecordAdapter(std::string key_name) : m_key_name(std::move(key_name)) {}

    void set_record_value(int64_t value) { m_value = value; }

    [[nodiscard]] int64_t get_int64_value(std::string_view key) const override {
        if (key == m_key_name) {
            return m_value;
        }
        return 0;
    }

    [[nodiscard]] std::unique_ptr<RecordTypedKeyIterator> typed_key_iter() const override {
        return std::make_unique<SingleTypedKeyIterator>(m_key_name, ValueType::Int64);
    }

private:
    std::string m_key_name;
    int64_t m_value{};
};

/**
 * Record implementation for an empty key-value pair.
 */
class EmptyRecord : public Record {
public:
    [[nodiscard]] std::unique_ptr<RecordTypedKeyIterator> typed_key_iter() const override {
        return std::make_unique<EmptyRecordTypedKeyIterator>();
    }
};
}  // namespace reducer

#endif  // CLP_AGGREGATION_RECORD_HPP
