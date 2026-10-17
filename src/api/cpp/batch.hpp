#pragma once
#include "response_checker.hpp"

namespace comm
{
    struct BatchOptions
    {
        uint32_t max_concurrency = 32;
    };

    struct BatchItemResult
    {
        std::string key;
        BatchItemState state;
        bool succeeded = false;
        std::optional<ExecutionResult<std::string>> text_execution;
        std::optional<ExecutionResult<Bytes>> byte_execution;
        std::optional<TransferResult> transfer;
        std::optional<ErrorInfo> error;
        std::chrono::nanoseconds elapsed{};
    };

    struct BatchResult
    {
        std::vector<BatchItemResult> items;
        bool all_succeeded = false, was_cancelled = false;
        std::chrono::nanoseconds elapsed{};
    };

    struct BatchItem
    {
        std::string key;
        Connection connection;
        int kind;
        std::string command, source, destination;
        Bytes byte_command;
        std::optional<Check> check;
        CallOptions options;
        TransferOptions transfer_options;

        static BatchItem execute(std::string key, Connection c, std::string_view command,
                                 std::optional<Check> check = {}, CallOptions options = {})
        {
            return {std::move(key),
                    std::move(c),
                    CD_BATCH_EXECUTE_STRING,
                    std::string(command),
                    {},
                    {},
                    {},
                    std::move(check),
                    std::move(options),
                    {}};
        }

        static BatchItem execute(std::string key, Connection c, std::span<const std::byte> command,
                                 std::optional<Check> check = {}, CallOptions options = {})
        {
            return {std::move(key),
                    std::move(c),
                    CD_BATCH_EXECUTE_BYTES,
                    {},
                    {},
                    {},
                    {command.begin(), command.end()},
                    std::move(check),
                    std::move(options),
                    {}};
        }

        static BatchItem execute_until(std::string key, Connection c, std::string_view command,
                                       Check match, CallOptions options = {})
        {
            auto item = execute(std::move(key), std::move(c), command, std::move(match),
                                std::move(options));
            item.kind = CD_BATCH_EXECUTE_UNTIL_STRING;
            return item;
        }

        static BatchItem execute_until(std::string key, Connection c,
                                       std::span<const std::byte> command, Check match,
                                       CallOptions options = {})
        {
            auto item = execute(std::move(key), std::move(c), command, std::move(match),
                                std::move(options));
            item.kind = CD_BATCH_EXECUTE_UNTIL_BYTES;
            return item;
        }

        static BatchItem upload(std::string key, Connection c, std::string source,
                                std::string destination, TransferOptions options = {})
        {
            return {std::move(key),
                    std::move(c),
                    CD_BATCH_UPLOAD,
                    {},
                    std::move(source),
                    std::move(destination),
                    {},
                    {},
                    {},
                    std::move(options)};
        }

        static BatchItem download(std::string key, Connection c, std::string source,
                                  std::string destination, TransferOptions options = {})
        {
            auto item = upload(std::move(key), std::move(c), std::move(source),
                               std::move(destination), std::move(options));
            item.kind = CD_BATCH_DOWNLOAD;
            return item;
        }
    };

    namespace detail
    {
        template <> inline BatchResult convert<BatchResult>(cd_result_t* h)
        {
            checked(cd_result_get_status(h), h);

            auto info = sized<cd_batch_info_t>();
            checked(cd_result_get_batch_info(h, &info));

            BatchResult result;
            result.all_succeeded = info.all_succeeded;
            result.was_cancelled = info.was_cancelled;
            result.elapsed = std::chrono::nanoseconds(info.elapsed_ns);

            for (uint64_t index = 0; index < info.item_count; ++index)
            {
                auto i = sized<cd_batch_item_result_t>();
                checked(cd_result_get_batch_item(h, index, &i));

                BatchItemResult item;
                item.key = string(i.key);
                item.state = static_cast<BatchItemState>(i.state);
                item.succeeded = i.succeeded;
                item.elapsed = std::chrono::nanoseconds(i.elapsed_ns);

                if (i.result)
                {
                    if (cd_result_get_status(i.result) != CD_OK)
                        item.error = error(i.result);
                    else
                    {
                        auto kind = cd_result_get_kind(i.result);
                        if (kind == CD_RESULT_EXECUTION_TEXT)
                            item.text_execution = convert<ExecutionResult<std::string>>(i.result);
                        else if (kind == CD_RESULT_EXECUTION_BYTES)
                            item.byte_execution = convert<ExecutionResult<Bytes>>(i.result);
                        else if (kind == CD_RESULT_TRANSFER)
                            item.transfer = convert<TransferResult>(i.result);
                    }
                }

                result.items.push_back(std::move(item));
            }

            return result;
        }
    }

    class Batch
    {
      public:
        static Operation<BatchResult> run_async(std::span<const BatchItem> items,
                                                const BatchOptions& options = {},
                                                std::stop_token stop = {})
        {
            if (items.empty())
                throw std::invalid_argument("Batch requires at least one item");

            std::vector<cd_batch_item_t> native(items.size());
            std::vector<cd_check_t> checks(items.size());
            std::vector<cd_call_options_t> calls(items.size());
            std::vector<cd_transfer_options_t> transfers(items.size());

            for (size_t n = 0; n < items.size(); ++n)
            {
                auto& i = items[n];
                auto& b = native[n];
                b.struct_size = sizeof(b);
                b.kind = i.kind;
                b.key = detail::view(i.key);
                b.connection = i.connection.handle_.get();
                b.command = detail::view(i.command);
                b.bytes = detail::view(i.byte_command);

                if (i.check)
                {
                    checks[n] = i.check->native();
                    b.check = &checks[n];
                }

                calls[n] = i.options.native();
                b.options = &calls[n];
                b.source = detail::view(i.source);
                b.destination = detail::view(i.destination);
                transfers[n] = i.transfer_options.native();
                b.transfer_options = &transfers[n];
            }

            cd_batch_options_t o;
            cd_batch_options_init(&o);
            o.max_concurrency = options.max_concurrency;

            cd_operation_t* op = nullptr;
            detail::checked(cd_batch_run_async(items[0].connection.driver_.get(), native.data(),
                                               native.size(), &o, nullptr, nullptr, &op));

            return Operation<BatchResult>(op, stop);
        }

        static BatchResult run(std::span<const BatchItem> items, const BatchOptions& options = {},
                               std::stop_token stop = {})
        {
            return run_async(items, options, stop).get();
        }
    };
}
