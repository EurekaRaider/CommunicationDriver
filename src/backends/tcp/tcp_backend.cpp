#include "backends/tcp/tcp_backend.hpp"

namespace cd
{
    TcpBackend::TcpBackend(std::shared_ptr<Runtime> r, BackendConfig<TcpConfig> c)
        : Backend(r), config(std::move(c)), tcp(r->state->io), tcp_resolver(r->state->io)
    {
    }

    void TcpBackend::open(Completion cb)
    {
        auto self = std::static_pointer_cast<TcpBackend>(shared_from_this());
        tcp_resolver.async_resolve(config.host, std::to_string(config.port),
            [self, cb](ErrorCode ec, asio::ip::tcp::resolver::results_type endpoints)
            {
                if (self->stopped)
                    return;

                if (ec)
                {
                    cb(CD_CONNECTION_FAILED, ec.message(), ec.value());
                    return;
                }

                asio::async_connect(self->tcp, endpoints,
                    [self, cb](ErrorCode e, const auto&)
                    {
                        if (e)
                            cb(CD_CONNECTION_FAILED, e.message(), e.value());
                        else
                        {
                            self->tcp.set_option(asio::ip::tcp::no_delay(true));
                            cb(CD_OK, {}, 0);
                            self->pump();
                        }
                    });
            });
    }

    void TcpBackend::pump()
    {
        if (stopped)
            return;

        auto self = std::static_pointer_cast<TcpBackend>(shared_from_this());

        auto cb = [self](ErrorCode ec, size_t n)
        {
            if (self->stopped)
                return;

            if (n)
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

        tcp.async_read_some(asio::buffer(buffer), std::move(cb));
    }

    void TcpBackend::write(std::string data, Completion cb)
    {
        auto bytes = std::make_shared<std::string>(std::move(data));
        auto self = shared_from_this();

        auto complete = [bytes, self, cb](ErrorCode ec, size_t)
        {
            cb(ec ? CD_IO_ERROR : CD_OK, ec ? ec.message() : "", ec.value());
        };

        asio::async_write(tcp, asio::buffer(*bytes), std::move(complete));
    }

    void TcpBackend::close()
    {
        stopped = true;
        tcp_resolver.cancel();
        ErrorCode e;
        tcp.close(e);
    }

    void TcpBackend::close_input(Completion cb)
    {
        ErrorCode ec;
        tcp.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
        cb(ec ? CD_IO_ERROR : CD_OK, ec.message(), ec.value());
    }
}
