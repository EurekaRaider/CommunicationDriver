#pragma once
#include "core/runtime.hpp"

namespace cd
{
    struct Backend : std::enable_shared_from_this<Backend>
    {
        std::shared_ptr<Runtime> runtime;
        std::function<void(int, std::string, bool)> output;
        std::function<void(cd_status_t, std::string, int64_t)> failure;
        std::function<void(std::optional<int>)> exited;
        std::function<void(int, std::string, bool)> command_output;
        std::function<void(std::optional<int>)> command_exited;

        explicit Backend(std::shared_ptr<Runtime> r) : runtime(std::move(r))
        {
        }

        std::string transfer_partial_file;

        virtual ~Backend() = default;
        virtual void open(Completion) = 0;
        virtual void write(std::string, Completion) = 0;
        virtual void close() = 0;
        virtual void close_input(Completion) = 0;

        virtual void command(std::string, Completion cb)
        {
            cb(CD_NOT_SUPPORTED, "Command channels unavailable", 0);
        }

        virtual void transfer(bool, std::string, std::string, bool, std::function<void(uint64_t)>,
                              Completion cb)
        {
            cb(CD_NOT_SUPPORTED, "File transfer unavailable", 0);
        }

        virtual bool command_channels() const
        {
            return false;
        }

        virtual void ensure_stdio(Completion cb)
        {
            cb(CD_OK, {}, 0);
        }

        virtual std::string frame_command(const std::string& command, const std::string&)
        {
            return command;
        }
    };
}
