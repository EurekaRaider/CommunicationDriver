#include "backends/shared/shell_session.hpp"
#include <charconv>

namespace cd
{
    bool ShellSession::filter_prompt(std::string& data)
    {
        if (prompt_token.empty())
            return true;

        prompt_buffer += data;

        if (prompt_ready)
        {
            auto pos = prompt_buffer.find(prompt_token);

            if (pos == std::string::npos)
            {
                if (prompt_buffer.size() > max_bytes)
                {
                    auto cb = std::exchange(prompt_ready, {});
                    cb(CD_LIMIT_EXCEEDED, "CMD startup exceeded buffer limit", 0);
                }
                return false;
            }

            prompt_buffer.erase(0, pos + prompt_token.size());

            auto cb = std::exchange(prompt_ready, {});
            cb(CD_OK, {}, 0);
        }

        auto marker_line = [&](size_t end)
        {
            if (completion_marker.empty())
                return false;

            auto prefix = output_tail + prompt_buffer.substr(0, end);
            auto marker = prefix.rfind(completion_marker + ":");

            if (marker == std::string::npos)
                return false;

            auto status = prefix.substr(marker + completion_marker.size() + 1);
            int code;
            auto parsed = std::from_chars(status.data(), status.data() + status.size(), code);
            return !status.empty() && parsed.ec == std::errc() &&
                   parsed.ptr == status.data() + status.size();
        };

        size_t pos;

        while ((pos = prompt_buffer.find(prompt_token)) != std::string::npos)
        {
            size_t start =
                pos >= 2 && prompt_buffer.compare(pos - 2, 2, "\r\n") == 0 && !marker_line(pos - 2)
                    ? pos - 2
                    : pos;
            prompt_buffer.erase(start, pos - start + prompt_token.size());
        }

        size_t suffix = 0;

        for (size_t n = 1; n < prompt_token.size() && n <= prompt_buffer.size(); ++n)
            if (prompt_buffer.compare(prompt_buffer.size() - n, n, prompt_token.substr(0, n)) == 0)
                suffix = n;

        if (suffix && prompt_buffer.size() >= suffix + 2 &&
            prompt_buffer.compare(prompt_buffer.size() - suffix - 2, 2, "\r\n") == 0)
            suffix += 2;

        if (!suffix && prompt_buffer.ends_with("\r\n") && !marker_line(prompt_buffer.size() - 2))
            suffix = 2;
        else if (!suffix && prompt_buffer.ends_with('\r'))
            suffix = 1;

        data = prompt_buffer.substr(0, prompt_buffer.size() - suffix);
        prompt_buffer.erase(0, prompt_buffer.size() - suffix);
        output_tail += data;

        auto limit = completion_marker.size() + 64;

        if (output_tail.size() > limit)
            output_tail.erase(0, output_tail.size() - limit);

        return true;
    }

    std::string frame_shell_command(
        const std::string& command, const std::string& marker, int shell)
    {
        if (shell == CD_SHELL_CMD)
            return command + "\r\nset \"__cd_exit=%errorlevel%\"\r\necho " + marker +
                   ":%__cd_exit%\r\necho " + marker + ":%__cd_exit% 1>&2\r\n";
        if ((shell == CD_SHELL_PWSH || shell == CD_SHELL_WINDOWS_POWERSHELL))
            return command +
                   "\n$__cd_exit = if ($?) { 0 } else { if ($LASTEXITCODE) { $LASTEXITCODE } else { 1 } }; [Console]::Out.Write('" +
                   marker + ":'+$__cd_exit+\"`n\"); [Console]::Error.Write('" + marker +
                   ":'+$__cd_exit+\"`n\")\n";

        // eval preserves cd/env in the current shell while keeping arbitrary multi-line commands intact.
        std::string quoted = "'";

        for (char c : command)
            quoted += c == '\'' ? "'\\''" : std::string(1, c);

        quoted += "'";

        return "eval " + quoted + "\n__cd_exit=$?; printf '" + marker +
               ":%s\\n' \"$__cd_exit\"; printf '" + marker + ":%s\\n' \"$__cd_exit\" >&2\n";
    }
}
