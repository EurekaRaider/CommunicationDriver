#pragma once
#include "core/backend.hpp"
#include "backends/serial/serial_config.hpp"

namespace cd
{
    class SerialBackend final : public Backend
    {
        BackendConfig<SerialConfig> config;
        asio::serial_port serial;
        std::array<char, 65536> buffer{};
        bool stopped = false;

    public:
        SerialBackend(std::shared_ptr<Runtime> r, BackendConfig<SerialConfig> c);

        void open(Completion cb) override;

        void pump();

        void write(std::string data, Completion cb) override;

        void close() override;

        void close_input(Completion cb) override;
    };
}
