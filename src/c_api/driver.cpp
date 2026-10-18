#include "c_api/common.hpp"
#include "backends/backend_factory.hpp"

using namespace cd;
using namespace cd::abi;

namespace
{
    template <class T>
    cd_status_t make_connection(cd_driver_t* driver, BackendConfig<T> config, cd_connection_t** out)
    {
        require(driver && out, "Driver and output handle required");
        *out = nullptr;
        if constexpr (std::is_base_of_v<SessionConfig, T>)
        {
            require(config.mode == CD_PERSISTENT || config.mode == CD_ONE_SHOT,
                "Invalid execution mode");
            require(
                config.shell >= CD_SHELL_DEFAULT && config.shell <= CD_SHELL_ZSH, "Invalid shell");
            if (!config.session_command.empty() && config.mode == CD_ONE_SHOT)
                throw Fault(CD_INVALID_ARGUMENT, "SessionCommand requires Persistent mode");
        }

        auto c = std::make_shared<Connection>(driver->value, connection_config(std::move(config)));
        driver->value->register_connection(c);

        *out = new cd_connection(c);
        return CD_OK;
    }

}

extern "C"
{
    cd_status_t CD_CALL cd_driver_create(cd_driver_t** out)
    {
        try
        {
            require(out, "Driver output required");
            *out = new cd_driver(std::make_shared<Runtime>());
            return CD_OK;
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

#define CREATE_HEAD(name, type)                                                                    \
    cd_status_t CD_CALL cd_create_##name(cd_driver_t* d, const cd_##name##_options_t* o,           \
        cd_connection_t** out, cd_result_t** error)                                                \
    {                                                                                              \
        if (error)                                                                                 \
            *error = nullptr;                                                                      \
        if (out)                                                                                   \
            *out = nullptr;                                                                        \
        try                                                                                        \
        {                                                                                          \
            require(o && o->struct_size >= sizeof(*o), "Invalid creation options");                \
            BackendConfig<type> c(o->defaults)
#define CREATE_END                                                                                 \
    return make_connection(d, std::move(c), out);                                                  \
    }                                                                                              \
    catch (...)                                                                                    \
    {                                                                                              \
        return exception_result(error);                                                            \
    }                                                                                              \
    }
    CREATE_HEAD(tcp, TcpConfig);
    c.host = copy(o->host);
    c.port = o->port;
    text_argument(c.host, "Host required");
    require(c.port, "Port required");
    CREATE_END
    CREATE_HEAD(udp, UdpConfig);
    c.host = copy(o->remote_host);
    c.port = o->remote_port;
    c.local_host = copy(o->local_host);
    c.local_port = o->local_port;
    text_argument(c.host, "RemoteHost required");
    require(c.port, "RemotePort required");
    text_argument(c.local_host, "Invalid LocalHost", false);
    CREATE_END
    CREATE_HEAD(serial, SerialConfig);
    c.port_name = copy(o->port_name);
    c.baud = o->baud_rate;
    c.bits = o->data_bits;
    c.parity = o->parity;
    c.stop = o->stop_bits;
    c.flow = o->flow_control;
    text_argument(c.port_name, "PortName required");
    require(c.baud&& c.bits >= 5 && c.bits <= 8, "Invalid serial settings");
    require(
        c.parity >= 0 && c.parity <= 2 && c.stop >= 0 && c.stop <= 2 && c.flow >= 0 && c.flow <= 2,
        "Invalid serial setting enum");
    CREATE_END
    CREATE_HEAD(ssh, SshConfig);
    c.host = copy(o->host);
    c.port = o->port;
    c.user = copy(o->user_name);
    c.password = copy(o->password);
    c.private_key = copy(o->private_key_path);
    c.passphrase = copy(o->passphrase);
    c.host_key = copy(o->host_key);
    c.auth = o->auth;
    c.key_kind = o->host_key_kind;
    c.shell = o->shell;
    c.mode = o->execution_mode;
    c.session_command = copy(o->session_command);
    text_argument(c.host, "Host required");
    text_argument(c.user, "UserName required");
    text_argument(c.host_key, "HostKey trust required");
    require(c.port&& c.auth >= 0 && c.auth <= 1 && c.key_kind >= 0 && c.key_kind <= 1,
        "Invalid SSH options");
    if (c.auth == CD_AUTH_PRIVATE_KEY)
        text_argument(c.private_key, "PrivateKey path required");
    CREATE_END
    CREATE_HEAD(adb, AdbConfig);
    c.device = copy(o->device_serial);
    c.executable = copy(o->executable_path);
    c.server_host = copy(o->server_host);
    c.server_port = o->server_port;
    c.mode = o->execution_mode;
    c.session_command = copy(o->session_command);
    text_argument(c.device, "DeviceSerial required");
    text_argument(c.server_host, "ServerHost required");
    require(c.server_port, "ServerPort required");
    text_argument(c.executable, "Invalid executable", false);
    CREATE_END
    CREATE_HEAD(cli, CliConfig);
    c.shell = o->shell;
    c.mode = o->execution_mode;
    c.executable = copy(o->executable_path);
    c.working_directory = copy(o->working_directory);
    require(o->arguments || o->argument_count == 0, "Arguments pointer required");
    require(o->environment || o->environment_count == 0, "Environment pointer required");
    for (uint64_t i = 0; i < o->argument_count; ++i)
    {
        auto arg = copy(o->arguments[i]);
        text_argument(arg, "Invalid argument", false);
        c.arguments.push_back(std::move(arg));
    }
    for (uint64_t i = 0; i < o->environment_count; ++i)
    {
        auto k = copy(o->environment[i].key);
        auto v = copy(o->environment[i].value);
        text_argument(k, "Invalid environment key");
        text_argument(v, "Invalid environment value", false);
        require(k.find('=') == std::string::npos, "Environment key cannot contain =");
        c.environment[k] = v;
    }
    text_argument(c.executable, "Invalid executable", false);
    text_argument(c.working_directory, "Invalid working directory", false);
    CREATE_END
#undef CREATE_HEAD
#undef CREATE_END

    cd_status_t CD_CALL cd_driver_close_async(
        cd_driver_t* d, cd_completion_fn cb, void* user, cd_operation_t** out)
    {
        try
        {
            require(d, "Driver required");

            auto op = std::make_shared<Operation>(d->value);
            accept(op, cb, user, out);
            d->value->close(
                [op]
                {
                    op->complete(std::make_shared<Result>());
                });

            return CD_OK;
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_driver_close(cd_driver_t* d, cd_result_t** out)
    {
        try
        {
            require(d && out, "Driver and result required");

            *out = nullptr;
            if (current_io == d->value->state.get())
            {
                *out = new cd_result(error_result(
                    Fault(CD_INVALID_CONTEXT, "Use CloseAsync in a completion callback")));
                return CD_INVALID_CONTEXT;
            }

            cd_operation_t* op = nullptr;
            auto s = cd_driver_close_async(d, nullptr, nullptr, &op);

            if (s != CD_OK)
                return s;

            s = cd_operation_wait(op, CD_INFINITE, out);
            release(op);

            return s;
        }
        catch (...)
        {
            return exception_result(out);
        }
    }
}
