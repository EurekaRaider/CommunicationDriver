#include "c_api/common.hpp"

using namespace cd;
using namespace cd::abi;

extern "C"
{
    uint32_t CD_CALL cd_abi_version(void)
    {
        return CD_ABI_VERSION;
    }

    const char* CD_CALL cd_status_name(cd_status_t s)
    {
        static const char* names[] = {"OK", "InvalidArgument", "NotSupported", "NotOpen", "Closed",
            "Timeout", "Cancelled", "ConnectionFailed", "Disconnected", "EndOfStream",
            "AuthenticationFailed", "HostKeyRejected", "DeviceUnavailable", "PermissionDenied",
            "IOError", "ProtocolError", "EncodingError", "LimitExceeded", "Busy", "NeedsRecovery",
            "InvalidContext", "InternalError"};
        return s >= 0 && s <= CD_INTERNAL_ERROR ? names[s] : "Unknown";
    }

    void CD_CALL cd_connection_options_init(cd_connection_options_t* o)
    {
        if (!o)
            return;

        *o = {};
        o->struct_size = sizeof(*o);
        o->encoding = CD_ENCODING_UTF8;
        o->new_line = CD_SV("\n");
        o->receive_until.struct_size = sizeof(cd_receive_until_t);
        o->connect_timeout_ms = 10000;
        o->io_timeout_ms = 5000;
        o->execution_timeout_ms = 30000;
        o->close_timeout_ms = 10000;
        o->max_response_bytes = 1048576;
    }

#define INIT_BEGIN(name)                                                                           \
    void CD_CALL cd_##name##_options_init(cd_##name##_options_t* o)                                \
    {                                                                                              \
        if (!o)                                                                                    \
            return;                                                                                \
        *o = {};                                                                                   \
        o->struct_size = sizeof(*o)
#define INIT_DEFAULTS(name, extra)                                                                 \
    INIT_BEGIN(name);                                                                              \
    cd_connection_options_init(&o->defaults);                                                      \
    extra;                                                                                         \
    }
    INIT_DEFAULTS(tcp, )
    INIT_DEFAULTS(udp, )
    INIT_DEFAULTS(serial, o->baud_rate = 115200; o->data_bits = 8)
    INIT_DEFAULTS(ssh, o->port = 22)
    INIT_DEFAULTS(adb, o->server_host = CD_SV("127.0.0.1"); o->server_port = 5037)
    INIT_DEFAULTS(cli, )
    INIT_BEGIN(call);
    o->receive_until.struct_size = sizeof(cd_receive_until_t);
}
INIT_BEGIN(transfer);
o->overwrite = 1;
}
INIT_BEGIN(batch);
o->max_concurrency = 32;
}
#undef INIT_BEGIN
#undef INIT_DEFAULTS

static cd_check_t check_value(int kind, bool text, cd_string_view_t s, cd_bytes_view_t b)
{
    return {sizeof(cd_check_t), kind, text, 0, s, b};
}

cd_check_t CD_CALL cd_check_contains_string(cd_string_view_t v)
{
    return check_value(CD_CHECK_CONTAINS, true, v, {});
}

cd_check_t CD_CALL cd_check_contains_bytes(cd_bytes_view_t v)
{
    return check_value(CD_CHECK_CONTAINS, false, {}, v);
}

cd_check_t CD_CALL cd_check_equals_string(cd_string_view_t v)
{
    return check_value(CD_CHECK_EQUALS, true, v, {});
}

cd_check_t CD_CALL cd_check_equals_bytes(cd_bytes_view_t v)
{
    return check_value(CD_CHECK_EQUALS, false, {}, v);
}

cd_check_t CD_CALL cd_check_regex(cd_string_view_t v)
{
    return check_value(CD_CHECK_REGEX, true, v, {});
}

static cd_receive_until_t rule_value(int kind)
{
    cd_receive_until_t r{};
    r.struct_size = sizeof(r);
    r.kind = kind;

    return r;
}

cd_receive_until_t CD_CALL cd_receive_until_bytes(uint64_t count)
{
    auto r = rule_value(CD_RECEIVE_BYTES);
    r.count = count;
    return r;
}

cd_receive_until_t CD_CALL cd_receive_until_characters(uint64_t count)
{
    auto r = rule_value(CD_RECEIVE_CHARACTERS);
    r.count = count;
    return r;
}

cd_receive_until_t CD_CALL cd_receive_until_delimiter_string(cd_string_view_t v, int32_t include)
{
    auto r = rule_value(CD_RECEIVE_DELIMITER);
    r.is_text = 1;
    r.text = v;
    r.include_delimiter = include;

    return r;
}

cd_receive_until_t CD_CALL cd_receive_until_delimiter_bytes(cd_bytes_view_t v, int32_t include)
{
    auto r = rule_value(CD_RECEIVE_DELIMITER);
    r.bytes = v;
    r.include_delimiter = include;

    return r;
}

cd_receive_until_t CD_CALL cd_receive_until_line(cd_string_view_t v)
{
    return cd_receive_until_delimiter_string(v.size ? v : CD_SV("\n"), 0);
}

cd_receive_until_t CD_CALL cd_receive_until_pattern(cd_string_view_t v)
{
    auto r = rule_value(CD_RECEIVE_PATTERN);
    r.is_text = 1;
    r.text = v;

    return r;
}

cd_receive_until_t CD_CALL cd_receive_until_idle(uint64_t interval)
{
    auto r = rule_value(CD_RECEIVE_IDLE);
    r.idle_ms = interval;
    return r;
}

cd_receive_until_t CD_CALL cd_receive_until_message(void)
{
    return rule_value(CD_RECEIVE_MESSAGE);
}

cd_receive_until_t CD_CALL cd_receive_until_available(void)
{
    return rule_value(CD_RECEIVE_AVAILABLE);
}
}
