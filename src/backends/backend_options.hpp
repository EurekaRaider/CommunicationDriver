#pragma once
#include "backends/tcp/tcp_config.hpp"
#include "backends/udp/udp_config.hpp"
#include "backends/serial/serial_config.hpp"
#include "backends/ssh/ssh_config.hpp"
#include "backends/adb/adb_config.hpp"
#include "backends/cli/cli_config.hpp"
#include <variant>

namespace cd
{
    using BackendOptions =
        std::variant<TcpConfig, UdpConfig, SerialConfig, SshConfig, AdbConfig, CliConfig>;
}
