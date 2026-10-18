#include "backends/tcp/tcp_backend.hpp"
#include "backends/udp/udp_backend.hpp"
#include "backends/serial/serial_backend.hpp"
#include "backends/ssh/ssh_backend.hpp"
#include "backends/adb/adb_backend.hpp"
#include "backends/cli/cli_backend.hpp"
#include "backends/backend_factory.hpp"
#include <type_traits>

namespace cd
{
    ConnectionConfig connection_config(BackendConfig<TcpConfig> options)
    {
        BackendTraits traits{"tcp",
            CD_CAP_READ | CD_CAP_WRITE | CD_CAP_EXECUTE | CD_CAP_EXECUTE_UNTIL | CD_CAP_CLOSE_INPUT,
            false, false, false, CD_PERSISTENT};
        auto defaults = std::move(options.defaults);
        return {std::move(defaults), traits, TcpConfig(std::move(options))};
    }

    ConnectionConfig connection_config(BackendConfig<UdpConfig> options)
    {
        BackendTraits traits{"udp",
            CD_CAP_READ | CD_CAP_WRITE | CD_CAP_EXECUTE | CD_CAP_EXECUTE_UNTIL | CD_CAP_MESSAGE,
            false, false, false, CD_PERSISTENT};
        auto defaults = std::move(options.defaults);
        return {std::move(defaults), traits, UdpConfig(std::move(options))};
    }

    ConnectionConfig connection_config(BackendConfig<SerialConfig> options)
    {
        BackendTraits traits{"serial",
            CD_CAP_READ | CD_CAP_WRITE | CD_CAP_EXECUTE | CD_CAP_EXECUTE_UNTIL, false, false, false,
            CD_PERSISTENT};
        auto defaults = std::move(options.defaults);
        return {std::move(defaults), traits, SerialConfig(std::move(options))};
    }

    ConnectionConfig connection_config(BackendConfig<SshConfig> options)
    {
        BackendTraits traits{"ssh",
            CD_CAP_READ | CD_CAP_WRITE | CD_CAP_EXECUTE | CD_CAP_EXECUTE_UNTIL |
                CD_CAP_CLOSE_INPUT | CD_CAP_STDERR | CD_CAP_UPLOAD | CD_CAP_DOWNLOAD,
            true, options.session_command.empty(), true, options.mode};
        auto defaults = std::move(options.defaults);
        return {std::move(defaults), traits, SshConfig(std::move(options))};
    }

    ConnectionConfig connection_config(BackendConfig<AdbConfig> options)
    {
        BackendTraits traits{"adb",
            CD_CAP_READ | CD_CAP_WRITE | CD_CAP_EXECUTE | CD_CAP_EXECUTE_UNTIL |
                CD_CAP_CLOSE_INPUT | CD_CAP_STDERR | CD_CAP_UPLOAD | CD_CAP_DOWNLOAD,
            true, options.session_command.empty(), false, options.mode};
        auto defaults = std::move(options.defaults);
        return {std::move(defaults), traits, AdbConfig(std::move(options))};
    }

    ConnectionConfig connection_config(BackendConfig<CliConfig> options)
    {
        BackendTraits traits{"cli",
            CD_CAP_READ | CD_CAP_WRITE | CD_CAP_EXECUTE | CD_CAP_EXECUTE_UNTIL |
                CD_CAP_CLOSE_INPUT | CD_CAP_STDERR,
            true, options.executable.empty(), false, options.mode};
        auto defaults = std::move(options.defaults);
        return {std::move(defaults), traits, CliConfig(std::move(options))};
    }

    std::shared_ptr<Backend> make_backend(
        std::shared_ptr<Runtime> runtime, const ConnectionConfig& config, bool persistent)
    {
        return std::visit(
            [&](const auto& options) -> std::shared_ptr<Backend>
            {
                using T = std::decay_t<decltype(options)>;
                BackendConfig<T> typed(config.defaults, options);
                if constexpr (std::is_base_of_v<SessionConfig, T>)
                    if (persistent)
                        typed.mode = CD_PERSISTENT;
                if constexpr (std::is_same_v<T, TcpConfig>)
                    return std::make_shared<TcpBackend>(runtime, std::move(typed));
                else if constexpr (std::is_same_v<T, UdpConfig>)
                    return std::make_shared<UdpBackend>(runtime, std::move(typed));
                else if constexpr (std::is_same_v<T, SerialConfig>)
                    return std::make_shared<SerialBackend>(runtime, std::move(typed));
                else if constexpr (std::is_same_v<T, SshConfig>)
                    return std::make_shared<SshBackend>(runtime, std::move(typed));
                else if constexpr (std::is_same_v<T, AdbConfig>)
                    return std::make_shared<AdbBackend>(runtime, std::move(typed));
                else if constexpr (std::is_same_v<T, CliConfig>)
                    return std::make_shared<CliBackend>(runtime, std::move(typed));
            },
            config.backend_options);
    }
}
