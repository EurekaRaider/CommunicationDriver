#include "execution/text_codec.hpp"
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#else
#include <iconv.h>
#endif

namespace cd
{
    static void require(bool condition, const char* message)
    {
        if (!condition)
            throw Fault(CD_INVALID_ARGUMENT, message);
    }

    std::string copy(cd_string_view_t v)
    {
        require(v.size <= SIZE_MAX && (v.data || v.size == 0), "Invalid string view");
        return v.size ? std::string(v.data, static_cast<size_t>(v.size)) : std::string();
    }

    std::string copy(cd_bytes_view_t v)
    {
        return copy({reinterpret_cast<const char*>(v.data), v.size});
    }

    cd_string_view_t view(const std::string& s)
    {
        return {s.data(), s.size()};
    }

    cd_bytes_view_t bytes_view(const std::string& s)
    {
        return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
    }

    // Returns a valid UTF-8 prefix. Incomplete terminal scalars stay buffered until the next block.
    size_t valid_utf8(const std::string& s, bool final, bool partial)
    {
        size_t i = 0;

        while (i < s.size())
        {
            unsigned char c = s[i];
            size_t n =
                c < 0x80 ? 1
                         : (c >= 0xc2 && c <= 0xdf
                                ? 2
                                : (c >= 0xe0 && c <= 0xef ? 3 : (c >= 0xf0 && c <= 0xf4 ? 4 : 0)));

            if (!n)
            {
                if (partial)
                    return i;
                throw Fault(CD_ENCODING_ERROR, "Invalid UTF-8 leading byte");
            }

            for (size_t j = 1; j < std::min(n, s.size() - i); ++j)
                if ((static_cast<unsigned char>(s[i + j]) & 0xc0) != 0x80)
                {
                    if (partial)
                        return i;
                    throw Fault(CD_ENCODING_ERROR, "Invalid UTF-8 continuation");
                }

            if (i + 1 < s.size() && n == 3 &&
                ((c == 0xe0 && static_cast<unsigned char>(s[i + 1]) < 0xa0) ||
                 (c == 0xed && static_cast<unsigned char>(s[i + 1]) >= 0xa0)))
            {
                if (partial)
                    return i;
                throw Fault(CD_ENCODING_ERROR, "Invalid UTF-8 scalar");
            }

            if (i + 1 < s.size() && n == 4 &&
                ((c == 0xf0 && static_cast<unsigned char>(s[i + 1]) < 0x90) ||
                 (c == 0xf4 && static_cast<unsigned char>(s[i + 1]) >= 0x90)))
            {
                if (partial)
                    return i;
                throw Fault(CD_ENCODING_ERROR, "Invalid UTF-8 scalar");
            }

            if (s.size() - i < n)
            {
                if (final)
                    throw Fault(CD_ENCODING_ERROR, "Incomplete UTF-8 scalar");
                return i;
            }

            i += n;
        }

        return i;
    }

    size_t gb18030_prefix(const std::string& s, bool final, bool partial)
    {
        size_t i = 0;

        while (i < s.size())
        {
            auto at = [&](size_t n)
            {
                return static_cast<unsigned char>(s[n]);
            };

            if (at(i) < 128)
            {
                ++i;
                continue;
            }

            bool valid = at(i) >= 0x81 && at(i) <= 0xfe;
            size_t n = 2;

            if (valid && i + 1 < s.size())
            {
                if (at(i + 1) >= 0x30 && at(i + 1) <= 0x39)
                {
                    n = 4;
                    if (i + 2 < s.size())
                        valid = at(i + 2) >= 0x81 && at(i + 2) <= 0xfe;
                    if (valid && i + 3 < s.size())
                        valid = at(i + 3) >= 0x30 && at(i + 3) <= 0x39;
                }
                else
                    valid = at(i + 1) >= 0x40 && at(i + 1) <= 0xfe && at(i + 1) != 0x7f;
            }

            if (!valid)
            {
                if (partial)
                    return i;
                throw Fault(CD_ENCODING_ERROR, "Invalid GB18030 sequence");
            }

            if (s.size() - i < n)
            {
                if (final)
                    throw Fault(CD_ENCODING_ERROR, "Incomplete GB18030 scalar");
                return i;
            }

            i += n;
        }

        return i;
    }

    size_t scalar_prefix(const std::string& s, uint64_t count)
    {
        size_t i = 0;
        uint64_t n = 0;

        while (i < s.size() && n < count)
        {
            unsigned char c = s[i];
            i += c < 0x80 ? 1 : (c < 0xe0 ? 2 : (c < 0xf0 ? 3 : 4));
            ++n;
        }

        return n == count ? i : std::string::npos;
    }

