#include "execution/check.hpp"
#include "execution/text_codec.hpp"
#include "core/result.hpp"

namespace cd
{
    static void require(bool condition, const char* message)
    {
        if (!condition)
            throw Fault(CD_INVALID_ARGUMENT, message);
    }

    std::shared_ptr<RE2> compile_regex(const std::string& pattern)
    {
        auto re = std::make_shared<RE2>(pattern, RE2::Quiet);
        if (!re->ok())
            throw Fault(CD_INVALID_ARGUMENT, "Invalid RE2 pattern: " + re->error());
        return re;
    }

    Check::Check(const cd_check_t* c)
    {
        if (!c)
            return;

        require(c->struct_size >= sizeof(*c), "Check struct_size too small");
        kind = c->kind;
        text = c->is_text != 0;
        require(kind >= CD_CHECK_NONE && kind <= CD_CHECK_REGEX, "Invalid check kind");
        value = text ? copy(c->text) : copy(c->bytes);

        if (text)
            valid_utf8(value, true);

        if (kind == CD_CHECK_REGEX)
        {
            require(text, "Regex requires text");
            regex = compile_regex(value);
        }
    }

    void Result::evaluate(const Check& c, int stream, cd_encoding_t encoding, bool final)
    {
        if (c.kind == CD_CHECK_NONE)
            return;

        if (c.text)
        {
            auto decoded = decode(raw[stream], encoding, final);
            evaluate_data(c, decoded);
        }
        else
            evaluate_data(c, raw[stream]);
    }

    void Result::evaluate_data(const Check& c, std::string_view data, size_t start)
    {
        if (c.kind == CD_CHECK_NONE)
            return;

        check_present = true;
        matched_is_text = c.text;
        matched.clear();
        groups.clear();
        failure.clear();

        size_t pos = std::string::npos;

        if (c.kind == CD_CHECK_CONTAINS)
        {
            pos = data.find(c.value, start);
            passed = pos != std::string::npos;
            if (passed)
                matched = c.value;
        }
        else if (c.kind == CD_CHECK_EQUALS)
        {
            passed = data == c.value;
            if (passed)
                matched = data;
        }
        else
        {
            std::vector<absl::string_view> captures(
                static_cast<size_t>(c.regex->NumberOfCapturingGroups()) + 1);
            passed = c.regex->Match({data.data(), data.size()}, 0, data.size(), RE2::UNANCHORED,
                captures.data(), static_cast<int>(captures.size()));

            if (passed)
            {
                matched = std::string(captures[0]);
                for (size_t i = 1; i < captures.size(); ++i)
                    groups.emplace_back(captures[i]);
            }
        }

        if (!passed)
            failure = "Response did not match";
    }
}
