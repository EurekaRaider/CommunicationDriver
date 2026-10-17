#include "c_api/common.hpp"
#include "core/batch.hpp"
#include <set>

using namespace cd;
using namespace cd::abi;

extern "C"
{
    cd_status_t CD_CALL cd_batch_run_async(cd_driver_t* d, const cd_batch_item_t* input,
                                           uint64_t count, const cd_batch_options_t* options,
                                           cd_completion_fn cb, void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            require(d && (input || count == 0) && count <= SIZE_MAX, "Invalid batch input");

            auto batch = std::make_shared<Batch>();
            batch->runtime = d->value;
            batch->operation = std::make_shared<Operation>(d->value);
            batch->result->kind = CD_RESULT_BATCH;

            std::shared_ptr<Cancellation> cancel;

            if (options)
            {
                require(options->struct_size >= sizeof(*options) && options->max_concurrency > 0,
                        "Invalid BatchOptions");
                batch->max = options->max_concurrency;
                cancel = cancellation(options->cancellation);
            }

            std::set<std::string> keys;

            for (uint64_t i = 0; i < count; ++i)
            {
                auto& item = input[i];
                require(item.struct_size >= sizeof(item) && item.connection, "Invalid batch item");

                auto key = copy(item.key);
                require(!key.empty() && keys.insert(key).second,
                        "Batch keys must be nonempty and unique");
                require(item.connection->value->runtime == d->value,
                        "Connections must belong to the same Driver");
                require(item.connection->value->state == CD_STATE_OPEN,
                        "Batch connections must already be open");
                require(item.kind >= CD_BATCH_EXECUTE_STRING && item.kind <= CD_BATCH_DOWNLOAD,
                        "Invalid batch action");

                bool text = item.kind == CD_BATCH_EXECUTE_STRING ||
                            item.kind == CD_BATCH_EXECUTE_UNTIL_STRING;
                Action action = item.kind == CD_BATCH_UPLOAD     ? Action::Upload
                                : item.kind == CD_BATCH_DOWNLOAD ? Action::Download
                                : item.kind == CD_BATCH_EXECUTE_UNTIL_STRING ||
                                        item.kind == CD_BATCH_EXECUTE_UNTIL_BYTES
                                    ? Action::ExecuteUntil
                                    : Action::Execute;
                auto req = request(item.connection, action, text, item.options);
                req->check = Check(item.check);

                if (action == Action::ExecuteUntil)
                    require(req->check.kind == CD_CHECK_CONTAINS ||
                                req->check.kind == CD_CHECK_REGEX,
                            "ExecuteUntil requires Contains or Regex");

                if (action == Action::Upload || action == Action::Download)
                {
                    req->source = copy(item.source);
                    req->destination = copy(item.destination);
                    text_argument(req->source, "Source required");
                    text_argument(req->destination, "Destination required");

                    if (auto o = item.transfer_options)
                    {
                        require(o->struct_size >= sizeof(*o), "Invalid TransferOptions");
                        req->overwrite = o->overwrite != 0;

                        if (o->timeout_ms)
                            req->options.timeout = o->timeout_ms;

                        req->options.cancellation = cancellation(o->cancellation);
                    }
                }
                else
                    req->input =
                        text ? encode(copy(item.command), req->options.encoding) : copy(item.bytes);

                require(req->options.timeout <= max_timeout_ms,
                        "Operation timeout is out of range");
                batch->items.push_back({item.connection->value, req, key});
                batch->result->items.push_back({key, CD_BATCH_NOT_STARTED, {}});
            }

            batch->admitted.resize(static_cast<size_t>(count));
            accept(batch->operation, cb, user, out);

            auto weak = std::weak_ptr<Batch>(batch);
            {
                std::lock_guard lock(batch->operation->mutex);
                batch->operation->cancel_hook = [weak]
                {
                    if (auto b = weak.lock())
                        asio::post(b->runtime->state->io,
                                   [b]
                                   {
                                       b->schedule();
                                   });
                };
            }

            if (cancel)
                cancel->subscribe(batch->operation);

            asio::post(d->value->state->io,
                       [batch]
                       {
                           batch->schedule();
                       });

            return CD_OK;
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_batch_run(cd_driver_t* d, const cd_batch_item_t* items, uint64_t count,
                                     const cd_batch_options_t* options, cd_result_t** out)
    {
        try
        {
            require(d && out, "Driver and result required");

            *out = nullptr;
            if (current_io == d->value->state.get())
                throw Fault(CD_INVALID_CONTEXT, "Use Batch.RunAsync in completion context");

            cd_operation_t* op = nullptr;
            auto s = cd_batch_run_async(d, items, count, options, nullptr, nullptr, &op);

            if (s != CD_OK)
                return s;

            s = cd_operation_wait(op, CD_INFINITE, out);
            release(op);

            return s == CD_OK ? cd_result_get_status(*out) : s;
        }
        catch (...)
        {
            return exception_result(out);
        }
    }
}
