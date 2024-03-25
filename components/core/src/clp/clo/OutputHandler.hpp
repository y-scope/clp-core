#ifndef CLP_CLO_OUTPUTHANDLER_HPP
#define CLP_CLO_OUTPUTHANDLER_HPP

#include <unistd.h>

#include <queue>
#include <string>

#include <mongocxx/client.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/uri.hpp>

#include "../../reducer/Pipeline.hpp"
#include "../Defs.h"
#include "../streaming_archive/MetadataDB.hpp"
#include "../TraceableException.hpp"

namespace clp::clo {
/**
 * Abstract class for handling output from a search.
 */
class OutputHandler {
public:
    // Destructor
    virtual ~OutputHandler() = default;

    // Methods
    /**
     * Adds a query result to a batch or sends it to the destination.
     * @param original_path The original path of the log event.
     * @param message The content of the log event.
     * @param timestamp The timestamp of the log event.
     * @return ErrorCode_Success if the result was added successfully, an error code otherwise.
     */
    virtual ErrorCode
    add_result(std::string const& original_path, std::string const& message, epochtime_t timestamp)
            = 0;

    /**
     * Flushes any buffered output. Called once at the end of search.
     */
    virtual void flush() = 0;

    /**
     * @param it
     * @return Whether a file can be skipped based on the current state of the output handler, and
     * metadata about the file
     */
    [[nodiscard]] virtual bool can_skip_file(
            [[maybe_unused]] clp::streaming_archive::MetadataDB::FileIterator const& it
    ) {
        return false;
    }
};

/**
 * Class encapsulating a network client used to send query results to a network destination.
 */
class NetworkOutputHandler : public OutputHandler {
public:
    // Types
    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        char const* what() const noexcept override {
            return "NetworkOutputHandler operation failed";
        }
    };

    // Constructors
    NetworkOutputHandler(std::string const& host, int port);

    // Destructor
    ~NetworkOutputHandler() override { close(m_socket_fd); }

    // Methods inherited from Client
    /**
     * Sends a result to the network destination.
     * @param original_path
     * @param message
     * @param timestamp
     * @return Same as networking::try_send
     */
    ErrorCode add_result(
            std::string const& original_path,
            std::string const& message,
            epochtime_t timestamp
    ) override;

    void flush() override { close(m_socket_fd); }

private:
    int m_socket_fd;
};

/**
 * Class encapsulating a MongoDB client used to send query results to the results cache.
 */
class ResultsCacheOutputHandler : public OutputHandler {
public:
    // Types
    struct QueryResult {
        // Constructors
        QueryResult(std::string original_path, std::string message, epochtime_t timestamp)
                : original_path(std::move(original_path)),
                  message(std::move(message)),
                  timestamp(timestamp) {}

        std::string original_path;
        std::string message;
        epochtime_t timestamp;
    };

    struct QueryResultGreaterTimestampComparator {
        bool operator()(
                std::unique_ptr<QueryResult> const& r1,
                std::unique_ptr<QueryResult> const& r2
        ) const {
            return r1->timestamp > r2->timestamp;
        }
    };

    class OperationFailed : public TraceableException {
    public:
        // Constructors
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        // Methods
        char const* what() const noexcept override {
            return "ResultsCacheOutputHandler operation failed";
        }
    };

    // Constructors
    ResultsCacheOutputHandler(
            std::string const& uri,
            std::string const& collection,
            uint64_t batch_size,
            uint64_t max_num_results
    );

    // Methods inherited from OutputHandler
    /**
     * Adds a result to the batch.
     * @param original_path
     * @param message
     * @param timestamp
     * @return ErrorCode_Success
     */
    ErrorCode add_result(
            std::string const& original_path,
            std::string const& message,
            epochtime_t timestamp
    ) override;

    void flush() override;

    [[nodiscard]] bool can_skip_file(clp::streaming_archive::MetadataDB::FileIterator const& it
    ) override {
        return is_latest_results_full() && get_smallest_timestamp() > it.get_end_ts();
    }

private:
    /**
     * @return The earliest (smallest) timestamp in the heap of latest results
     */
    [[nodiscard]] epochtime_t get_smallest_timestamp() const {
        return m_latest_results.empty() ? cEpochTimeMin : m_latest_results.top()->timestamp;
    }

    /**
     * @return Whether the heap of latest results is full.
     */
    [[nodiscard]] bool is_latest_results_full() const {
        return m_latest_results.size() >= m_max_num_results;
    }

    mongocxx::client m_client;
    mongocxx::collection m_collection;
    std::vector<bsoncxx::document::value> m_results;
    uint64_t m_batch_size;
    uint64_t m_max_num_results;
    // The search results with the latest timestamps
    std::priority_queue<
            std::unique_ptr<QueryResult>,
            std::vector<std::unique_ptr<QueryResult>>,
            QueryResultGreaterTimestampComparator>
            m_latest_results;
};

/**
 * Class encapsulating a reducer client used to send count aggregation results to the reducer.
 */
class CountOutputHandler : public OutputHandler {
public:
    // Constructor
    explicit CountOutputHandler(int reducer_socket_fd);

    // Methods inherited from OutputHandler
    /**
     * Adds a result.
     * @param original_path
     * @param message
     * @param timestamp
     * @return ErrorCode_Success
     */
    ErrorCode add_result(
            std::string const& original_path,
            std::string const& message,
            epochtime_t timestamp
    ) override;

    void flush() override;

private:
    int m_reducer_socket_fd;
    reducer::Pipeline m_pipeline;
};

}  // namespace clp::clo

#endif  // CLP_CLO_OUTPUTHANDLER_HPP
