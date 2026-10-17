#pragma once
#include "core/config.hpp"
#include "core/request.hpp"

namespace cd
{
    struct Connection : std::enable_shared_from_this<Connection>
    {
        std::shared_ptr<Runtime> runtime;
        ConnectionConfig config;
        cd_capabilities_t capabilities;
        std::atomic<int> state{CD_STATE_CREATED};
        std::shared_ptr<Backend> persistent, one_shot;
        std::list<std::shared_ptr<Request>> queue;
        std::shared_ptr<Request> active;
        size_t closing_count = 0;
        std::string pending[2];
        std::deque<size_t> messages[2];
        bool eof[2] = {false, false};
        std::string stdio_pending[2];
        bool stdio_eof[2] = {false, false}, isolating = false;
        std::optional<int> process_exit;
        bool process_finished = false;
        uint64_t generation = 0;
        Connection(std::shared_ptr<Runtime>, ConnectionConfig);
        ~Connection();
        void submit(std::shared_ptr<Request>);
        void next();
        void start();
        void attach(const std::shared_ptr<Backend>&, bool isolated = false);
        void receive(int, std::string, bool, bool);
        void process_data();
        void finish(cd_status_t = CD_OK, std::string = {}, int64_t = 0, bool advance = true);
        void abort(const std::shared_ptr<Request>&, cd_status_t, std::string);
        void force_close();
        void ensure_persistent(Completion);
        void finish_close(const std::shared_ptr<Request>&, cd_status_t = CD_OK, std::string = {});
        bool boundary(std::string&, Request&, size_t&, size_t&);
        void consume_pending(int stream, size_t count);
    };
}
