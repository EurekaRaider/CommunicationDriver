#include "core/runtime.hpp"
#include "core/connection.hpp"
#include <algorithm>

namespace cd
{
    thread_local IoState* current_io = nullptr;

    Runtime::Runtime()
    {
        auto s = state;
        worker = std::thread(
            [s]
            {
                current_io = s.get();
                s->io.run();
                current_io = nullptr;
            });
    }

    Runtime::~Runtime()
    {
        guard.reset();
        if (worker.joinable())
        {
            if (worker.get_id() == std::this_thread::get_id())
                worker.detach();
            else
                worker.join();
        }
    }

    void Runtime::register_connection(const std::shared_ptr<Connection>& c)
    {
        std::lock_guard lock(registry_mutex);

        if (closed)
            throw Fault(CD_CLOSED, "Driver is closed");

        if (connections.size() >= cleanup_at)
        {
            std::erase_if(connections,
                          [](const auto& connection)
                          {
                              return connection.expired();
                          });
            cleanup_at = std::max<size_t>(64, connections.size() * 2);
        }

        connections.push_back(c);
    }

    void Runtime::close(std::function<void()> callback)
    {
        closed = true;

        std::vector<std::shared_ptr<Connection>> list;
        {
            std::lock_guard lock(registry_mutex);
            for (auto& w : connections)
                if (auto c = w.lock())
                    list.push_back(std::move(c));
        }

        auto self = state;
        asio::post(state->io,
                   [self, list = std::move(list), cb = std::move(callback)]() mutable
                   {
                       for (auto& c : list)
                           c->force_close();
                       if (self->file_jobs)
                           self->file_waiters.push_back(std::move(cb));
                       else
                           asio::post(self->io, std::move(cb));
                   });
    }

    void Runtime::file_job(std::function<void()> work, std::function<void(std::exception_ptr)> done)
    {
        auto s = state;
        ++s->file_jobs;
        asio::post(s->files,
                   [s, work = std::move(work), done = std::move(done)]() mutable
                   {
                       std::exception_ptr error;

                       try
                       {
                           work();
                       }
                       catch (...)
                       {
                           error = std::current_exception();
                       }

                       work = {};
                       asio::post(s->io,
                                  [s, error, done = std::move(done)]() mutable
                                  {
                                      done(error);
                                      if (--s->file_jobs == 0)
                                      {
                                          auto waiters = std::exchange(s->file_waiters, {});
                                          for (auto& cb : waiters)
                                              asio::post(s->io, std::move(cb));
                                      }
                                  });
                   });
    }

    void Runtime::after_files(std::function<void()> done)
    {
        if (state->file_jobs)
            state->file_waiters.push_back(std::move(done));
        else
            asio::post(state->io, std::move(done));
    }
}
