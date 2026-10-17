#include "backends/adb/adb_backend.hpp"
#include <random>

namespace cd
{
    AdbBackend::AdbBackend(std::shared_ptr<Runtime> r, BackendConfig<AdbConfig> c)
        : Backend(r), config(std::move(c))
    {
    }

    std::vector<std::string> AdbBackend::adb_args()
    {
        return {"-H", config.server_host, "-P", std::to_string(config.server_port),
                "-s", config.device};
    }

    BackendConfig<ProcessConfig> AdbBackend::process_config(std::vector<std::string> args)
    {
        ProcessConfig options;
        options.mode = CD_ONE_SHOT;
        options.executable = config.executable.empty() ? "adb" : config.executable;
        options.arguments = std::move(args);
        return BackendConfig<ProcessConfig>(config.defaults, std::move(options));
    }

    void AdbBackend::launch(std::string command, bool one, Completion cb)
    {
        try
        {
            auto args = adb_args();
            args.insert(args.end(), {"shell", "-T"});
            args.push_back(one ? command
                               : (config.session_command.empty() ? "sh" : config.session_command));
            auto options = process_config(std::move(args));
            process = std::make_shared<ProcessChannel>(runtime, std::move(options));
            process->missing_executable = CD_DEVICE_UNAVAILABLE;
            process->output = output;
            process->exited = exited;
            process->failure = failure;
            process->launch({}, one, std::move(cb));
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

    void AdbBackend::auxiliary_finish(cd_status_t status, std::string message, int64_t native)
    {
        if (!auxiliary_callback)
            return;

        auto cb = std::exchange(auxiliary_callback, {});

        if (auxiliary)
        {
            auxiliary->close();
            auxiliary.reset();
        }

        cb(status, std::move(message), native);
    }

    void AdbBackend::auxiliary_complete()
    {
        if (auxiliary_exit && auxiliary_eof[0] && auxiliary_eof[1])
            auxiliary_finish(*auxiliary_exit == 0 ? CD_OK : CD_IO_ERROR, auxiliary_output,
                             *auxiliary_exit);
    }

    void AdbBackend::run_auxiliary(std::vector<std::string> args, Completion cb)
    {
        auxiliary_callback = std::move(cb);
        auxiliary_output.clear();
        auxiliary_eof[0] = auxiliary_eof[1] = false;
        auxiliary_exit.reset();

        auxiliary = std::make_shared<ProcessChannel>(runtime, process_config(std::move(args)));

        auto weak =
            std::weak_ptr<AdbBackend>(std::static_pointer_cast<AdbBackend>(shared_from_this()));
        auxiliary->output = [weak](int stream, std::string data, bool end)
        {
            if (auto self = weak.lock())
            {
                self->auxiliary_eof[stream] |= end;
                self->auxiliary_output += data;
                if (self->auxiliary_output.size() > self->config.defaults.max_bytes)
                    self->auxiliary_finish(CD_LIMIT_EXCEEDED, "ADB control output exceeded limit",
                                           0);
                else
                    self->auxiliary_complete();
            }
        };
        auxiliary->exited = [weak](std::optional<int> code)
        {
            if (auto self = weak.lock())
            {
                self->auxiliary_exit = code;
                self->auxiliary_complete();
            }
        };
        auxiliary->failure = [weak](cd_status_t s, std::string m, int64_t n)
        {
            if (auto self = weak.lock())
                self->auxiliary_finish(s, m, n);
        };
        auxiliary->command({},
                           [weak](cd_status_t s, std::string m, int64_t n)
                           {
                               if (s != CD_OK)
                                   if (auto self = weak.lock())
                                       self->auxiliary_finish(s, m, n);
                           });
    }

    void AdbBackend::open(Completion cb)
    {
        auto args = adb_args();
        args.push_back("get-state");

        auto self = std::static_pointer_cast<AdbBackend>(shared_from_this());
        run_auxiliary(
            std::move(args),
            [self, cb](cd_status_t s, std::string output, int64_t native)
            {
                if (s != CD_OK ||
                    (output != "device\n" && output != "device\r\n" && output != "device"))
                {
                    cb(CD_DEVICE_UNAVAILABLE, "Bound ADB device is unavailable: " + output, native);
                    return;
                }

                if (self->config.mode == CD_ONE_SHOT)
                    cb(CD_OK, {}, 0);
                else
                    self->launch({}, false, cb);
            });
    }

    bool AdbBackend::command_channels() const
    {
        return config.mode == CD_ONE_SHOT;
    }

    void AdbBackend::command(std::string command, Completion cb)
    {
        auto self = std::static_pointer_cast<AdbBackend>(shared_from_this());
        launch(std::move(command), true,
               [self, cb](cd_status_t s, std::string m, int64_t n)
               {
                   if (s != CD_OK)
                       cb(s, std::move(m), n);
                   else
                       self->close_input(cb);
               });
    }

    void AdbBackend::write(std::string data, Completion cb)
    {
        process->write(std::move(data), std::move(cb));
    }

    void AdbBackend::close_input(Completion cb)
    {
        process->close_input(std::move(cb));
    }

    std::string AdbBackend::frame_command(const std::string& command, const std::string& marker)
    {
        return frame_shell_command(command, marker, CD_SHELL_SH);
    }

    void AdbBackend::transfer(bool upload, std::string source, std::string destination,
                              bool overwrite, std::function<void(uint64_t)> progress, Completion cb)
    {
        transfer_partial_file.clear();

        try
        {
            if (!upload && std::filesystem::is_directory(utf8_path(destination)))
                throw Fault(CD_INVALID_ARGUMENT, "Destination must be a single file");
            if (!upload && !overwrite && std::filesystem::exists(utf8_path(destination)))
                throw Fault(CD_PERMISSION_DENIED, "Destination already exists");
            if (upload && !std::filesystem::is_regular_file(utf8_path(source)))
                throw Fault(CD_INVALID_ARGUMENT, "Source must be a single regular file");

            auto self = std::static_pointer_cast<AdbBackend>(shared_from_this());

            auto quote = [](const std::string& value)
            {
                std::string text = "'";
                for (char c : value)
                    text += c == '\'' ? "'\\''" : std::string(1, c);
                return text + "'";
            };

            std::random_device random;
            auto staging = overwrite ? destination
                                     : destination + ".cd-partial-" + std::to_string(random()) +
                                           std::to_string(random());

            auto perform =
                [self, upload, source, destination, staging, overwrite, progress, cb, quote]
            {
                self->transfer_partial_file = staging;

                auto args = self->adb_args();
                args.push_back(upload ? "push" : "pull");
                args.push_back(source);
                args.push_back(staging);
                self->run_auxiliary(
                    std::move(args),
                    [self, upload, source, destination, staging, overwrite, progress, cb,
                     quote](cd_status_t s, std::string m, int64_t n)
                    {
                        if (s != CD_OK)
                        {
                            cb(s, std::move(m), n);
                            return;
                        }

                        auto done = [self, upload, source, destination, progress,
                                     cb](cd_status_t status, std::string message, int64_t native)
                        {
                            if (status == CD_OK)
                            {
                                self->transfer_partial_file.clear();

                                std::error_code e;
                                auto size = std::filesystem::file_size(
                                    utf8_path(upload ? source : destination), e);

                                if (!e)
                                    progress(size);
                            }

                            cb(status, std::move(message), native);
                        };

                        if (overwrite)
                        {
                            done(CD_OK, {}, 0);
                            return;
                        }

                        if (upload)
                        {
                            auto install = self->adb_args();
                            install.insert(install.end(),
                                           {"shell", "-T",
                                            "ln " + quote(staging) + " " + quote(destination) +
                                                " && rm " + quote(staging)});
                            self->run_auxiliary(std::move(install), std::move(done));
                        }
                        else
                        {
                            std::error_code error;
                            std::filesystem::create_hard_link(utf8_path(staging),
                                                              utf8_path(destination), error);

                            if (error)
                            {
                                done(error == std::errc::file_exists ? CD_PERMISSION_DENIED
                                                                     : CD_IO_ERROR,
                                     error.message(), error.value());
                                return;
                            }

                            std::filesystem::remove(utf8_path(staging), error);
                            done(error ? CD_IO_ERROR : CD_OK, error.message(), error.value());
                        }
                    });
            };

            auto args = adb_args();
            args.insert(args.end(), {"shell", "-T",
                                     std::string(upload ? "test -d " : "test -f ") +
                                         quote(upload ? destination : source)});
            run_auxiliary(
                std::move(args),
                [self, upload, overwrite, destination, perform, cb,
                 quote](cd_status_t s, std::string m, int64_t code)
                {
                    if ((upload && s == CD_OK) || (!upload && code == 1))
                    {
                        cb(CD_INVALID_ARGUMENT, "Transfer requires a single file path", 0);
                        return;
                    }

                    if ((upload && code != 1) || (!upload && s != CD_OK))
                    {
                        cb(s, std::move(m), code);
                        return;
                    }

                    if (upload && !overwrite)
                    {
                        auto probe = self->adb_args();
                        probe.insert(probe.end(), {"shell", "-T", "test -e " + quote(destination)});
                        self->run_auxiliary(
                            std::move(probe),
                            [perform, cb](cd_status_t status, std::string message, int64_t native)
                            {
                                if (status == CD_OK)
                                    cb(CD_PERMISSION_DENIED, "Destination already exists", 0);
                                else if (native == 1)
                                    perform();
                                else
                                    cb(status, std::move(message), native);
                            });
                    }
                    else
                        perform();
                });
        }
        catch (const Fault& f)
        {
            cb(f.code, f.message, f.native);
        }
        catch (const std::exception& e)
        {
            cb(CD_IO_ERROR, e.what(), 0);
        }
    }

    void AdbBackend::close()
    {
        if (auxiliary)
        {
            auxiliary->close();
            auxiliary.reset();
        }
        auxiliary_callback = {};
        if (process)
            process->close();
    }
}
