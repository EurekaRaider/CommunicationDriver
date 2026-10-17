#pragma once
#include "core/backend.hpp"
#include "backends/adb/adb_config.hpp"
#include "backends/shared/process_channel.hpp"

namespace cd
{
    class AdbBackend final : public Backend
    {
        BackendConfig<AdbConfig> config;
        std::shared_ptr<ProcessChannel> process, auxiliary;
        Completion auxiliary_callback;
        std::string auxiliary_output;
        bool auxiliary_eof[2] = {false, false};
        std::optional<int> auxiliary_exit;

      public:
        AdbBackend(std::shared_ptr<Runtime> r, BackendConfig<AdbConfig> c);

        std::vector<std::string> adb_args();
        BackendConfig<ProcessConfig> process_config(std::vector<std::string> args);

        void launch(std::string command, bool one, Completion cb);

        void auxiliary_finish(cd_status_t status, std::string message, int64_t native);

        void auxiliary_complete();

        void run_auxiliary(std::vector<std::string> args, Completion cb);
        void open(Completion cb) override;

        bool command_channels() const override;

        void command(std::string command, Completion cb) override;

        void write(std::string data, Completion cb) override;

        void close_input(Completion cb) override;

        std::string frame_command(const std::string& command, const std::string& marker) override;

        void transfer(bool upload, std::string source, std::string destination, bool overwrite,
                      std::function<void(uint64_t)> progress, Completion cb) override;

        void close() override;
    };
}
