#pragma once
#include "operation.hpp"

namespace comm
{
    class Driver;

    class Connection
    {
        std::shared_ptr<cd_connection_t> handle_;
        std::shared_ptr<cd_driver_t> driver_;
        friend class Driver;
        friend struct BatchItem;
        friend class Batch;

        Connection(cd_connection_t* h, std::shared_ptr<cd_driver_t> d)
            : handle_(h, cd_connection_release), driver_(std::move(d))
        {
        }

        template <class T, class F>
        Operation<T> begin(F&& call, std::stop_token stop, int stream = 0) const
        {
            cd_operation_t* op = nullptr;
            detail::checked(call(handle_.get(), &op));
            return Operation<T>(op, stop, stream);
        }

        template <class T, class F> T complete(F&& call, std::stop_token stop, int stream = 0) const
        {
            cd_cancel_source_t* cancellation = nullptr;

            if (stop.stop_possible())
                detail::checked(cd_cancel_source_create(&cancellation));

            std::unique_ptr<cd_cancel_source_t, decltype(&cd_cancel_source_release)> source(
                cancellation, cd_cancel_source_release);
            std::optional<std::stop_callback<std::function<void()>>> callback;

            if (cancellation)
                callback.emplace(stop,
                                 [cancellation]
                                 {
                                     cd_cancel_source_request(cancellation);
                                 });

            cd_result_t* h = nullptr;
            auto status = call(handle_.get(), cancellation, &h);
            detail::ResultHandle owner(h);
            detail::checked(status, h);

            if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, Bytes>)
                return detail::response<T>(h, stream);
            else
                return detail::convert<T>(h);
        }

      public:
        State state() const
        {
            return static_cast<State>(cd_connection_get_state(handle_.get()));
        }

        cd_capabilities_t capabilities() const
        {
            return cd_connection_get_capabilities(handle_.get());
        }

        Operation<void> open_async(const CallOptions& options = {}, std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<void>(
                [&](auto* c, auto** op)
                {
                    return cd_open_async(c, &o, nullptr, nullptr, op);
                },
                stop);
        }

