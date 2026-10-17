#pragma once
#include "backends/shared/backend_config.hpp"

namespace cd
{
    struct SshConfig : SessionConfig
    {
        std::string host, user, password, private_key, passphrase, host_key;
        uint16_t port = 0;
        int auth = 0, key_kind = 0;
    };
}
