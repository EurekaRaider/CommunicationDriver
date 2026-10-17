#pragma once
#include <communicationdriver/communicationdriver.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>
#include <map>

namespace comm
{
    using Bytes = std::vector<std::byte>;
    using Duration = std::chrono::milliseconds;
    enum class Encoding
    {
        UTF8 = CD_ENCODING_UTF8,
        ASCII = CD_ENCODING_ASCII,
        GB18030 = CD_ENCODING_GB18030
    };
    enum class ExecutionMode
    {
        Persistent = CD_PERSISTENT,
        OneShot = CD_ONE_SHOT
    };
    enum class ShellKind
    {
        Default,
        Cmd,
        WindowsPowerShell,
        Pwsh,
        Sh,
        Bash,
        Zsh
    };
    enum class State
    {
        Created,
        Opening,
        Open,
        Closing,
        Closed,
        Faulted,
        NeedsRecovery
    };
    enum class Stream
    {
        StandardOutput,
        StandardError
    };
    enum class CompletionReason
    {
        None,
        CommandCompleted,
        ProtocolBoundary,
        Idle,
        Matched
    };
    enum class BatchItemState
    {
        Completed,
        Failed,
        TimedOut,
        Cancelled,
        NotStarted
    };
    enum class Parity
    {
        None,
        Odd,
        Even
    };
    enum class StopBits
    {
        One,
        OnePointFive,
        Two
    };
    enum class FlowControl
    {
        None,
        Software,
        Hardware
    };

    struct ErrorInfo
    {
        cd_status_t code = CD_OK;
        std::string message, backend, partial_text, partial_file;
        int64_t native_code = 0;
        Bytes partial_bytes;
        std::optional<uint64_t> confirmed_progress;
    };

    class CommunicationError : public std::runtime_error
    {
      public:
        ErrorInfo info;

        explicit CommunicationError(ErrorInfo e) : std::runtime_error(e.message), info(std::move(e))
        {
        }
    };

    namespace detail
    {
        inline cd_string_view_t view(std::string_view s)
        {
            return {s.data(), s.size()};
        }

        inline cd_bytes_view_t view(std::span<const std::byte> b)
        {
            return {reinterpret_cast<const uint8_t*>(b.data()), b.size()};
        }

        inline std::string string(cd_string_view_t s)
        {
            return s.size ? std::string(s.data, s.size) : "";
        }

        inline Bytes bytes(cd_bytes_view_t b)
        {
            if (!b.size)
                return {};
            auto begin = reinterpret_cast<const std::byte*>(b.data);
            return {begin, begin + b.size};
        }

        template <class T> T sized()
        {
            T t{};
            t.struct_size = sizeof(T);
            return t;
        }

        inline ErrorInfo error(cd_result_t* r, cd_status_t status = CD_OK)
        {
            auto e = sized<cd_error_info_t>();
            cd_result_get_error(r, &e);

            return {r ? e.code : status,
                    r ? string(e.message) : cd_status_name(status),
                    string(e.backend),
                    string(e.partial_text),
                    string(e.partial_file),
                    e.native_code,
                    bytes(e.partial_bytes),
                    e.has_confirmed_progress ? std::optional<uint64_t>(e.confirmed_progress)
                                             : std::nullopt};
        }

        inline void checked(cd_status_t s, cd_result_t* r = nullptr)
        {
            if (s != CD_OK)
                throw CommunicationError(error(r, s));
        }

        struct ResultHandle
        {
            cd_result_t* h = nullptr;

            explicit ResultHandle(cd_result_t* r) : h(r)
            {
            }

            ~ResultHandle()
            {
                cd_result_release(h);
            }

            ResultHandle(const ResultHandle&) = delete;
        };
    }
}
