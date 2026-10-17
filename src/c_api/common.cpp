#include "c_api/common.hpp"

namespace cd::abi
{
    cd_status_t exception_result(cd_result_t** out) noexcept
    {
        std::shared_ptr<Result> r;

        try
        {
            throw;
        }
        catch (const Fault& f)
        {
            r = error_result(f);
        }
        catch (const std::exception& e)
        {
            r = error_result(Fault(CD_INTERNAL_ERROR, e.what()));
        }
        catch (...)
        {
            r = error_result(Fault(CD_INTERNAL_ERROR, "Unknown native exception"));
        }

        auto status = r->status;

        if (out)
        {
            try
            {
                *out = new cd_result(std::move(r));
            }
            catch (...)
            {
                *out = nullptr;
            }
        }

        return status;
    }

    cd_status_t accept(std::shared_ptr<Operation> op, cd_completion_fn cb, void* user,
                       cd_operation_t** out)
    {
        require(out, "Operation output required");

        *out = nullptr;
        auto handle = new cd_operation(op);
        retain(handle);
        op->callback = [handle, cb, user](std::shared_ptr<Result> result)
        {
            cd_result* r = nullptr;

            try
            {
                r = new cd_result(std::move(result));
                if (cb)
                    cb(handle, r, user);
            }
            catch (...)
            { /* Foreign callbacks must not unwind into the event loop. */
            }

            release(r);
            release(handle);
        };

        *out = handle;
        return CD_OK;
    }

    std::shared_ptr<Request> request(cd_connection_t* c, Action action, bool text,
                                     const cd_call_options_t* options)
    {
        require(c, "Connection required");

        auto& defaults = c->value->config.defaults;
        uint64_t timeout = action == Action::Open    ? defaults.connect_ms
                           : action == Action::Close ? defaults.close_ms
                           : (action == Action::Execute || action == Action::ExecuteUntil ||
                              action == Action::Upload || action == Action::Download)
                               ? defaults.execution_ms
                               : defaults.io_ms;
        return std::make_shared<Request>(action, text, Options(defaults, options, timeout));
    }

    cd_status_t submit(cd_connection_t* c, std::shared_ptr<Request> r, cd_completion_fn cb,
                       void* user, cd_operation_t** out)
    {
        require(r->options.timeout <= max_timeout_ms, "Operation timeout is out of range");

        auto op = std::make_shared<Operation>(c->value->runtime);
        r->operation = op;
        accept(op, cb, user, out);
        c->value->submit(std::move(r));

        return CD_OK;
    }
}
