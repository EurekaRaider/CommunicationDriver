#pragma once
#include "core/common.hpp"
#include <boost/process.hpp>

namespace cd
{
    namespace process = boost::process;
#ifdef __APPLE__
    std::shared_ptr<process::process> spawn_process_macos(asio::io_context&,
        const process::filesystem::path&, std::vector<std::string>&, std::vector<std::string>&,
        const std::filesystem::path&, asio::writable_pipe&, asio::readable_pipe&,
        asio::readable_pipe&);
#endif
    std::pair<std::string, std::vector<std::string>> shell_command(int, bool, const std::string&);
    process::filesystem::path executable_path(const std::string&);
}
