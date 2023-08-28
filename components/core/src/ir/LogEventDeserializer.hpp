#ifndef IR_LOGEVENTDESERIALIZER_HPP
#define IR_LOGEVENTDESERIALIZER_HPP

#include <optional>

#include <boost/outcome/std_result.hpp>

#include "../ffi/encoding_methods.hpp"
#include "../ReaderInterface.hpp"
#include "../TimestampPattern.hpp"
#include "../TraceableException.hpp"
#include "LogEvent.hpp"

namespace ir {
/**
 * Class for deserializing IR log events from an IR stream.
 *
 * TODO: We're currently returning std::errc error codes, but we should replace
 * these with our own custom error codes (derived from std::error_code), ideally
 * replacing IRErrorCode.
 * @tparam encoded_variable_t Type of encoded variables in the stream
 */
template <typename encoded_variable_t>
class LogEventDeserializer {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        [[nodiscard]] auto what() const noexcept -> char const* override {
            return "ir::LogEventParser operation failed";
        }
    };

    // Factory functions
    /**
     * Creates a log event deserializer for the given stream
     * @param reader A reader for the IR stream
     * @return A result containing the serializer or an error code indicating
     * the failure
     */
    static auto create(ReaderInterface& reader)
            -> BOOST_OUTCOME_V2_NAMESPACE::std_result<LogEventDeserializer<encoded_variable_t>>;

    // Delete copy constructor and assignment
    LogEventDeserializer(LogEventDeserializer const&) = delete;
    auto operator=(LogEventDeserializer const&) -> LogEventDeserializer& = delete;

    // Define default move constructor and assignment
    LogEventDeserializer(LogEventDeserializer&&) = default;
    auto operator=(LogEventDeserializer&&) -> LogEventDeserializer& = default;

    // Methods
    [[nodiscard]] auto get_timestamp_pattern() const -> TimestampPattern const& {
        return m_timestamp_pattern;
    }

    /**
     * Deserializes a log event from the stream
     * @return A result containing the log event or an error code indicating
     * the failure
     */
    [[nodiscard]] auto deserialize_log_event()
            -> BOOST_OUTCOME_V2_NAMESPACE::std_result<LogEvent<encoded_variable_t>>;

private:
    // Constructors
    explicit LogEventDeserializer(ReaderInterface& reader) : m_reader{reader} {}

    LogEventDeserializer(ReaderInterface& reader, ffi::epoch_time_ms_t ref_timestamp)
            : m_reader{reader},
              m_prev_msg_timestamp{ref_timestamp} {}

    // Variables
    TimestampPattern m_timestamp_pattern{0, "%Y-%m-%dT%H:%M:%S.%3"};
    [[no_unique_address]] std::conditional_t<
            std::is_same_v<encoded_variable_t, ffi::four_byte_encoded_variable_t>,
            ffi::epoch_time_ms_t,
            EmptyType>
            m_prev_msg_timestamp{};
    ReaderInterface& m_reader;
};
}  // namespace ir

#include "LogEventDeserializer.tpp"

#endif  // IR_LOGEVENTDESERIALIZER_HPP
