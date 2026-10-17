#pragma once
#include "types.hpp"

namespace comm
{
    class Check
    {
        int kind_ = 0;
        bool text_ = true;
        std::string text_value_;
        Bytes byte_value_;

        Check(int kind, std::string_view s) : kind_(kind), text_value_(s)
        {
        }

        Check(int kind, std::span<const std::byte> b)
            : kind_(kind), text_(false), byte_value_(b.begin(), b.end())
        {
        }

      public:
        static Check contains(std::string_view s)
        {
            return {CD_CHECK_CONTAINS, s};
        }

        static Check contains(std::span<const std::byte> b)
        {
            return {CD_CHECK_CONTAINS, b};
        }

        static Check equals(std::string_view s)
        {
            return {CD_CHECK_EQUALS, s};
        }

        static Check equals(std::span<const std::byte> b)
        {
            return {CD_CHECK_EQUALS, b};
        }

        static Check regex(std::string_view s)
        {
            return {CD_CHECK_REGEX, s};
        }

        cd_check_t native() const
        {
            return {sizeof(cd_check_t),       kind_, text_, 0, detail::view(text_value_),
                    detail::view(byte_value_)};
        }
    };

    class ReceiveUntil
    {
        cd_receive_until_t rule_{};
        std::string text_;
        Bytes bytes_;

        explicit ReceiveUntil(cd_receive_until_t rule) : rule_(rule)
        {
        }

      public:
        ReceiveUntil()
        {
            rule_.struct_size = sizeof(rule_);
        }

        static ReceiveUntil bytes(uint64_t count)
        {
            return ReceiveUntil(cd_receive_until_bytes(count));
        }

        static ReceiveUntil characters(uint64_t count)
        {
            return ReceiveUntil(cd_receive_until_characters(count));
        }

        static ReceiveUntil delimiter(std::string_view s, bool include = false)
        {
            ReceiveUntil r(cd_receive_until_delimiter_string({}, include));
            r.text_ = s;
            return r;
        }

        static ReceiveUntil delimiter(std::span<const std::byte> b, bool include = false)
        {
            ReceiveUntil r(cd_receive_until_delimiter_bytes({}, include));
            r.bytes_ = {b.begin(), b.end()};
            return r;
        }

        static ReceiveUntil line(std::string_view s = "\n")
        {
            return delimiter(s);
        }

        static ReceiveUntil pattern(std::string_view s)
        {
            ReceiveUntil r(cd_receive_until_pattern({}));
            r.text_ = s;
            return r;
        }

        static ReceiveUntil idle(Duration d)
        {
            return ReceiveUntil(cd_receive_until_idle(static_cast<uint64_t>(d.count())));
        }

        static ReceiveUntil message()
        {
            return ReceiveUntil(cd_receive_until_message());
        }

        static ReceiveUntil available()
        {
            return ReceiveUntil(cd_receive_until_available());
        }

        cd_receive_until_t native() const
        {
            auto r = rule_;
            r.text = detail::view(text_);
            r.bytes = detail::view(bytes_);

            return r;
        }
    };
}
