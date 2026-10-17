#include "core/options.hpp"
#include "execution/text_codec.hpp"

namespace cd
{
    static void require(bool condition, const char* message)
    {
        if (!condition)
            throw Fault(CD_INVALID_ARGUMENT, message);
    }

    Defaults::Defaults(const cd_connection_options_t& d)
    {
        require(d.struct_size >= sizeof(d), "Defaults struct_size too small");
        encoding = d.encoding == CD_ENCODING_DEFAULT ? CD_ENCODING_UTF8 : d.encoding;
        require(encoding >= CD_ENCODING_UTF8 && encoding <= CD_ENCODING_GB18030,
                "Invalid encoding");
        newline = copy(d.new_line);
        require(!newline.empty(), "NewLine cannot be empty");
        valid_utf8(newline, true);
        rule = Rule(d.receive_until);
        connect_ms = d.connect_timeout_ms;
        io_ms = d.io_timeout_ms;
        execution_ms = d.execution_timeout_ms;
        close_ms = d.close_timeout_ms;
        max_bytes = d.max_response_bytes;
        require(connect_ms && io_ms && execution_ms && close_ms && max_bytes,
                "Timeouts and buffer limit must be positive");
        require(connect_ms <= max_timeout_ms && io_ms <= max_timeout_ms &&
                    execution_ms <= max_timeout_ms && close_ms <= max_timeout_ms &&
                    max_bytes <= SIZE_MAX,
                "Timeout or buffer limit is out of range");
    }

    Options::Options(const Defaults& d, const cd_call_options_t* o, uint64_t fallback)
        : encoding(d.encoding), newline(d.newline), rule(d.rule), timeout(fallback),
          max_bytes(d.max_bytes)
    {
        if (!o)
            return;

        require(o->struct_size >= sizeof(*o), "CallOptions struct_size too small");

        if (o->encoding != CD_ENCODING_DEFAULT)
            encoding = o->encoding;

        require(encoding >= CD_ENCODING_UTF8 && encoding <= CD_ENCODING_GB18030,
                "Invalid encoding");

        if (o->timeout_ms)
            timeout = o->timeout_ms;

        if (o->max_response_bytes)
            max_bytes = o->max_response_bytes;

        require(timeout <= max_timeout_ms && max_bytes <= SIZE_MAX,
                "Timeout or buffer limit is out of range");

        if (o->new_line.size)
            newline = copy(o->new_line);

        valid_utf8(newline, true);

        if (o->receive_until.kind != CD_RECEIVE_NONE)
            rule = Rule(o->receive_until);

        read_stream = o->read_stream;
        check_stream = o->check_stream;
        require(read_stream >= 0 && read_stream <= 1 && check_stream >= 0 && check_stream <= 1,
                "Invalid stream");
        cancellation = cd::cancellation(o->cancellation);
    }
}
