#include "execution/receive_rule.hpp"
#include "execution/text_codec.hpp"
#include "core/connection.hpp"
#include <algorithm>

namespace cd
{
    static void require(bool condition, const char* message)
    {
        if (!condition)
            throw Fault(CD_INVALID_ARGUMENT, message);
    }

    Rule::Rule(const cd_receive_until_t& r)
        : kind(r.kind), count(r.count), idle_ms(r.idle_ms), text(r.is_text != 0),
          include(r.include_delimiter != 0), value(r.is_text ? copy(r.text) : copy(r.bytes))
    {
        require(r.struct_size >= sizeof(r), "ReceiveUntil struct_size too small");
        require(kind >= CD_RECEIVE_NONE && kind <= CD_RECEIVE_AVAILABLE, "Invalid receive kind");

        if (kind == CD_RECEIVE_BYTES || kind == CD_RECEIVE_CHARACTERS)
            require(count > 0 && count <= SIZE_MAX, "Receive count must be positive");

        if (kind == CD_RECEIVE_DELIMITER)
            require(!value.empty(), "Delimiter cannot be empty");

        if (kind == CD_RECEIVE_IDLE)
            require(idle_ms > 0 && idle_ms <= max_timeout_ms, "Idle must fit a positive duration");

        if (text)
            valid_utf8(value, true);

        if (kind == CD_RECEIVE_PATTERN)
            regex = compile_regex(value);
    }

    bool Connection::boundary(std::string& data, Request& request, size_t& consume, size_t& keep)
    {
        const auto& options = request.options;
        const auto& rule = options.rule;
        consume = keep = 0;

        switch (rule.kind)
        {
            case CD_RECEIVE_BYTES:
                if (data.size() >= rule.count)
                {
                    consume = keep = static_cast<size_t>(rule.count);
                    return true;
                }
                return false;
            case CD_RECEIVE_CHARACTERS:
            {
                auto& decoded = request.boundary_text;
                decoded.append(data, options.encoding);

                if (decoded.characters < rule.count)
                    return false;

                auto pos = scalar_prefix(decoded.text, rule.count);
                consume = keep = encode(decoded.text.substr(0, pos), options.encoding).size();

                return true;
            }
            case CD_RECEIVE_DELIMITER:
            {
                if (request.delimiter.empty())
                    request.delimiter =
                        rule.text ? encode(rule.value, options.encoding) : rule.value;

                const auto& delimiter = request.delimiter;
                // Only the previous suffix can start a delimiter spanning the new block.
                size_t overlap = delimiter.size() - 1;
                size_t start =
                    request.delimiter_scanned > overlap ? request.delimiter_scanned - overlap : 0;
                auto pos = data.find(delimiter, start);
                request.delimiter_scanned = data.size();

                if (pos == std::string::npos)
                    return false;

                consume = pos + delimiter.size();
                keep = rule.include ? consume : pos;

                return true;
            }
            case CD_RECEIVE_PATTERN:
            {
                request.boundary_text.append(data, options.encoding);

                const auto& text = request.boundary_text.text;
                absl::string_view match;

                if (!rule.regex->Match(text, 0, text.size(), RE2::UNANCHORED, &match, 1))
                    return false;

                size_t end = static_cast<size_t>(match.data() - text.data()) + match.size();
                consume = keep = encode(text.substr(0, end), options.encoding).size();

                return true;
            }
            case CD_RECEIVE_AVAILABLE:
                if (!data.empty())
                {
                    consume = keep = data.size();
                    return true;
                }
                return false;
            case CD_RECEIVE_MESSAGE:
            {
                auto& sizes = messages[options.read_stream];

                if (sizes.empty())
                    return false;

                consume = keep = sizes.front();

                return true;
            }
            default:
                return false;
        }
    }
}
