#pragma once
#include "core/common.hpp"
#include <re2/re2.h>

namespace cd
{
    struct Check
    {
        int kind = CD_CHECK_NONE;
        bool text = true;
        std::string value;
        std::shared_ptr<RE2> regex;
        explicit Check(const cd_check_t* = nullptr);
    };

    std::shared_ptr<RE2> compile_regex(const std::string&);
}
