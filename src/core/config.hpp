#pragma once
#include "core/options.hpp"
#include "core/backend.hpp"
#include "backends/backend_options.hpp"

namespace cd
{
    struct BackendTraits
    {
        const char* name;
        cd_capabilities_t capabilities;
        bool commands = false;
        bool managed = false;
        bool reuse_command_channel = false;
        int mode = CD_PERSISTENT;
    };

    struct ConnectionConfig
    {
        Defaults defaults;
        BackendTraits traits;
        BackendOptions backend_options;

        const char* name() const
        {
            return traits.name;
        }

        bool managed() const
        {
            return traits.managed;
        }
    };

    std::shared_ptr<Backend> make_backend(std::shared_ptr<Runtime>, const ConnectionConfig&,
                                          bool persistent = false);
}
