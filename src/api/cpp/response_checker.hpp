#pragma once
#include "driver.hpp"

namespace comm
{
    class ResponseChecker
    {
      public:
        static bool is_match(std::string_view response, const Check& check)
        {
            auto c = check.native();
            cd_result_t* r = nullptr;
            auto status = cd_response_checker_is_match_string(detail::view(response), &c, &r);
            detail::ResultHandle owner(r);
            detail::checked(status, r);

            return detail::check_result(r)->passed;
        }

        static bool is_match(std::span<const std::byte> response, const Check& check)
        {
            auto c = check.native();
            cd_result_t* r = nullptr;
            auto status = cd_response_checker_is_match_bytes(detail::view(response), &c, &r);
            detail::ResultHandle owner(r);
            detail::checked(status, r);

            return detail::check_result(r)->passed;
        }
    };
}
