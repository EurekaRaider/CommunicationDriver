#pragma once
#include "check.hpp"

namespace comm
{
    struct ConnectionOptions
    {
        Encoding encoding = Encoding::UTF8;
        std::string new_line = "\n";
        std::optional<ReceiveUntil> receive_until;
        Duration connect_timeout{10000}, io_timeout{5000}, execution_timeout{30000},
            close_timeout{10000};
        uint64_t max_response_bytes = 1048576;

        cd_connection_options_t native() const
        {
            cd_connection_options_t o;
            cd_connection_options_init(&o);
            o.encoding = static_cast<int>(encoding);
            o.new_line = detail::view(new_line);

            if (receive_until)
                o.receive_until = receive_until->native();

            o.connect_timeout_ms = connect_timeout.count();
            o.io_timeout_ms = io_timeout.count();
            o.execution_timeout_ms = execution_timeout.count();
            o.close_timeout_ms = close_timeout.count();
            o.max_response_bytes = max_response_bytes;

            return o;
        }
    };

    struct TcpOptions : ConnectionOptions
    {
        std::string host;
        uint16_t port = 0;
    };

    struct UdpOptions : ConnectionOptions
    {
        std::string remote_host, local_host;
        uint16_t remote_port = 0, local_port = 0;
    };

    struct SerialOptions : ConnectionOptions
    {
        std::string port_name;
        uint32_t baud_rate = 115200, data_bits = 8;
        Parity parity = Parity::None;
        StopBits stop_bits = StopBits::One;
        FlowControl flow_control = FlowControl::None;
    };

    struct SshAuth
    {
        int kind = 0;
        std::string secret, passphrase;

        static SshAuth password(std::string s)
        {
            return {CD_AUTH_PASSWORD, std::move(s), {}};
        }

        static SshAuth private_key(std::string path, std::string phrase = {})
        {
            return {CD_AUTH_PRIVATE_KEY, std::move(path), std::move(phrase)};
        }
    };

    struct HostKey
    {
        int kind = 0;
        std::string value;

        static HostKey known_hosts(std::string path)
        {
            return {CD_HOST_KEY_KNOWN_HOSTS, std::move(path)};
        }

        static HostKey sha256(std::string fingerprint)
        {
            return {CD_HOST_KEY_SHA256, std::move(fingerprint)};
        }
    };

    struct SshOptions : ConnectionOptions
    {
        std::string host, user_name, session_command;
        uint16_t port = 22;
        SshAuth auth;
        HostKey host_key;
        ShellKind shell = ShellKind::Default;
        ExecutionMode execution_mode = ExecutionMode::Persistent;
    };

    struct AdbOptions : ConnectionOptions
    {
        std::string device_serial, executable_path, server_host = "127.0.0.1", session_command;
        uint16_t server_port = 5037;
        ExecutionMode execution_mode = ExecutionMode::Persistent;
    };

    struct CliOptions : ConnectionOptions
    {
        ShellKind shell = ShellKind::Default;
        std::string executable_path, working_directory;
        std::vector<std::string> arguments;
        std::map<std::string, std::string> environment;
        ExecutionMode execution_mode = ExecutionMode::Persistent;
    };

    struct CallOptions
    {
        std::optional<Duration> timeout;
        std::optional<Encoding> encoding;
        std::string new_line;
        uint64_t max_response_bytes = 0;
        std::optional<ReceiveUntil> receive_until;
        Stream read_stream = Stream::StandardOutput, check_stream = Stream::StandardOutput;

        cd_call_options_t native() const
        {
            cd_call_options_t o;
            cd_call_options_init(&o);

            if (timeout)
                o.timeout_ms = timeout->count();

            if (encoding)
                o.encoding = static_cast<int>(*encoding);

            o.new_line = detail::view(new_line);
            o.max_response_bytes = max_response_bytes;

            if (receive_until)
                o.receive_until = receive_until->native();

            o.read_stream = static_cast<int>(read_stream);
            o.check_stream = static_cast<int>(check_stream);

            return o;
        }
    };

    struct TransferOptions
    {
        std::optional<Duration> timeout;
        bool overwrite = true;

        cd_transfer_options_t native() const
        {
            cd_transfer_options_t o;
            cd_transfer_options_init(&o);

            if (timeout)
                o.timeout_ms = timeout->count();

            o.overwrite = overwrite;

            return o;
        }
    };
}
