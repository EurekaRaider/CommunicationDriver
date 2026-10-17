#pragma once
#include "connection.hpp"

namespace comm
{
    class Driver
    {
        std::shared_ptr<cd_driver_t> handle_;

        template <class F> Connection create(F&& f)
        {
            cd_connection_t* c = nullptr;
            cd_result_t* e = nullptr;
            auto status = f(handle_.get(), &c, &e);
            detail::ResultHandle error(e);
            detail::checked(status, e);

            return Connection(c, handle_);
        }

      public:
        Driver()
        {
            cd_driver_t* h = nullptr;
            detail::checked(cd_driver_create(&h));
            handle_ = {h, cd_driver_release};
        }

        Connection create_tcp(const TcpOptions& p)
        {
            cd_tcp_options_t o;
            cd_tcp_options_init(&o);
            o.defaults = p.native();
            o.host = detail::view(p.host);
            o.port = p.port;

            return create(
                [&](auto* d, auto** c, auto** e)
                {
                    return cd_create_tcp(d, &o, c, e);
                });
        }

        Connection create_udp(const UdpOptions& p)
        {
            cd_udp_options_t o;
            cd_udp_options_init(&o);
            o.defaults = p.native();
            o.remote_host = detail::view(p.remote_host);
            o.local_host = detail::view(p.local_host);
            o.remote_port = p.remote_port;
            o.local_port = p.local_port;

            return create(
                [&](auto* d, auto** c, auto** e)
                {
                    return cd_create_udp(d, &o, c, e);
                });
        }

        Connection create_serial(const SerialOptions& p)
        {
            cd_serial_options_t o;
            cd_serial_options_init(&o);
            o.defaults = p.native();
            o.port_name = detail::view(p.port_name);
            o.baud_rate = p.baud_rate;
            o.data_bits = p.data_bits;
            o.parity = static_cast<int>(p.parity);
            o.stop_bits = static_cast<int>(p.stop_bits);
            o.flow_control = static_cast<int>(p.flow_control);

            return create(
                [&](auto* d, auto** c, auto** e)
                {
                    return cd_create_serial(d, &o, c, e);
                });
        }

        Connection create_ssh(const SshOptions& p)
        {
            cd_ssh_options_t o;
            cd_ssh_options_init(&o);
            o.defaults = p.native();
            o.host = detail::view(p.host);
            o.port = p.port;
            o.user_name = detail::view(p.user_name);
            o.auth = p.auth.kind;

            if (p.auth.kind == CD_AUTH_PASSWORD)
                o.password = detail::view(p.auth.secret);
            else
                o.private_key_path = detail::view(p.auth.secret);

            o.passphrase = detail::view(p.auth.passphrase);
            o.host_key_kind = p.host_key.kind;
            o.host_key = detail::view(p.host_key.value);
            o.session_command = detail::view(p.session_command);
            o.shell = static_cast<int>(p.shell);
            o.execution_mode = static_cast<int>(p.execution_mode);

            return create(
                [&](auto* d, auto** c, auto** e)
                {
                    return cd_create_ssh(d, &o, c, e);
                });
        }

        Connection create_adb(const AdbOptions& p)
        {
            cd_adb_options_t o;
            cd_adb_options_init(&o);
            o.defaults = p.native();
            o.device_serial = detail::view(p.device_serial);
            o.executable_path = detail::view(p.executable_path);
            o.server_host = detail::view(p.server_host);
            o.server_port = p.server_port;
            o.session_command = detail::view(p.session_command);
            o.execution_mode = static_cast<int>(p.execution_mode);

            return create(
                [&](auto* d, auto** c, auto** e)
                {
                    return cd_create_adb(d, &o, c, e);
                });
        }

        Connection create_cli(const CliOptions& p)
        {
            cd_cli_options_t o;
            cd_cli_options_init(&o);
            o.defaults = p.native();
            o.shell = static_cast<int>(p.shell);
            o.execution_mode = static_cast<int>(p.execution_mode);
            o.executable_path = detail::view(p.executable_path);
            o.working_directory = detail::view(p.working_directory);

            std::vector<cd_string_view_t> args;

            for (auto& s : p.arguments)
                args.push_back(detail::view(s));

            o.arguments = args.data();
            o.argument_count = args.size();

            std::vector<cd_environment_entry_t> env;

            for (auto& [k, v] : p.environment)
                env.push_back({detail::view(k), detail::view(v)});

            o.environment = env.data();
            o.environment_count = env.size();

            return create(
                [&](auto* d, auto** c, auto** e)
                {
                    return cd_create_cli(d, &o, c, e);
                });
        }

        Operation<void> close_async()
        {
            cd_operation_t* op = nullptr;
            detail::checked(cd_driver_close_async(handle_.get(), nullptr, nullptr, &op));
            return Operation<void>(op);
        }

        void close()
        {
            cd_result_t* r = nullptr;
            auto status = cd_driver_close(handle_.get(), &r);
            detail::ResultHandle result(r);
            detail::checked(status, r);
        }
    };
}
