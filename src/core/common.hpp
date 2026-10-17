#pragma once
#include <communicationdriver/communicationdriver.h>
#include <boost/asio.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <list>
#include <thread>
#include <vector>
#include <filesystem>

namespace cd
{
    namespace asio = boost::asio;
    using Clock = std::chrono::steady_clock;
    // Leave headroom for adding the duration to the current steady-clock time.
    inline constexpr uint64_t max_timeout_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::duration::max()).count() / 2;
    using ErrorCode = boost::system::error_code;
    using Completion = std::function<void(cd_status_t, std::string, int64_t)>;

    struct Fault : std::exception
    {
        cd_status_t code;
        std::string message;
        int64_t native;

        Fault(cd_status_t c, std::string m, int64_t n = 0)
            : code(c), message(std::move(m)), native(n)
        {
        }

        const char* what() const noexcept override
        {
            return message.c_str();
        }
    };

    std::string copy(cd_string_view_t);
    std::string copy(cd_bytes_view_t);
    cd_string_view_t view(const std::string&);
    cd_bytes_view_t bytes_view(const std::string&);
    std::string decode(const std::string&, cd_encoding_t, bool final = true);
    std::string encode(const std::string&, cd_encoding_t);
    std::filesystem::path utf8_path(const std::string&);
    size_t scalar_prefix(const std::string&, uint64_t);
    uint64_t scalar_count(const std::string&);
}
