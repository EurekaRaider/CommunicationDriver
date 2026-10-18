#pragma once
#include "core/options.hpp"

namespace cd
{
    struct SessionConfig
    {
        int mode = CD_PERSISTENT;
        int shell = CD_SHELL_DEFAULT;
        std::string session_command;
    };

    template <class T>
    struct BackendConfig : T
    {
        Defaults defaults;

        explicit BackendConfig(const cd_connection_options_t& d) : defaults(d)
        {
        }

        BackendConfig(Defaults d, T options) : T(std::move(options)), defaults(std::move(d))
        {
        }
    };
}