    uint64_t scalar_count(const std::string& s)
    {
        uint64_t n = 0;
        for (unsigned char c : s)
            if ((c & 0xc0) != 0x80)
                ++n;
        return n;
    }
#ifndef _WIN32
    static std::string convert(const std::string& input, const char* to, const char* from,
                               bool final)
    {
        iconv_t converter = iconv_open(to, from);

        if (converter == reinterpret_cast<iconv_t>(-1))
            throw Fault(CD_ENCODING_ERROR, "Encoding converter unavailable", errno);

        std::string result(input.size() * 4 + 16, '\0');
        char* source = const_cast<char*>(input.data());
        size_t source_size = input.size();
        char* target = result.data();
        size_t space = result.size();
        size_t rc = iconv(converter, &source, &source_size, &target, &space);
        int error = errno;
        iconv_close(converter);

        if (rc == static_cast<size_t>(-1) && !(error == EINVAL && !final))
            throw Fault(CD_ENCODING_ERROR, "Invalid encoding sequence", error);

        result.resize(result.size() - space);

        return result;
    }
#endif
    std::string decode(const std::string& s, cd_encoding_t encoding, bool final)
    {
        if (encoding == CD_ENCODING_UTF8 || encoding == CD_ENCODING_DEFAULT)
            return s.substr(0, valid_utf8(s, final));

        if (encoding == CD_ENCODING_ASCII)
        {
            for (unsigned char c : s)
                if (c > 127)
                    throw Fault(CD_ENCODING_ERROR, "Non-ASCII byte");
            return s;
        }

        if (encoding != CD_ENCODING_GB18030)
            throw Fault(CD_INVALID_ARGUMENT, "Unknown encoding");

        size_t prefix = gb18030_prefix(s, final);

        if (!prefix)
            return {};

#ifdef _WIN32
        int n = MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(prefix),
                                    nullptr, 0);
        if (!n)
            throw Fault(CD_ENCODING_ERROR, "Invalid GB18030 sequence", GetLastError());
        std::wstring w(n, 0);
        MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(prefix),
                            w.data(), n);
        int m = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), n, nullptr, 0, nullptr,
                                    nullptr);
        std::string out(m, 0);
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), n, out.data(), m, nullptr,
                            nullptr);
        return out;
#else
        return convert(s.substr(0, prefix), "UTF-8", "GB18030", true);
#endif
    }

    std::string partial_text(const std::string& raw, cd_encoding_t encoding)
    {
        size_t prefix = 0;

        if (encoding == CD_ENCODING_GB18030)
            prefix = gb18030_prefix(raw, false, true);
        else if (encoding == CD_ENCODING_ASCII)
        {
            while (prefix < raw.size() && static_cast<unsigned char>(raw[prefix]) < 128)
                ++prefix;
        }
        else
            prefix = valid_utf8(raw, false, true);

        return decode(raw.substr(0, prefix), encoding, false);
    }

    std::string encode(const std::string& s, cd_encoding_t encoding)
    {
        valid_utf8(s, true);

        if (encoding == CD_ENCODING_UTF8 || encoding == CD_ENCODING_DEFAULT)
            return s;
        if (encoding == CD_ENCODING_ASCII)
            return decode(s, encoding);
        if (encoding != CD_ENCODING_GB18030)
            throw Fault(CD_INVALID_ARGUMENT, "Unknown encoding");

#ifdef _WIN32
        if (s.empty())
            return {};
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                    static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                            w.data(), n);
        int m = WideCharToMultiByte(54936, WC_ERR_INVALID_CHARS, w.data(), n, nullptr, 0, nullptr,
                                    nullptr);
        if (!m)
            throw Fault(CD_ENCODING_ERROR, "GB18030 conversion failed", GetLastError());
        std::string out(m, 0);
        WideCharToMultiByte(54936, WC_ERR_INVALID_CHARS, w.data(), n, out.data(), m, nullptr,
                            nullptr);
        return out;
#else
        return convert(s, "GB18030", "UTF-8", true);
#endif
    }

    void DecodedText::append(std::string_view raw, cd_encoding_t encoding)
    {
        if (raw.size() < consumed)
        {
            consumed = 0;
            characters = 0;
            text.clear();
        }

        if (raw.size() == consumed)
            return;

        auto tail = std::string(raw.substr(consumed));
        auto next = decode(tail, encoding, false);
        consumed += encoding == CD_ENCODING_GB18030 ? gb18030_prefix(tail, false) : next.size();
        characters += scalar_count(next);
        text += next;
    }

    std::filesystem::path utf8_path(const std::string& s)
    {
        return std::filesystem::path(std::u8string(s.begin(), s.end()));
    }
}
