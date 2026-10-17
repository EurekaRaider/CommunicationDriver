#include "core/cancellation.hpp"
#include "core/operation.hpp"
#include <algorithm>

namespace cd
{
    void Cancellation::request()
    {
        if (requested.exchange(true))
            return;

        std::vector<std::weak_ptr<Operation>> targets;
        {
            std::lock_guard lock(mutex);
            targets.swap(listeners);
        }

        for (auto& weak : targets)
            if (auto op = weak.lock())
                op->cancel();
    }

    // Amortize cleanup across insertions while retaining only weak ownership.
    void Cancellation::subscribe(std::weak_ptr<Operation> weak)
    {
        {
            std::lock_guard lock(mutex);

            if (!requested)
            {
                if (listeners.size() >= cleanup_at)
                {
                    std::erase_if(listeners,
                                  [](auto& entry)
                                  {
                                      auto op = entry.lock();
                                      return !op || op->completed();
                                  });
                    cleanup_at = std::max<size_t>(64, listeners.size() * 2);
                }

                listeners.push_back(std::move(weak));

                return;
            }
        }

        if (auto op = weak.lock())
            op->cancel();
    }
}
