#include "backends/udp/udp_backend.hpp"

namespace cd
{
    UdpBackend::UdpBackend(std::shared_ptr<Runtime> r, BackendConfig<UdpConfig> c)
        : Backend(r), config(std::move(c)), udp(r->state->io), udp_resolver(r->state->io)
    {
    }

    void UdpBackend::open(Completion cb)
    {
        auto self = std::static_pointer_cast<UdpBackend>(shared_from_this());
        udp_resolver.async_resolve(config.host, std::to_string(config.port),
            [self, cb](ErrorCode ec, asio::ip::udp::resolver::results_type eps)
            {
                if (self->stopped)
                    return;

                if (ec)
                {
                    cb(CD_CONNECTION_FAILED, ec.message(), ec.value());
                    return;
                }

                ErrorCode e;
                auto endpoint = eps.begin()->endpoint();
                self->udp.open(endpoint.protocol(), e);

                if (!e)
                {
                    auto address = self->config.local_host.empty()
                                       ? (endpoint.address().is_v4()
                                                 ? asio::ip::address(asio::ip::address_v4::any())
                                                 : asio::ip::address(asio::ip::address_v6::any()))
                                       : asio::ip::make_address(self->config.local_host, e);
                    if (!e)
                        self->udp.bind({address, self->config.local_port}, e);
                }

                if (!e)
                    self->udp.connect(endpoint, e);

                if (e)
                    cb(CD_CONNECTION_FAILED, e.message(), e.value());
                else
                {
                    cb(CD_OK, {}, 0);
                    self->pump();
                }
            });
    }

    void UdpBackend::pump()
    {
        if (stopped)
            return;

        auto self = std::static_pointer_cast<UdpBackend>(shared_from_this());

        auto cb = [self](ErrorCode ec, size_t n)
        {
            if (self->stopped)
                return;

            if (n || (!ec))
                self->output(0, std::string(self->buffer.data(), n), false);

            if (ec)
            {
                self->output(0, {}, true);
                if (ec != asio::error::eof && ec != asio::error::operation_aborted)
                    self->failure(CD_DISCONNECTED, ec.message(), ec.value());
            }
            else
                self->pump();
        };

        udp.async_receive(asio::buffer(buffer), std::move(cb));
    }

    void UdpBackend::write(std::string data, Completion cb)
    {
        if (data.size() > 65507)
        {
            cb(CD_INVALID_ARGUMENT, "UDP datagram exceeds 65507 bytes", 0);
            return;
        }

        auto bytes = std::make_shared<std::string>(std::move(data));
        auto self = shared_from_this();

        auto complete = [bytes, self, cb](ErrorCode ec, size_t)
        {
            cb(ec ? CD_IO_ERROR : CD_OK, ec ? ec.message() : "", ec.value());
        };

        udp.async_send(asio::buffer(*bytes), std::move(complete));
    }

    void UdpBackend::close()
    {
        stopped = true;
        udp_resolver.cancel();
        ErrorCode e;
        udp.close(e);
    }

    void UdpBackend::close_input(Completion cb)
    {
        cb(CD_NOT_SUPPORTED, "Half-close unavailable", 0);
    }
}
