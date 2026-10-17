#include "core/batch.hpp"

namespace cd
{
    void Batch::schedule()
    {
        if (operation->completed())
            return;

        if (operation->cancelled && !result->was_cancelled)
        {
            result->was_cancelled = true;

            while (next_index < items.size())
            {
                result->items[next_index].state = CD_BATCH_NOT_STARTED;
                ++next_index;
                ++done;
            }

            for (auto& op : admitted)
                if (op && !op->completed())
                    op->cancel();
        }

        while (!operation->cancelled && running < max && next_index < items.size())
        {
            size_t index = next_index++;
            auto& item = items[index];
            auto req = item.request;
            req->started = Clock::now();
            req->operation = std::make_shared<Operation>(runtime);
            admitted[index] = req->operation;

            auto self = shared_from_this();
            req->operation->callback = [self, index](std::shared_ptr<Result> r)
            {
                auto& entry = self->result->items[index];
                entry.result = std::move(r);
                entry.state = entry.result->status == CD_OK          ? CD_BATCH_COMPLETED
                              : entry.result->status == CD_TIMEOUT   ? CD_BATCH_TIMED_OUT
                              : entry.result->status == CD_CANCELLED ? CD_BATCH_CANCELLED
                                                                     : CD_BATCH_FAILED;
                --self->running;
                ++self->done;
                self->schedule();
            };
            ++running;
            item.connection->submit(req);
        }

        if (done == items.size())
        {
            result->elapsed_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
            operation->complete(result);
        }
    }
}
