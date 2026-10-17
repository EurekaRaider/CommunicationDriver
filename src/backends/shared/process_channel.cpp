#include "backends/shared/process_channel.hpp"
#include <random>

namespace cd
{
    ProcessChannel::ProcessChannel(std::shared_ptr<Runtime> r, BackendConfig<ProcessConfig> c)
        : Backend(r), config(std::move(c)), shell(config.defaults.max_bytes), input(r->state->io),
          out(r->state->io), err(r->state->io)
    {
    }

    void ProcessChannel::launch(const std::string& command, bool one, Completion cb)
    {
        try
        {
            std::string executable;
            std::vector<std::string> args;

            if (!config.executable.empty())
            {
                executable = config.executable;
                args = config.arguments;
            }
            else
            {
                auto s = shell_command(config.shell, one, command);
                executable = s.first;
                args = std::move(s.second);
            }

            auto path = executable_path(executable);

            if (path.empty())
                throw Fault(missing_executable, "Executable not found: " + executable);

            std::map<std::string, std::string> env;

            for (auto v : process::environment::current())
            {
                if (v.key().size() == 0)
                    continue;
                env[v.key().string()] = v.value().size() == 0 ? std::string() : v.value().string();
            }

            for (auto& [k, v] : config.environment)
                env[k] = v;

            bool cmd_shell = config.executable.empty() && !one &&
                             (config.shell == CD_SHELL_CMD
#ifdef _WIN32
                              || config.shell == CD_SHELL_DEFAULT
#endif
                             );

            if (cmd_shell)
            {
                std::random_device random;
                shell.prompt_token =
                    "__CD_PROMPT_" + std::to_string(random()) + std::to_string(random()) + "__";
                env["PROMPT"] = shell.prompt_token;
                shell.prompt_ready = cb;
            }

            std::vector<std::string> env_list;

            for (auto& [k, v] : env)
                env_list.push_back(k + "=" + v);

            auto dir = config.working_directory.empty() ? std::filesystem::current_path()
                                                        : utf8_path(config.working_directory);
#ifdef __APPLE__
            child =
                spawn_process_macos(runtime->state->io, path, args, env_list, dir, input, out, err);
#else
            child = std::make_shared<process::process>(
                runtime->state->io, path, args, process::process_stdio{input, out, err},
                process::process_environment(env_list),
                process::process_start_dir(process::filesystem::path(dir.native())));
#endif
            pump(0);
            pump(1);

            auto self = std::static_pointer_cast<ProcessChannel>(shared_from_this());
            // async_wait retains references into the process; keep it alive through completion after Close.
            auto waiting_child = child;
            child->async_wait(
                [self, waiting_child](ErrorCode ec, int code)
                {
                    if (self->stopped)
                        return;
                    if (ec)
                        self->failure(CD_IO_ERROR, ec.message(), ec.value());
                    else
                        self->exited(code);
                });

            if (!cmd_shell)
                cb(CD_OK, {}, 0);
        }
        catch (const Fault& f)
        {
            cb(f.code, f.message, f.native);
        }
        catch (const std::exception& e)
        {
            cb(CD_CONNECTION_FAILED, e.what(), 0);
        }
    }

    void ProcessChannel::open(Completion cb)
    {

        if (config.mode == CD_ONE_SHOT)
        {
            auto executable = config.executable.empty()
                                  ? shell_command(config.shell, true, {}).first
                                  : config.executable;
            if (executable_path(executable).empty())
                cb(CD_CONNECTION_FAILED, "Executable not found: " + executable, 0);
            else
                cb(CD_OK, {}, 0);
            return;
        }

        launch({}, false, std::move(cb));
    }

    bool ProcessChannel::command_channels() const
    {
        return config.mode == CD_ONE_SHOT;
    }

    void ProcessChannel::command(std::string command, Completion cb)
    {
        bool direct = !config.executable.empty();
        auto self = std::static_pointer_cast<ProcessChannel>(shared_from_this());
        launch(command, true,
               [self, direct, command, cb](cd_status_t s, std::string m, int64_t n)
               {
                   if (s != CD_OK)
                   {
                       cb(s, m, n);
                       return;
                   }

                   if (direct)
                       self->write(
                           command,
                           [self, cb](cd_status_t status, std::string message, int64_t native)
                           {
                               if (status != CD_OK)
                                   cb(status, message, native);
                               else
                                   self->close_input(cb);
                           });
                   else
                       self->close_input(cb);
               });
    }

    void ProcessChannel::pump(int stream)
    {
        auto self = std::static_pointer_cast<ProcessChannel>(shared_from_this());
        auto& pipe = stream ? err : out;
        auto& b = stream ? err_buffer : out_buffer;
        pipe.async_read_some(
            asio::buffer(b),
            [self, stream](ErrorCode ec, size_t n)
            {
                if (self->stopped)
                    return;

                auto& buffer = stream ? self->err_buffer : self->out_buffer;

                if (n)
                {
                    std::string data(buffer.data(), n);
                    if (stream == 0 && !self->shell.filter_prompt(data))
                    {
                        self->pump(stream);
                        return;
                    }
                    if (!data.empty())
                        self->output(stream, std::move(data), false);
                }

                if (ec)
                {
                    if (stream == 0 && !self->shell.prompt_ready &&
                        !self->shell.prompt_buffer.empty())
                        self->output(0, std::exchange(self->shell.prompt_buffer, {}), false);

                    if (self->shell.prompt_ready)
                    {
                        auto cb = std::exchange(self->shell.prompt_ready, {});
                        cb(CD_CONNECTION_FAILED, "CMD exited before managed prompt handshake",
                           ec.value());
                    }

                    self->output(stream, {}, true);

                    if (ec != asio::error::eof && ec != asio::error::broken_pipe &&
                        ec != asio::error::operation_aborted)
                        self->failure(CD_IO_ERROR, ec.message(), ec.value());
                }
                else
                    self->pump(stream);
            });
    }

    void ProcessChannel::write(std::string data, Completion cb)
    {
        auto bytes = std::make_shared<std::string>(std::move(data));
        auto self = shared_from_this();
        asio::async_write(input, asio::buffer(*bytes),
                          [self, bytes, cb](ErrorCode ec, size_t)
                          {
                              cb(ec ? CD_IO_ERROR : CD_OK, ec.message(), ec.value());
                          });
    }

    void ProcessChannel::close_input(Completion cb)
    {
        ErrorCode e;
        input.close(e);
        cb(e ? CD_IO_ERROR : CD_OK, e.message(), e.value());
    }

    void ProcessChannel::close()
    {
        stopped = true;

        ErrorCode e;
        input.close(e);
        out.close(e);
        err.close(e);

        if (child)
        {
            child->terminate(e);
            child.reset();
        }
    }
}
