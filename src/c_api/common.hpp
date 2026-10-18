#pragma once
#include "c_api/handles.hpp"
#include <cstring>

namespace cd::abi
{
    template <class T>
    void retain(T* h)
    {
        if (h)
            h->refs.fetch_add(1, std::memory_order_relaxed);
    }

    template <class T>
    void release(T* h)
    {
        if (h && h->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete h;
    }

    inline void require(bool c, const char* m)
    {
        if (!c)
            throw Fault(CD_INVALID_ARGUMENT, m);
    }

    inline void text_argument(const std::string& s, const char* message, bool nonempty = true)
    {
        require((!nonempty || !s.empty()) && s.find('\0') == std::string::npos, message);
        decode(s, CD_ENCODING_UTF8);
    }

    cd_status_t exception_result(cd_result_t**) noexcept;
    cd_status_t accept(std::shared_ptr<Operation>, cd_completion_fn, void*, cd_operation_t**);
    std::shared_ptr<Request> request(cd_connection_t*, Action, bool, const cd_call_options_t*);
    cd_status_t submit(
        cd_connection_t*, std::shared_ptr<Request>, cd_completion_fn, void*, cd_operation_t**);

    template <class F>
    cd_status_t synchronous(cd_connection_t* c, cd_result_t** result, F&& start)
    {
        if (result)

            *result = nullptr;
        if (c && current_io == c->value->runtime->state.get())
        {
            if (result)
                *result = new cd_result(error_result(Fault(
                    CD_INVALID_CONTEXT, "Synchronous wait inside completion context; use Async")));
            return CD_INVALID_CONTEXT;
        }

        require(result, "Result output required");

        cd_operation_t* op = nullptr;
        auto status = start(&op);

        if (status != CD_OK)
        {
            *result = new cd_result(error_result(Fault(status, cd_status_name(status))));
            return status;
        }

        status = cd_operation_wait(op, CD_INFINITE, result);
        release(op);

        return status == CD_OK ? cd_result_get_status(*result) : status;
    }
}
