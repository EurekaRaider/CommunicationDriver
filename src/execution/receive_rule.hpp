#pragma once
#include "execution/check.hpp"

namespace cd
{
    struct Rule
    {
        int kind = CD_RECEIVE_NONE;
        uint64_t count = 0, idle_ms = 0;
        bool text = false, include = false;
        std::string value;
        std::shared_ptr<RE2> regex;
        Rule() = default;
        explicit Rule(const cd_receive_until_t&);
    };
}
