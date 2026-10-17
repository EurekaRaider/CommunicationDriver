#pragma once
#include "core/backend.hpp"
#include "backends/tcp/tcp_config.hpp"

namespace cd
{
    class TcpBackend final : public Backend
    {
        BackendConfig<TcpConfig> config;
        asio::ip::tcp::socket tcp;
        asio::ip::tcp::resolver tcp_resolver;
        std::array<char, 65536> buffer{};
        bool stopped = false;

      public:
        TcpBackend(std::shared_ptr<Runtime> r, BackendConfig<TcpConfig> c);

        void open(Completion cb) override;

        void pump();

        void write(std::string data, Completion cb) override;

        void close() override;

        void close_input(Completion cb) override;
    };
}
