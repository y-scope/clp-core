#ifndef CLP_NETWORKREADER_HPP
#define CLP_NETWORKREADER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <ystdlib/container/Array.hpp>

#include <curl/curl.h>

#include "CurlDownloadHandler.hpp"
#include "CurlGlobalInstance.hpp"
#include "ErrorCode.hpp"
#include "ReaderInterface.hpp"
#include "Thread.hpp"
#include "TraceableException.hpp"

namespace clp {
/**
 * This class implements the ReaderInterface to stream data directly from a given URL (e.g.,
 * streaming from a pre-signed S3 URL). This class uses a background thread to download and buffer
 * data in buffers from a buffer pool. When buffering data, if no empty buffer is available, the
 * downloading thread will block until there is, or until the download times out. Any read
 * operations will read from the next filled buffer from a queue. If no filled buffer is available,
 * the thread calling read will block until there is a filled buffer, or the download times out.
 */
class NetworkReader : public ReaderInterface {
public:
    // Types
    using BufferView = std::span<char>;

    /**
     * The exception thrown by this class.
     */
    class OperationFailed : public TraceableException {
    public:
        OperationFailed(ErrorCode error_code, char const* const filename, int line_number)
                : TraceableException(error_code, filename, line_number) {}

        [[nodiscard]] auto what() const noexcept -> char const* override {
            return "clp::NetworkReader operation failed.";
        }
    };

    /**
     * The possible states of the reader.
     */
    enum class State : uint8_t {
        // The reader is downloading data from the source URL.
        InProgress = 0,
        // The reader has failed to download data from the source URL.
        Failed,
        // The data at the given URL has been downloaded.
        Finished
    };

    // Constants
    static constexpr size_t cDefaultBufferPoolSize{8};
    static constexpr size_t cDefaultBufferSize{4096};

    static constexpr size_t cMinBufferPoolSize{2};
    static constexpr size_t cMinBufferSize{512};

    /**
     * Constructs a reader to stream data from the given URL, starting at the given offset.
     * NOTE: This class depends on `libcurl`, so an instance of `clp::CurlGlobalInstance` must
     * remain alive for the entire lifespan of any instance of this class.
     *
     * This class maintains an instance of `CurlGlobalInstance` in case the user forgets to
     * instantiate one, but it is better for performance if the user instantiates one. For instance,
     * if the user doesn't instantiate a `CurlGlobalInstance` and only ever creates one
     * `NetworkReader` at a time, then every construction and destruction of `NetworkReader` would
     * cause `libcurl` to init and deinit. In contrast, if the user instantiates a
     * `CurlGlobalInstance` before instantiating any `NetworkReader`s, then the `CurlGlobalInstance`
     * maintained by this class will simply be a reference to the existing one rather than
     * initializing and deinitializing `libcurl`.
     *
     * @param src_url
     * @param offset Index of the byte at which to start the download
     * @param disable_caching Whether to disable the caching.
     * @param overall_timeout Maximum time that the download may take. Note that this includes
     * `connection_timeout`. Doc: https://curl.se/libcurl/c/CURLOPT_TIMEOUT.html
     * @param connection_timeout Maximum time that the connection phase may take.
     * Doc: https://curl.se/libcurl/c/CURLOPT_CONNECTTIMEOUT.html
     * @param buffer_pool_size The required number of buffers in the buffer pool.
     * @param buffer_size The size of each buffer in the buffer pool.
     * @param http_header_kv_pairs Key-value pairs representing HTTP headers to pass to the server
     * in the download request. Doc: https://curl.se/libcurl/c/CURLOPT_HTTPHEADER.html
     */
    explicit NetworkReader(
            std::string_view src_url,
            size_t offset = 0,
            bool disable_caching = false,
            std::chrono::seconds overall_timeout = CurlDownloadHandler::cDefaultOverallTimeout,
            std::chrono::seconds connection_timeout
            = CurlDownloadHandler::cDefaultConnectionTimeout,
            size_t buffer_pool_size = cDefaultBufferPoolSize,
            size_t buffer_size = cDefaultBufferSize,
            std::optional<std::unordered_map<std::string, std::string>> http_header_kv_pairs
            = std::nullopt
    );

    // Destructor
    ~NetworkReader() override;

    // Copy/Move Constructors
    // These are disabled since this class' synchronization primitives are non-copyable and
    // non-moveable.
    NetworkReader(NetworkReader const&) = delete;
    NetworkReader(NetworkReader&&) = delete;
    auto operator=(NetworkReader const&) -> NetworkReader& = delete;
    auto operator=(NetworkReader&&) -> NetworkReader& = delete;

