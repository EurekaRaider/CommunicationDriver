#pragma once
#include "execution/receive_rule.hpp"
#include "core/cancellation.hpp"

namespace cd
{
    struct Defaults
    {
        cd_encoding_t encoding = CD_ENCODING_UTF8;
        std::string newline = "\n";
        Rule rule;
        uint64_t connect_ms = 10000, io_ms = 5000, execution_ms = 30000, close_ms = 10000,
                 max_bytes = 1048576;
        explicit Defaults(const cd_connection_options_t&);
    };

    struct Options
    {
        cd_encoding_t encoding;
        std::string newline;
        Rule rule;
        uint64_t timeout, max_bytes;
        int read_stream = 0, check_stream = 0;
        std::shared_ptr<Cancellation> cancellation;
        Options(const Defaults&, const cd_call_options_t*, uint64_t fallback);
    };
}
