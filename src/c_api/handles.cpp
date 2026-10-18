#include "c_api/common.hpp"

using namespace cd;
using namespace cd::abi;

cd_driver::~cd_driver()
{
    value->close(
        []
        {
        });
}

cd_connection::~cd_connection()
{
    auto c = value;
    asio::post(c->runtime->state->io,
        [c]
        {
            c->force_close();
        });
}

cd_result::cd_result(std::shared_ptr<Result> r) : CdHandle(std::move(r))
{
    for (auto& g : value->groups)
        group_views.push_back(view(g));
    for (auto& item : value->items)
        item_handles.push_back(item.result ? new cd_result(item.result) : nullptr);
}

cd_result::~cd_result()
{
    for (auto* item : item_handles)
        release(item);
}

namespace cd
{
    std::shared_ptr<Cancellation> cancellation(cd_cancel_source_t* c)
    {
        return c ? c->value : nullptr;
    }
}

extern "C"
{
#define LIFETIME(name)                                                                             \
    void CD_CALL cd_##name##_retain(cd_##name##_t* h)                                              \
    {                                                                                              \
        retain(h);                                                                                 \
    }                                                                                              \
    void CD_CALL cd_##name##_release(cd_##name##_t* h)                                             \
    {                                                                                              \
        release(h);                                                                                \
    }
    LIFETIME(driver)
    LIFETIME(connection)
    LIFETIME(operation)
    LIFETIME(result)
    LIFETIME(cancel_source)
#undef LIFETIME
}
