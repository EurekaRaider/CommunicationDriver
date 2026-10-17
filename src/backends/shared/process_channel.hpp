#pragma once
#include "core/backend.hpp"
#include "backends/shared/process_config.hpp"
#include "backends/shared/shell_session.hpp"
#include "platform/process.hpp"

namespace cd
{
    struct ProcessChannel : Backend
    {
        BackendConfig<ProcessConfig> config;
        ShellSession shell;
        asio::writable_pipe input;
        asio::readable_pipe out, err;
        std::shared_ptr<process::process> child;
        std::array<char, 8192> out_buffer{}, err_buffer{};
        bool stopped = false;
        cd_status_t missing_executable = CD_CONNECTION_FAILED;
        ProcessChannel(std::shared_ptr<Runtime> r, BackendConfig<ProcessConfig> c);

        void launch(const std::string& command, bool one, Completion cb);

        void open(Completion cb) override;

        bool command_channels() const override;

        void command(std::string command, Completion cb) override;

        void pump(int stream);

        void write(std::string data, Completion cb) override;

        void close_input(Completion cb) override;

        void close() override;
    };
}
