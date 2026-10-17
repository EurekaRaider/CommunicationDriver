#pragma once
#include "core/runtime.hpp"
#include "core/result.hpp"
#include "core/cancellation.hpp"

namespace cd
{
    struct Operation : std::enable_shared_from_this<Operation>
    {
        std::shared_ptr<Runtime> runtime;
        std::mutex mutex;
        std::condition_variable cv;
        std::shared_ptr<Result> result;
        std::atomic<bool> cancelled{false};
        std::function<void(std::shared_ptr<Result>)> callback;
        std::function<void()> cancel_hook;

        explicit Operation(std::shared_ptr<Runtime> r) : runtime(std::move(r))
        {
        }

        void complete(std::shared_ptr<Result>);
        void cancel();
        bool completed();
    };
}
