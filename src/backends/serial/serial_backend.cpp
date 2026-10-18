#include "backends/serial/serial_backend.hpp"

namespace cd
{
    SerialBackend::SerialBackend(std::shared_ptr<Runtime> r, BackendConfig<SerialConfig> c)
        : Backend(r), config(std::move(c)), serial(r->state->io)
    {
    }

    void SerialBackend::open(Completion cb)
    {
        auto self = std::static_pointer_cast<SerialBackend>(shared_from_this());
        ErrorCode e;
        serial.open(config.port_name, e);

        if (!e)
            serial.set_option(asio::serial_port_base::baud_rate(config.baud), e);

        if (!e)
            serial.set_option(asio::serial_port_base::character_size(config.bits), e);

        if (!e)
            serial.set_option(asio::serial_port_base::parity(
                                  static_cast<asio::serial_port_base::parity::type>(config.parity)),
                e);

        if (!e)
            serial.set_option(
                asio::serial_port_base::stop_bits(
                    static_cast<asio::serial_port_base::stop_bits::type>(config.stop)),
                e);

        if (!e)
            serial.set_option(
                asio::serial_port_base::flow_control(
                    static_cast<asio::serial_port_base::flow_control::type>(config.flow)),
                e);

        if (e)
            cb(e == asio::error::access_denied ? CD_PERMISSION_DENIED : CD_CONNECTION_FAILED,
                e.message(), e.value());
        else
        {
            cb(CD_OK, {}, 0);
            pump();
        }
    }

    void SerialBackend::pump()
    {
        if (stopped)
            return;

        auto self = std::static_pointer_cast<SerialBackend>(shared_from_this());

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

        serial.async_read_some(asio::buffer(buffer), std::move(cb));
    }

    void SerialBackend::write(std::string data, Completion cb)
    {
        auto bytes = std::make_shared<std::string>(std::move(data));
        auto self = shared_from_this();

        auto complete = [bytes, self, cb](ErrorCode ec, size_t)
        {
            cb(ec ? CD_IO_ERROR : CD_OK, ec ? ec.message() : "", ec.value());
        };

        asio::async_write(serial, asio::buffer(*bytes), std::move(complete));
    }

    void SerialBackend::close()
    {
        stopped = true;
        ErrorCode e;
        serial.close(e);
    }

    void SerialBackend::close_input(Completion cb)
    {
        cb(CD_NOT_SUPPORTED, "Half-close unavailable", 0);
    }
}
