#pragma once
#include "core/backend.hpp"
#include "backends/udp/udp_config.hpp"

namespace cd
{
    class UdpBackend final : public Backend
    {
        BackendConfig<UdpConfig> config;
        asio::ip::udp::socket udp;
        asio::ip::udp::resolver udp_resolver;
        std::array<char, 65536> buffer{};
        bool stopped = false;

      public:
        UdpBackend(std::shared_ptr<Runtime> r, BackendConfig<UdpConfig> c);

        void open(Completion cb) override;

        void pump();

        void write(std::string data, Completion cb) override;

        void close() override;

        void close_input(Completion cb) override;
    };
}
