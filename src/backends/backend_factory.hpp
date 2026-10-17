#pragma once
#include "core/config.hpp"
#include "backends/tcp/tcp_config.hpp"
#include "backends/udp/udp_config.hpp"
#include "backends/serial/serial_config.hpp"
#include "backends/ssh/ssh_config.hpp"
#include "backends/adb/adb_config.hpp"
#include "backends/cli/cli_config.hpp"

namespace cd
{
    ConnectionConfig connection_config(BackendConfig<TcpConfig>);
    ConnectionConfig connection_config(BackendConfig<UdpConfig>);
    ConnectionConfig connection_config(BackendConfig<SerialConfig>);
    ConnectionConfig connection_config(BackendConfig<SshConfig>);
    ConnectionConfig connection_config(BackendConfig<AdbConfig>);
    ConnectionConfig connection_config(BackendConfig<CliConfig>);
}