    // Methods implementing `clp::ReaderInterface`
    /**
     * Tries to read up to a given number of bytes from the buffered data.
     * @param buf
     * @param num_bytes_to_read
     * @param num_bytes_read Returns the number of bytes read.
     * @return ErrorCode_EndOfFile if there is no more buffered data.
     * @return ErrorCode_Success on success.
     */
    [[nodiscard]] auto try_read(char* buf, size_t num_bytes_to_read, size_t& num_bytes_read)
            -> ErrorCode override {
        return read_from_filled_buffers(num_bytes_to_read, num_bytes_read, buf);
    }

    /**
     * Tries to seek to the given position, relative to the beginning of the data.
     * @param pos
     * @return ErrorCode_Unsupported if the given position is lower than the current position.
     * Since this class only supports streaming, it cannot seek backwards.
     * @return ErrorCode_OutOfBounds if the given pos is past the end of the data.
     * @return ErrorCode_Success on success.
     */
    [[nodiscard]] auto try_seek_from_begin(size_t pos) -> ErrorCode override {
        if (pos < m_file_pos) {
            return ErrorCode_Unsupported;
        }
        if (pos == m_file_pos) {
            return ErrorCode_Success;
        }
        size_t num_bytes_read{};
        auto const num_bytes_to_read{pos - m_file_pos};
        auto const err{read_from_filled_buffers(num_bytes_to_read, num_bytes_read, nullptr)};
        if (ErrorCode_EndOfFile == err
            || (ErrorCode_Success == err && num_bytes_read < num_bytes_to_read))
        {
            return ErrorCode_OutOfBounds;
        }
        return err;
    }

    /**
     * @param pos Returns the position of the read head in the buffer.
     * @return ErrorCode_Failure if the initial offset is not 0 and the HTTP request returned an
     * error.
     * @return ErrorCode_NotReady if the initial offset is not 0 and no bytes have been downloaded
     * yet when this function is called.
     * @return ErrorCode_Success on success.
     */
    [[nodiscard]] auto try_get_pos(size_t& pos) -> ErrorCode override;

    // Methods
    /**
     * @return Whether the caller requested that the download (curl transfer) be aborted.
     */
    [[nodiscard]] auto is_abort_download_requested() const -> bool {
        return m_abort_download_requested.load();
    }

    /**
     * @return Whether the downloader thread is running.
     */
    [[nodiscard]] auto is_downloader_thread_running() const -> bool {
        if (nullptr == m_downloader_thread) {
            return false;
        }
        return m_downloader_thread->is_running();
    }

    /**
     * Buffers the downloaded data in the currently acquired or next available buffer from the
     * buffer pool.
     * NOTE: This function should be called by the libcurl write callback only.
     * @param data
     * @return Number of bytes buffered.
     */
    [[nodiscard]] auto buffer_downloaded_data(BufferView data) -> size_t;

    /**
     * @return Whether the downloader thread is still downloading data.
     */
    [[nodiscard]] auto is_download_in_progress() const -> bool {
        return get_state() == State::InProgress;
    }

    /**
     * @returns Whether the download has timed out.
     */
    [[nodiscard]] auto is_download_timed_out() const -> bool {
        auto const curl_return_code{get_curl_ret_code()};
        return curl_return_code.has_value() && CURLE_OPERATION_TIMEDOUT == curl_return_code.value();
    }

    /**
     * @return curl return code if the download has completed (successfully/unsuccessfully).
     * @return std::nullopt if the download is still in-progress.
     */
    [[nodiscard]] auto get_curl_ret_code() const -> std::optional<CURLcode> {
        return m_curl_ret_code.load();
    }

    /**
     * @return The error message set by the underlying CURL handler.
     * @return std::nullopt if the download is still in-progress or no error has occured.
     */
    [[nodiscard]] auto get_curl_error_msg() const -> std::optional<std::string_view> {
        if (auto const ret_code{get_curl_ret_code()};
            false == ret_code.has_value() || CURLE_OK == ret_code.value())
        {
            return std::nullopt;
        }
        return std::string_view{m_curl_error_msg_buf->data()};
    }

private:
    /**
     * This class implements clp::Thread to download data using CURL.
     */
    class DownloaderThread : public Thread {
    public:
        // Constructor
        /**
         * Constructs a clp::thread for data downloading.
         * @param reader
         * @param offset Index of the byte at which to start the download.
         * @param disable_caching Whether to disable caching.
         * @param http_header_kv_pairs Key-value pairs representing HTTP headers to pass to the
         * server in the download request. Doc: https://curl.se/libcurl/c/CURLOPT_HTTPHEADER.html
         */
        DownloaderThread(
                NetworkReader& reader,
                size_t offset,
                bool disable_caching,
                std::optional<std::unordered_map<std::string, std::string>> http_header_kv_pairs
        )
                : m_reader{reader},
                  m_offset{offset},
                  m_disable_caching{disable_caching},
                  m_http_header_kv_pairs{std::move(http_header_kv_pairs)} {}

