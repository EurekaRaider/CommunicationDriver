#include "backends/cli/cli_backend.hpp"

namespace cd
{
    CliBackend::CliBackend(std::shared_ptr<Runtime> r, BackendConfig<CliConfig> c)
        : Backend(r), process(std::make_shared<ProcessChannel>(
                          r, BackendConfig<ProcessConfig>(
                                 std::move(c.defaults), std::move(static_cast<ProcessConfig&>(c)))))
    {
    }

    void CliBackend::bind()
    {
        process->output = output;
        process->exited = exited;
        process->failure = failure;
    }

    void CliBackend::open(Completion cb)
    {
        bind();
        process->open(std::move(cb));
    }

    void CliBackend::command(std::string command, Completion cb)
    {
        bind();
        process->command(std::move(command), std::move(cb));
    }

    void CliBackend::write(std::string data, Completion cb)
    {
        process->write(std::move(data), std::move(cb));
    }

    void CliBackend::close_input(Completion cb)
    {
        process->close_input(std::move(cb));
    }

    void CliBackend::close()
    {
        process->close();
    }

    bool CliBackend::command_channels() const
    {
        return process->config.mode == CD_ONE_SHOT;
    }

    std::string CliBackend::frame_command(const std::string& command, const std::string& marker)
    {
        process->shell.completion_marker = marker;
        int shell = process->config.shell;
#ifdef _WIN32
        if (shell == CD_SHELL_DEFAULT)
            shell = CD_SHELL_CMD;
#endif
        return frame_shell_command(command, marker, shell);
    }
}
