#pragma once
#include "backends/shared/backend_config.hpp"

namespace cd
{
    struct UdpConfig
    {
        std::string host, local_host;
        uint16_t port = 0, local_port = 0;
    };
}
