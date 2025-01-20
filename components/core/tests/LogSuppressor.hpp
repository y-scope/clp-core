#ifndef TEST_LOGSUPPRESSOR_HPP
#define TEST_LOGSUPPRESSOR_HPP

#include <spdlog/spdlog.h>

/**
 * A class that suppresses logs so long as it's instantiated.
 */
class LogSuppressor {
public:
    LogSuppressor() {
        m_previous_logging_level = spdlog::default_logger()->level();
        spdlog::default_logger()->set_level(spdlog::level::off);
    }

    ~LogSuppressor() { spdlog::default_logger()->set_level(m_previous_logging_level); }

    // Delete copy & move constructors and assignment operators
    LogSuppressor(LogSuppressor const& other) = delete;
    LogSuppressor(LogSuppressor&& other) = delete;
    auto operator=(LogSuppressor const& other) -> LogSuppressor& = delete;
    auto operator=(LogSuppressor&& other) -> LogSuppressor& = delete;

private:
    spdlog::level::level_enum m_previous_logging_level;
};

#endif  // TEST_LOGSUPPRESSOR_HPP