    private:
        // Methods implementing `clp::Thread`
        auto thread_method() -> void final;

        NetworkReader& m_reader;
        size_t m_offset{0};
        bool m_disable_caching{false};
        std::optional<std::unordered_map<std::string, std::string>> m_http_header_kv_pairs;
    };

    /**
     * Submits a request to abort the ongoing curl download session.
     */
    auto submit_abort_download_request() -> void;

    /**
     * Acquires an empty buffer (if curr_downloader_buf is unset) to write downloaded data.
     */
    auto acquire_empty_buffer() -> void;

    /**
     * Releases an empty buffer which has been fully consumed by the reader.
     */
    auto release_empty_buffer() -> void;

    /**
     * Enqueues the current downloader buffer (if set) into the filled buffer queue.
     */
    auto enqueue_filled_buffer() -> void;

    /**
     * Gets a filled buffer (if curr_downloader_buf is unset) from the filled buffer queue.
     */
    auto get_filled_buffer() -> void;

    /**
     * Reads data from the filled buffers with a given amount of bytes.
     * @param num_bytes_to_read
     * @param num_bytes_read Returns the number of bytes read.
     * @param dst A pointer to a destination buffer. If the pointer is not null, data will be
     * copied to the destination.
     * @return ErrorCode_EndOfFile if the buffer doesn't contain any more data.
     * @return ErrorCode_Success on success.
     */
    [[nodiscard]] auto
    read_from_filled_buffers(size_t num_bytes_to_read, size_t& num_bytes_read, char* dst)
            -> ErrorCode;

    /**
     * Sets the download completion status with the return code from curl.
     * @param curl_code
     */
    auto set_download_completion_status(CURLcode curl_code) -> void {
        // NOTE: The ordering of these statements is important so when callers see the download is
        // complete, they can check the CURL return code afterward.
        m_curl_ret_code.store(curl_code);
        m_state.store((CURLE_OK == curl_code) ? State::Finished : State::Failed);
    }

    [[nodiscard]] auto get_state() const -> State { return m_state.load(); }

    /**
     * @return Whether at least one byte has been downloaded through the CURL write callback.
     */
    [[nodiscard]] auto at_least_one_byte_downloaded() const -> bool {
        return m_at_least_one_byte_downloaded.load();
    }

    CurlGlobalInstance m_curl_global_instance;

    std::string m_src_url;

    size_t m_offset{0};
    size_t m_file_pos{0};

    std::chrono::seconds m_overall_timeout;
    std::chrono::seconds m_connection_timeout;

    size_t m_buffer_pool_size{cDefaultBufferPoolSize};
    size_t m_buffer_size{cDefaultBufferSize};
    size_t m_curr_downloader_buf_idx{0};

    std::vector<ystdlib::container::Array<char>> m_buffer_pool;
    std::queue<BufferView> m_filled_buffer_queue;
    std::optional<BufferView> m_curr_downloader_buf;
    std::optional<BufferView> m_curr_reader_buf;

    std::mutex m_buffer_resource_mutex;
    std::condition_variable m_downloader_cv;
    std::condition_variable m_reader_cv;

    std::unique_ptr<DownloaderThread> m_downloader_thread{nullptr};
    std::atomic<bool> m_at_least_one_byte_downloaded{false};
    std::atomic<bool> m_abort_download_requested{false};

    // These two members should only be set from `set_download_completion_status`
    std::atomic<State> m_state{State::InProgress};
    std::atomic<std::optional<CURLcode>> m_curl_ret_code;

    std::shared_ptr<CurlDownloadHandler::ErrorMsgBuf> m_curl_error_msg_buf{
            std::make_shared<CurlDownloadHandler::ErrorMsgBuf>()
    };
};
}  // namespace clp

#endif  // CLP_NETWORKREADER_HPP
