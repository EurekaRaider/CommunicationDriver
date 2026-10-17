#pragma once
#include "backends/shared/backend_config.hpp"

namespace cd
{
    struct AdbConfig : SessionConfig
    {
        std::string device, executable, server_host;
        uint16_t server_port = 5037;
    };
}
