#include "platform/process.hpp"

namespace cd
{
    std::pair<std::string, std::vector<std::string>> shell_command(
        int shell, bool one_shot, const std::string& command)
    {
#ifdef _WIN32
        if (shell == CD_SHELL_DEFAULT)
            shell = CD_SHELL_CMD;
#else
        if (shell == CD_SHELL_DEFAULT)
            shell = CD_SHELL_SH;
#endif

        if (shell == CD_SHELL_CMD)
            return {"cmd.exe", one_shot ? std::vector<std::string>{"/D", "/S", "/C", command}
                                        : std::vector<std::string>{"/D", "/Q"}};
        if (shell == CD_SHELL_WINDOWS_POWERSHELL || shell == CD_SHELL_PWSH)
            return {shell == CD_SHELL_PWSH ? "pwsh" : "powershell.exe",
                one_shot ? std::vector<std::string>{"-NoLogo", "-NoProfile", "-NonInteractive",
                               "-Command", command}
                         : std::vector<std::string>{
                               "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", "-"}};

        std::string exe = shell == CD_SHELL_BASH ? "bash" : shell == CD_SHELL_ZSH ? "zsh" : "sh";
        return {
            exe, one_shot ? std::vector<std::string>{"-c", command} : std::vector<std::string>{}};
    }

    process::filesystem::path executable_path(const std::string& executable)
    {
        auto path = utf8_path(executable);
        if (path.has_parent_path())
        {
            if (!std::filesystem::is_regular_file(path))
                return {};
            return process::filesystem::path(std::filesystem::absolute(path).native());
        }
        return process::environment::find_executable(executable);
    }
}
