#pragma once
#include "core/common.hpp"

namespace cd
{
    // A request owns its decoder state; only complete scalars advance the wire offset.
    struct DecodedText
    {
        size_t consumed = 0;
        uint64_t characters = 0;
        std::string text;
        void append(std::string_view, cd_encoding_t);
    };

    size_t valid_utf8(const std::string&, bool final, bool partial = false);
    size_t gb18030_prefix(const std::string&, bool final, bool partial = false);
    std::string partial_text(const std::string&, cd_encoding_t);
}
