#pragma once
#include "backends/shared/backend_config.hpp"

namespace cd
{
    struct TcpConfig
    {
        std::string host;
        uint16_t port = 0;
    };
}
