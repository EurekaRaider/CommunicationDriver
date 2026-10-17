#pragma once
#include "core/common.hpp"

namespace cd
{
    struct Operation;

    struct Cancellation
    {
        std::atomic<bool> requested{false};
        std::mutex mutex;
        std::vector<std::weak_ptr<Operation>> listeners;
        size_t cleanup_at = 64;
        void request();
        void subscribe(std::weak_ptr<Operation>);
    };

    std::shared_ptr<Cancellation> cancellation(cd_cancel_source_t*);
}
