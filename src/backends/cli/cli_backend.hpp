#pragma once
#include "core/backend.hpp"
#include "backends/cli/cli_config.hpp"
#include "backends/shared/process_channel.hpp"

namespace cd
{
    class CliBackend final : public Backend
    {
        std::shared_ptr<ProcessChannel> process;

      public:
        CliBackend(std::shared_ptr<Runtime> r, BackendConfig<CliConfig> c);

        void bind();

        void open(Completion cb) override;

        void command(std::string command, Completion cb) override;

        void write(std::string data, Completion cb) override;

        void close_input(Completion cb) override;

        void close() override;

        bool command_channels() const override;

        std::string frame_command(const std::string& command, const std::string& marker) override;
    };
}
