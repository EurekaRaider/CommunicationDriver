#include "core/operation.hpp"

namespace cd
{
    void Operation::complete(std::shared_ptr<Result> r)
    {
        std::function<void(std::shared_ptr<Result>)> cb;
        {
            std::lock_guard lock(mutex);

            if (result)
                return;

            result = r;
            cb = std::move(callback);
            cancel_hook = {};
        }
        cv.notify_all();

        if (cb)
            cb(std::move(r));
    }

    bool Operation::completed()
    {
        std::lock_guard lock(mutex);
        return bool(result);
    }

    void Operation::cancel()
    {
        if (cancelled.exchange(true))
            return;

        std::function<void()> f;
        {
            std::lock_guard lock(mutex);
            f = cancel_hook;
        }

        if (f)
            f();
    }

    std::shared_ptr<Result> error_result(const Fault& f)
    {
        auto r = std::make_shared<Result>();
        r->status = f.code;
        r->message = f.message;
        r->native = f.native;

        return r;
    }
}
