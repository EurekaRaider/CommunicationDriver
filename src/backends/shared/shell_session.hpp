#pragma once
#include "core/common.hpp"

namespace cd
{
    struct ShellSession
    {
        std::string completion_marker, prompt_token, prompt_buffer, output_tail;
        Completion prompt_ready;
        uint64_t max_bytes;

        explicit ShellSession(uint64_t limit) : max_bytes(limit)
        {
        }

        bool filter_prompt(std::string&);
    };

    std::string frame_shell_command(const std::string&, const std::string&, int shell);
}