        void open(const CallOptions& options = {}, std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<void>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_open(c, &o, result);
                },
                stop);
        }

        Operation<void> close_async(const CallOptions& options = {},
                                    std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<void>(
                [&](auto* c, auto** op)
                {
                    return cd_close_async(c, &o, nullptr, nullptr, op);
                },
                stop);
        }

        void close(const CallOptions& options = {}, std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<void>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_close(c, &o, result);
                },
                stop);
        }

        Operation<void> close_input_async(const CallOptions& options = {},
                                          std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<void>(
                [&](auto* c, auto** op)
                {
                    return cd_close_input_async(c, &o, nullptr, nullptr, op);
                },
                stop);
        }

        void close_input(const CallOptions& options = {}, std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<void>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_close_input(c, &o, result);
                },
                stop);
        }

        Operation<Bytes> read_bytes_async(const CallOptions& options = {},
                                          std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<Bytes>(
                [&](auto* c, auto** op)
                {
                    return cd_read_bytes_async(c, &o, nullptr, nullptr, op);
                },
                stop, static_cast<int>(options.read_stream));
        }

        Bytes read_bytes(const CallOptions& options = {}, std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<Bytes>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_read_bytes(c, &o, result);
                },
                stop, static_cast<int>(options.read_stream));
        }

        Operation<std::string> read_string_async(const CallOptions& options = {},
                                                 std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<std::string>(
                [&](auto* c, auto** op)
                {
                    return cd_read_string_async(c, &o, nullptr, nullptr, op);
                },
                stop, static_cast<int>(options.read_stream));
        }

        std::string read_string(const CallOptions& options = {}, std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<std::string>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_read_string(c, &o, result);
                },
                stop, static_cast<int>(options.read_stream));
        }

        Operation<std::string> read_line_async(const CallOptions& options = {},
                                               std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<std::string>(
                [&](auto* c, auto** op)
                {
                    return cd_read_line_async(c, &o, nullptr, nullptr, op);
                },
                stop, static_cast<int>(options.read_stream));
        }

        std::string read_line(const CallOptions& options = {}, std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<std::string>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_read_line(c, &o, result);
                },
                stop, static_cast<int>(options.read_stream));
        }

        Operation<Bytes> read_exactly_bytes_async(uint64_t count, const CallOptions& options = {},
                                                  std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<Bytes>(
                [&](auto* c, auto** op)
                {
                    return cd_read_exactly_bytes_async(c, count, &o, nullptr, nullptr, op);
                },
                stop, static_cast<int>(options.read_stream));
        }

        Bytes read_exactly_bytes(uint64_t count, const CallOptions& options = {},
                                 std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<Bytes>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_read_exactly_bytes(c, count, &o, result);
                },
                stop, static_cast<int>(options.read_stream));
        }

        Operation<std::string> read_exactly_string_async(uint64_t count,
                                                         const CallOptions& options = {},
                                                         std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<std::string>(
                [&](auto* c, auto** op)
                {
                    return cd_read_exactly_string_async(c, count, &o, nullptr, nullptr, op);
                },
                stop, static_cast<int>(options.read_stream));
        }

        std::string read_exactly_string(uint64_t count, const CallOptions& options = {},
                                        std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<std::string>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_read_exactly_string(c, count, &o, result);
                },
                stop, static_cast<int>(options.read_stream));
        }

        Operation<Bytes> read_until_bytes_async(const ReceiveUntil& until,
                                                const CallOptions& options = {},
                                                std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto u = until.native();
            return begin<Bytes>(
                [&](auto* c, auto** op)
                {
                    return cd_read_until_bytes_async(c, &u, &o, nullptr, nullptr, op);
                },
                stop, static_cast<int>(options.read_stream));
        }

        Bytes read_until_bytes(const ReceiveUntil& until, const CallOptions& options = {},
                               std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto u = until.native();
            return complete<Bytes>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_read_until_bytes(c, &u, &o, result);
                },
                stop, static_cast<int>(options.read_stream));
        }

        Operation<std::string> read_until_string_async(const ReceiveUntil& until,
                                                       const CallOptions& options = {},
                                                       std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto u = until.native();
            return begin<std::string>(
                [&](auto* c, auto** op)
                {
                    return cd_read_until_string_async(c, &u, &o, nullptr, nullptr, op);
                },
                stop, static_cast<int>(options.read_stream));
        }

        std::string read_until_string(const ReceiveUntil& until, const CallOptions& options = {},
                                      std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto u = until.native();
            return complete<std::string>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_read_until_string(c, &u, &o, result);
                },
                stop, static_cast<int>(options.read_stream));
        }

        Operation<void> write_bytes_async(std::span<const std::byte> input,
                                          const CallOptions& options = {},
                                          std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<void>(
                [&](auto* c, auto** op)
                {
                    return cd_write_bytes_async(c, detail::view(input), &o, nullptr, nullptr, op);
                },
                stop);
        }

        void write_bytes(std::span<const std::byte> input, const CallOptions& options = {},
                         std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<void>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_write_bytes(c, detail::view(input), &o, result);
                },
                stop);
        }

        Operation<void> write_string_async(std::string_view input, const CallOptions& options = {},
                                           std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<void>(
                [&](auto* c, auto** op)
                {
                    return cd_write_string_async(c, detail::view(input), &o, nullptr, nullptr, op);
                },
                stop);
        }

        void write_string(std::string_view input, const CallOptions& options = {},
                          std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<void>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_write_string(c, detail::view(input), &o, result);
                },
                stop);
        }

        Operation<void> write_line_async(std::string_view input, const CallOptions& options = {},
                                         std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<void>(
                [&](auto* c, auto** op)
                {
                    return cd_write_line_async(c, detail::view(input), &o, nullptr, nullptr, op);
                },
                stop);
        }

        void write_line(std::string_view input, const CallOptions& options = {},
                        std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<void>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_write_line(c, detail::view(input), &o, result);
                },
                stop);
        }

        Operation<ExecutionResult<std::string>>
        execute_async(std::string_view command, const std::optional<Check>& check = {},
                      const CallOptions& options = {}, std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto ch = check ? check->native() : cd_check_t{};
            return begin<ExecutionResult<std::string>>(
                [&](auto* c, auto** op)
                {
                    return cd_execute_string_async(c, detail::view(command), check ? &ch : nullptr,
                                                   &o, nullptr, nullptr, op);
                },
                stop);
        }

        ExecutionResult<std::string> execute(std::string_view command,
                                             const std::optional<Check>& check = {},
                                             const CallOptions& options = {},
                                             std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto ch = check ? check->native() : cd_check_t{};
            return complete<ExecutionResult<std::string>>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_execute_string(c, detail::view(command), check ? &ch : nullptr, &o,
                                             result);
                },
                stop);
        }

        Operation<ExecutionResult<std::string>> execute_until_async(std::string_view command,
                                                                    const Check& match,
                                                                    const CallOptions& options = {},
                                                                    std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto ch = match.native();
            return begin<ExecutionResult<std::string>>(
                [&](auto* c, auto** op)
                {
                    return cd_execute_until_string_async(c, detail::view(command), &ch, &o, nullptr,
                                                         nullptr, op);
                },
                stop);
        }

        ExecutionResult<std::string> execute_until(std::string_view command, const Check& match,
                                                   const CallOptions& options = {},
                                                   std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto ch = match.native();
            return complete<ExecutionResult<std::string>>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_execute_until_string(c, detail::view(command), &ch, &o, result);
                },
                stop);
        }

        Operation<ExecutionResult<Bytes>> execute_async(std::span<const std::byte> command,
                                                        const std::optional<Check>& check = {},
                                                        const CallOptions& options = {},
                                                        std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto ch = check ? check->native() : cd_check_t{};
            return begin<ExecutionResult<Bytes>>(
                [&](auto* c, auto** op)
                {
                    return cd_execute_bytes_async(c, detail::view(command), check ? &ch : nullptr,
                                                  &o, nullptr, nullptr, op);
                },
                stop);
        }

        ExecutionResult<Bytes> execute(std::span<const std::byte> command,
                                       const std::optional<Check>& check = {},
                                       const CallOptions& options = {},
                                       std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto ch = check ? check->native() : cd_check_t{};
            return complete<ExecutionResult<Bytes>>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_execute_bytes(c, detail::view(command), check ? &ch : nullptr, &o,
                                            result);
                },
                stop);
        }

        Operation<ExecutionResult<Bytes>> execute_until_async(std::span<const std::byte> command,
                                                              const Check& match,
                                                              const CallOptions& options = {},
                                                              std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto ch = match.native();
            return begin<ExecutionResult<Bytes>>(
                [&](auto* c, auto** op)
                {
                    return cd_execute_until_bytes_async(c, detail::view(command), &ch, &o, nullptr,
                                                        nullptr, op);
                },
                stop);
        }

        ExecutionResult<Bytes> execute_until(std::span<const std::byte> command, const Check& match,
                                             const CallOptions& options = {},
                                             std::stop_token stop = {}) const
        {
            auto o = options.native();
            auto ch = match.native();
            return complete<ExecutionResult<Bytes>>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_execute_until_bytes(c, detail::view(command), &ch, &o, result);
                },
                stop);
        }

        Operation<TransferResult> upload_async(std::string_view source,
                                               std::string_view destination,
                                               const TransferOptions& options = {},
                                               std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<TransferResult>(
                [&](auto* c, auto** op)
                {
                    return cd_upload_async(c, detail::view(source), detail::view(destination), &o,
                                           nullptr, nullptr, op);
                },
                stop);
        }

        TransferResult upload(std::string_view source, std::string_view destination,
                              const TransferOptions& options = {}, std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<TransferResult>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_upload(c, detail::view(source), detail::view(destination), &o,
                                     result);
                },
                stop);
        }

        Operation<TransferResult> download_async(std::string_view source,
                                                 std::string_view destination,
                                                 const TransferOptions& options = {},
                                                 std::stop_token stop = {}) const
        {
            auto o = options.native();
            return begin<TransferResult>(
                [&](auto* c, auto** op)
                {
                    return cd_download_async(c, detail::view(source), detail::view(destination), &o,
                                             nullptr, nullptr, op);
                },
                stop);
        }

        TransferResult download(std::string_view source, std::string_view destination,
                                const TransferOptions& options = {},
                                std::stop_token stop = {}) const
        {
            auto o = options.native();
            return complete<TransferResult>(
                [&](auto* c, auto* cancellation, auto** result)
                {
                    o.cancellation = cancellation;
                    return cd_download(c, detail::view(source), detail::view(destination), &o,
                                       result);
                },
                stop);
        }
    };
}
