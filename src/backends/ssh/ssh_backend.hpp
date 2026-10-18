#pragma once
#include "core/backend.hpp"
#include "backends/ssh/ssh_config.hpp"
#include "backends/shared/shell_session.hpp"
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <fstream>

namespace cd
{
    class SshBackend final : public Backend
    {
        BackendConfig<SshConfig> config;
        ShellSession prompt;
        asio::ip::tcp::socket socket;
        asio::ip::tcp::resolver resolver;

        LIBSSH2_SESSION* session = nullptr;
        LIBSSH2_CHANNEL* channel = nullptr;
        LIBSSH2_CHANNEL* stdio_channel = nullptr;
        LIBSSH2_SFTP* sftp = nullptr;
        LIBSSH2_SFTP_HANDLE* file = nullptr;

        bool stopped = false, waiting = false, write_eof = false, command_active = false;
        int phase = 0, channel_phase = 0, transfer_phase = 0;
        bool stdio_started = false;
        uint64_t wait_revision = 0;
        int wait_directions = 0;

        Completion open_callback, command_callback, write_callback, transfer_callback;
        std::string command_text, write_data, source, destination;
        size_t write_offset = 0;

        bool upload = false, overwrite = true;
        uint64_t transferred = 0;
        std::function<void(uint64_t)> progress;
        std::shared_ptr<std::fstream> local;
        bool disk_busy = false;
        std::array<char, 32768> file_buffer{};
        size_t file_size = 0, file_offset = 0;

    public:
        SshBackend(std::shared_ptr<Runtime> r, BackendConfig<SshConfig> c);

        ~SshBackend() override;

        std::string session_shell();

        std::string one_shot_command(const std::string& command);

        std::string last_error();

        void report(cd_status_t status, int rc);

        void wait_socket();

        bool retry(int rc, cd_status_t status = CD_IO_ERROR);

        void open(Completion cb) override;

        bool verify_key();

        void drive();

        bool command_channels() const override;

        void command(std::string command, Completion cb) override;

        void ensure_stdio(Completion cb) override;

        void write(std::string data, Completion cb) override;

        void close_input(Completion cb) override;

        void transfer(bool is_upload, std::string from, std::string to, bool replace,
            std::function<void(uint64_t)> on_progress, Completion cb) override;

        void drive_transfer();

        void close() override;
        std::string frame_command(const std::string& command, const std::string& marker) override;
    };
}
