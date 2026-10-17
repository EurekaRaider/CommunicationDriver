#pragma once
#include "core/connection.hpp"

namespace cd
{
    struct Batch : std::enable_shared_from_this<Batch>
    {
        struct Item
        {
            std::shared_ptr<Connection> connection;
            std::shared_ptr<Request> request;
            std::string key;
        };

        std::shared_ptr<Runtime> runtime;
        std::shared_ptr<Operation> operation;
        std::shared_ptr<Result> result = std::make_shared<Result>();
        std::vector<Item> items;
        size_t next_index = 0, running = 0, done = 0, max = 32;
        Clock::time_point start = Clock::now();
        std::vector<std::shared_ptr<Operation>> admitted;

        void schedule();
    };
}
