#pragma once
#include "options.hpp"

namespace comm
{
    struct CheckResult
    {
        bool passed = false;
        std::optional<std::string> matched_text;
        std::optional<Bytes> matched_bytes;
        std::vector<std::string> groups;
        std::string failure_reason;
    };

    template <class T> struct ExecutionResult
    {
        T response{}, standard_error{};
        std::optional<CheckResult> check;
        std::optional<int> exit_code;
        CompletionReason completion_reason = CompletionReason::None;
        std::chrono::nanoseconds elapsed{};
    };

    struct TransferResult
    {
        std::string source, destination;
        std::optional<uint64_t> bytes_transferred;
        std::chrono::nanoseconds elapsed{};
    };

    namespace detail
    {
        inline std::optional<CheckResult> check_result(cd_result_t* h)
        {
            auto c = sized<cd_check_result_t>();
            checked(cd_result_get_check(h, &c));

            if (!c.has_value)
                return {};

            CheckResult r;
            r.passed = c.passed;

            if (c.matched_text.data)
                r.matched_text = string(c.matched_text);

            if (c.matched_bytes.data)
                r.matched_bytes = bytes(c.matched_bytes);

            for (uint64_t i = 0; i < c.group_count; ++i)
                r.groups.push_back(string(c.groups[i]));

            r.failure_reason = string(c.failure_reason);

            return r;
        }

        template <class T> T response(cd_result_t* h, int stream)
        {
            if constexpr (std::is_same_v<T, std::string>)
            {
                cd_string_view_t v{};
                checked(cd_result_get_text(h, stream, &v));
                return string(v);
            }
            else
            {
                cd_bytes_view_t v{};
                checked(cd_result_get_bytes(h, stream, &v));
                return bytes(v);
            }
        }

        template <class T> T convert(cd_result_t* h)
        {
            checked(cd_result_get_status(h), h);

            if constexpr (std::is_same_v<T, void>)
                return;
            else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, Bytes>)
                return response<T>(h, 0);
            else if constexpr (std::is_same_v<T, TransferResult>)
            {
                auto i = sized<cd_transfer_result_t>();
                checked(cd_result_get_transfer(h, &i));
                return {string(i.source), string(i.destination),
                        i.has_bytes_transferred ? std::optional<uint64_t>(i.bytes_transferred)
                                                : std::nullopt,
                        std::chrono::nanoseconds(i.elapsed_ns)};
            }
            else
            {
                using R = decltype(T::response);

                auto info = sized<cd_execution_info_t>();
                checked(cd_result_get_execution_info(h, &info));

                return {response<R>(h, 0),
                        response<R>(h, 1),
                        check_result(h),
                        info.has_exit_code ? std::optional<int>(info.exit_code) : std::nullopt,
                        static_cast<CompletionReason>(info.completion_reason),
                        std::chrono::nanoseconds(info.elapsed_ns)};
            }
        }

        struct OperationControl
        {
            cd_operation_t* handle;
            int stream = 0;
            std::atomic<bool> consumed{false};
            std::unique_ptr<std::stop_callback<std::function<void()>>> cancellation;

            explicit OperationControl(cd_operation_t* h, int s) : handle(h), stream(s)
            {
            }

            ~OperationControl()
            {
                cancellation.reset();
                cd_operation_release(handle);
            }
        };
    }
}
