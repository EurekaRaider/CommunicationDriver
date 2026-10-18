#include "c_api/common.hpp"
#include <algorithm>

using namespace cd;
using namespace cd::abi;

extern "C"
{
    cd_status_t CD_CALL cd_result_get_status(const cd_result_t* r)
    {
        return r ? r->value->status : CD_INVALID_ARGUMENT;
    }

    cd_result_kind_t CD_CALL cd_result_get_kind(const cd_result_t* r)
    {
        return r ? r->value->kind : CD_RESULT_VOID;
    }

#define GET_START(type)                                                                            \
    if (!h || !out || out->struct_size < sizeof(type))                                             \
        return CD_INVALID_ARGUMENT;                                                                \
    auto& r = *h->value;                                                                           \
    *out = {};                                                                                     \
    out->struct_size = sizeof(type)

    cd_status_t CD_CALL cd_result_get_error(const cd_result_t* h, cd_error_info_t* out)
    {
        GET_START(cd_error_info_t);
        out->code = r.status;
        out->message = view(r.message);
        out->backend = view(r.backend);
        out->native_code = r.native;
        out->partial_text = view(r.text[r.partial_stream]);
        out->partial_bytes = bytes_view(r.raw[r.partial_stream]);
        out->confirmed_progress = r.progress.value_or(0);
        out->has_confirmed_progress = r.progress.has_value();
        out->partial_file = view(r.partial_file);

        return CD_OK;
    }

    cd_status_t CD_CALL cd_result_get_text(
        const cd_result_t* h, cd_stream_t stream, cd_string_view_t* out)
    {
        if (!h || !out || stream < 0 || stream > 1)
            return CD_INVALID_ARGUMENT;
        *out = view(h->value->text[stream]);
        return CD_OK;
    }

    cd_status_t CD_CALL cd_result_get_bytes(
        const cd_result_t* h, cd_stream_t stream, cd_bytes_view_t* out)
    {
        if (!h || !out || stream < 0 || stream > 1)
            return CD_INVALID_ARGUMENT;
        *out = bytes_view(h->value->raw[stream]);
        return CD_OK;
    }

    cd_status_t CD_CALL cd_result_get_check(const cd_result_t* h, cd_check_result_t* out)
    {
        GET_START(cd_check_result_t);
        out->has_value = r.check_present;
        out->passed = r.passed;

        if (r.check_present && r.passed)
        {
            if (r.matched_is_text)
                out->matched_text = view(r.matched);
            else
                out->matched_bytes = bytes_view(r.matched);
        }

        out->groups = h->group_views.data();
        out->group_count = h->group_views.size();
        out->failure_reason = view(r.failure);

        return CD_OK;
    }

    cd_status_t CD_CALL cd_result_get_execution_info(const cd_result_t* h, cd_execution_info_t* out)
    {
        GET_START(cd_execution_info_t);
        out->completion_reason = r.completion;
        out->has_exit_code = r.exit_code.has_value();
        out->exit_code = r.exit_code.value_or(0);
        out->elapsed_ns = r.elapsed_ns;

        return CD_OK;
    }

    cd_status_t CD_CALL cd_result_get_transfer(const cd_result_t* h, cd_transfer_result_t* out)
    {
        GET_START(cd_transfer_result_t);

        if (r.kind != CD_RESULT_TRANSFER)
            return CD_NOT_SUPPORTED;

        out->source = view(r.source);
        out->destination = view(r.destination);
        out->bytes_transferred = r.progress.value_or(0);
        out->has_bytes_transferred = r.progress.has_value();
        out->elapsed_ns = r.elapsed_ns;

        return CD_OK;
    }

    cd_status_t CD_CALL cd_result_get_batch_info(const cd_result_t* h, cd_batch_info_t* out)
    {
        GET_START(cd_batch_info_t);

        if (r.kind != CD_RESULT_BATCH)
            return CD_NOT_SUPPORTED;

        out->all_succeeded = std::all_of(r.items.begin(), r.items.end(),
            [](const BatchEntry& e)
            {
                return e.result && e.result->succeeded();
            });
        out->was_cancelled = r.was_cancelled;
        out->item_count = r.items.size();
        out->elapsed_ns = r.elapsed_ns;

        return CD_OK;
    }

    cd_status_t CD_CALL cd_result_get_batch_item(
        const cd_result_t* h, uint64_t index, cd_batch_item_result_t* out)
    {
        GET_START(cd_batch_item_result_t);

        if (index >= r.items.size())
            return CD_INVALID_ARGUMENT;

        auto& entry = r.items[static_cast<size_t>(index)];
        out->key = view(entry.key);
        out->state = entry.state;
        out->result = h->item_handles[static_cast<size_t>(index)];
        out->succeeded = entry.result && entry.result->succeeded();
        out->elapsed_ns = entry.result ? entry.result->elapsed_ns : 0;

        return CD_OK;
    }

#undef GET_START
}
