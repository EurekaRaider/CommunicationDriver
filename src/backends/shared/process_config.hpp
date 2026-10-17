#pragma once
#include "backends/shared/backend_config.hpp"

namespace cd
{
    struct ProcessConfig : SessionConfig
    {
        std::string executable, working_directory;
        std::vector<std::string> arguments;
        std::map<std::string, std::string> environment;
    };
}
