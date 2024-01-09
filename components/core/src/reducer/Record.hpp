#ifndef CLP_AGGREGATION_RECORD_HPP
#define CLP_AGGREGATION_RECORD_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

#include "RecordValueIterator.hpp"

namespace reducer {
class Record {
public:
    virtual std::string_view get_string_value(std::string const& key) const {
        return std::string_view();
    }

    virtual int64_t get_int64_value(std::string const& key) const { return 0.0; }

    virtual double get_double_value(std::string const& key) const { return 0; }

    virtual std::unique_ptr<RecordValueIterator> value_it() const = 0;

    virtual ~Record() {}
};

class StringRecordAdapter : public Record {
public:
    StringRecordAdapter(std::string key_name) : m_key_name(key_name) {}

    void set_record_value(std::string_view value) { m_value = value; }

    virtual std::string_view get_string_value(std::string const& key) const {
        if (key == m_key_name) {
            return m_value;
        }
        return std::string_view();
    }

    virtual std::unique_ptr<RecordValueIterator> value_it() const {
        return std::unique_ptr<RecordValueIterator>(
                new SimpleSingleValueIterator(m_key_name, ValueType::STRING)
        );
    }

private:
    std::string m_key_name;
    std::string_view m_value;
};

class Int64RecordAdapter : public Record {
public:
    Int64RecordAdapter(std::string key_name) : m_key_name(key_name) {}

    void set_record_value(int64_t value) { m_value = value; }

    virtual int64_t get_int64_value(std::string const& key) const {
        if (key == m_key_name) {
            return m_value;
        }
        return 0;
    }

    virtual std::unique_ptr<RecordValueIterator> value_it() const {
        return std::unique_ptr<RecordValueIterator>(
                new SimpleSingleValueIterator(m_key_name, ValueType::INT64)
        );
    }

private:
    std::string m_key_name;
    int64_t m_value;
};

class EmptyRecord : public Record {
public:
    EmptyRecord() {}

    virtual std::unique_ptr<RecordValueIterator> value_it() const {
        return std::unique_ptr<RecordValueIterator>(new EmptyRecordValueIterator());
    }
};
}  // namespace reducer

#endif  // CLP_AGGREGATION_RECORD_HPP
