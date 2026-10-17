#include "c_api/common.hpp"

using namespace cd;
using namespace cd::abi;

extern "C"
{
    int32_t CD_CALL cd_operation_is_completed(cd_operation_t* o)
    {
        return o && o->value->completed();
    }

    cd_status_t CD_CALL cd_operation_wait(cd_operation_t* o, uint64_t timeout, cd_result_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            require(o && out, "Operation and result required");
            require(timeout == CD_INFINITE || timeout <= max_timeout_ms,
                    "Observation timeout is out of range");

            auto op = o->value;

            if (current_io == op->runtime->state.get())
                return CD_INVALID_CONTEXT;

            std::unique_lock lock(op->mutex);

            if (timeout == CD_INFINITE)
                op->cv.wait(lock,
                            [&]
                            {
                                return bool(op->result);
                            });
            else if (!op->cv.wait_for(lock, std::chrono::milliseconds(timeout),
                                      [&]
                                      {
                                          return bool(op->result);
                                      }))
                return CD_TIMEOUT;

            *out = new cd_result(op->result);
            return CD_OK;
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    void CD_CALL cd_operation_cancel(cd_operation_t* o)
    {
        if (o)
            o->value->cancel();
    }

    cd_status_t CD_CALL cd_cancel_source_create(cd_cancel_source_t** out)
    {
        try
        {
            require(out, "Output required");
            *out = new cd_cancel_source(std::make_shared<Cancellation>());
            return CD_OK;
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    void CD_CALL cd_cancel_source_request(cd_cancel_source_t* c)
    {
        if (c)
            c->value->request();
    }

    int32_t CD_CALL cd_cancel_source_is_requested(cd_cancel_source_t* c)
    {
        return c && c->value->requested;
    }
}
