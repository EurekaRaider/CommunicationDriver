#pragma once
#include "backends/shared/backend_config.hpp"

namespace cd
{
    struct SerialConfig
    {
        std::string port_name;
        uint32_t baud = 115200, bits = 8;
        int parity = 0, stop = 0, flow = 0;
    };
}
