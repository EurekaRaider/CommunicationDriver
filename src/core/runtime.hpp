#pragma once
#include "core/common.hpp"

namespace cd
{
    struct IoState
    {
        asio::io_context io;
        asio::thread_pool files{2};
        size_t file_jobs = 0;
        std::vector<std::function<void()>> file_waiters;
    };

    extern thread_local IoState* current_io;
    struct Connection;

    struct Runtime
    {
        std::shared_ptr<IoState> state = std::make_shared<IoState>();
        asio::executor_work_guard<asio::io_context::executor_type> guard =
            asio::make_work_guard(state->io);
        std::thread worker;
        std::mutex registry_mutex;
        std::vector<std::weak_ptr<Connection>> connections;
        size_t cleanup_at = 64;
        std::atomic<bool> closed{false};
        Runtime();
        ~Runtime();
        void register_connection(const std::shared_ptr<Connection>&);
        void close(std::function<void()>);
        void file_job(std::function<void()> work, std::function<void(std::exception_ptr)> done);
        void after_files(std::function<void()> done);
    };
}
