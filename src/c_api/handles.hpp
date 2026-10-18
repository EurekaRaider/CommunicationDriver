#pragma once
#include "core/runtime.hpp"
#include "core/operation.hpp"
#include "core/connection.hpp"

// These objects are only visible inside the implementation; the ABI stays opaque.
template <class T>
struct CdHandle
{
    std::atomic<uint32_t> refs{1};
    std::shared_ptr<T> value;

    explicit CdHandle(std::shared_ptr<T> v) : value(std::move(v))
    {
    }
};

struct cd_driver : CdHandle<cd::Runtime>
{
    using CdHandle::CdHandle;
    ~cd_driver();
};

struct cd_connection : CdHandle<cd::Connection>
{
    using CdHandle::CdHandle;
    ~cd_connection();
};

struct cd_operation : CdHandle<cd::Operation>
{
    using CdHandle::CdHandle;
};

struct cd_cancel_source : CdHandle<cd::Cancellation>
{
    using CdHandle::CdHandle;
};

struct cd_result : CdHandle<cd::Result>
{
    std::vector<cd_string_view_t> group_views;
    std::vector<cd_result*> item_handles;
    explicit cd_result(std::shared_ptr<cd::Result>);
    ~cd_result();
};
