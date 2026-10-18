#include "c_api/common.hpp"

using namespace cd;
using namespace cd::abi;

extern "C"
{
    cd_status_t CD_CALL cd_response_checker_is_match_string(
        cd_string_view_t response, const cd_check_t* check, cd_result_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            require(out && check, "Check and result required");

            auto r = std::make_shared<Result>();
            r->kind = CD_RESULT_CHECK;
            r->raw[0] = copy(response);
            r->evaluate(Check(check), 0, CD_ENCODING_UTF8, true);

            *out = new cd_result(r);
            return CD_OK;
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_response_checker_is_match_bytes(
        cd_bytes_view_t response, const cd_check_t* check, cd_result_t** out)
    {
        return cd_response_checker_is_match_string(
            {reinterpret_cast<const char*>(response.data), response.size}, check, out);
    }
}
