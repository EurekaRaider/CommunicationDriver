#include "c_api/common.hpp"

using namespace cd;
using namespace cd::abi;

extern "C"
{
    cd_state_t CD_CALL cd_connection_get_state(cd_connection_t* c)
    {
        return c ? c->value->state.load() : CD_STATE_CLOSED;
    }

    cd_capabilities_t CD_CALL cd_connection_get_capabilities(cd_connection_t* c)
    {
        return c ? c->value->capabilities : 0;
    }

    cd_status_t CD_CALL cd_open_async(cd_connection_t* c, const cd_call_options_t* options,
                                      cd_completion_fn cb, void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Open, false, options);
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_open(cd_connection_t* c, const cd_call_options_t* options,
                                cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_open_async(c, options, nullptr, nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_close_async(cd_connection_t* c, const cd_call_options_t* options,
                                       cd_completion_fn cb, void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Close, false, options);
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_close(cd_connection_t* c, const cd_call_options_t* options,
                                 cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_close_async(c, options, nullptr, nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_close_input_async(cd_connection_t* c, const cd_call_options_t* options,
                                             cd_completion_fn cb, void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::CloseInput, false, options);
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_close_input(cd_connection_t* c, const cd_call_options_t* options,
                                       cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_close_input_async(c, options, nullptr, nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_read_bytes_async(cd_connection_t* c, const cd_call_options_t* options,
                                            cd_completion_fn cb, void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Read, false, options);
            r->options.rule = Rule(cd_receive_until_available());
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_read_bytes(cd_connection_t* c, const cd_call_options_t* options,
                                      cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_read_bytes_async(c, options, nullptr, nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_read_string_async(cd_connection_t* c, const cd_call_options_t* options,
                                             cd_completion_fn cb, void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Read, true, options);
            r->options.rule = Rule(cd_receive_until_available());
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_read_string(cd_connection_t* c, const cd_call_options_t* options,
                                       cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_read_string_async(c, options, nullptr, nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_read_line_async(cd_connection_t* c, const cd_call_options_t* options,
                                           cd_completion_fn cb, void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Read, true, options);
            r->options.rule = Rule(cd_receive_until_line(view(r->options.newline)));
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_read_line(cd_connection_t* c, const cd_call_options_t* options,
                                     cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_read_line_async(c, options, nullptr, nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_read_exactly_bytes_async(cd_connection_t* c, uint64_t count,
                                                    const cd_call_options_t* options,
                                                    cd_completion_fn cb, void* user,
                                                    cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Read, false, options);
            r->options.rule = Rule(cd_receive_until_bytes(count));
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_read_exactly_bytes(cd_connection_t* c, uint64_t count,
                                              const cd_call_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_read_exactly_bytes_async(c, count, options, nullptr,
                                                                      nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_read_exactly_string_async(cd_connection_t* c, uint64_t count,
                                                     const cd_call_options_t* options,
                                                     cd_completion_fn cb, void* user,
                                                     cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Read, true, options);
            r->options.rule = Rule(cd_receive_until_characters(count));
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_read_exactly_string(cd_connection_t* c, uint64_t count,
                                               const cd_call_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_read_exactly_string_async(c, count, options, nullptr,
                                                                       nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_read_until_bytes_async(cd_connection_t* c,
                                                  const cd_receive_until_t* until,
                                                  const cd_call_options_t* options,
                                                  cd_completion_fn cb, void* user,
                                                  cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            require(until, "ReceiveUntil required");

            auto r = request(c, Action::Read, false, options);
            r->options.rule = Rule(*until);
            require(r->options.rule.kind != CD_RECEIVE_NONE, "ReceiveUntil required");

            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_read_until_bytes(cd_connection_t* c, const cd_receive_until_t* until,
                                            const cd_call_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_read_until_bytes_async(c, until, options, nullptr,
                                                                    nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_read_until_string_async(cd_connection_t* c,
                                                   const cd_receive_until_t* until,
                                                   const cd_call_options_t* options,
                                                   cd_completion_fn cb, void* user,
                                                   cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            require(until, "ReceiveUntil required");

            auto r = request(c, Action::Read, true, options);
            r->options.rule = Rule(*until);
            require(r->options.rule.kind != CD_RECEIVE_NONE, "ReceiveUntil required");

            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_read_until_string(cd_connection_t* c, const cd_receive_until_t* until,
                                             const cd_call_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_read_until_string_async(c, until, options, nullptr,
                                                                     nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_write_bytes_async(cd_connection_t* c, cd_bytes_view_t input,
                                             const cd_call_options_t* options, cd_completion_fn cb,
                                             void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Write, false, options);
            r->input = copy(input);
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_write_bytes(cd_connection_t* c, cd_bytes_view_t input,
                                       const cd_call_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_write_bytes_async(c, input, options, nullptr, nullptr,
                                                               op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_write_string_async(cd_connection_t* c, cd_string_view_t input,
                                              const cd_call_options_t* options, cd_completion_fn cb,
                                              void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Write, true, options);
            r->input = encode(copy(input), r->options.encoding);
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_write_string(cd_connection_t* c, cd_string_view_t input,
                                        const cd_call_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_write_string_async(c, input, options, nullptr, nullptr,
                                                                op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_write_line_async(cd_connection_t* c, cd_string_view_t input,
                                            const cd_call_options_t* options, cd_completion_fn cb,
                                            void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Write, true, options);
            r->input = encode(copy(input) + r->options.newline, r->options.encoding);
            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_write_line(cd_connection_t* c, cd_string_view_t input,
                                      const cd_call_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_write_line_async(c, input, options, nullptr, nullptr,
                                                              op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_execute_string_async(cd_connection_t* c, cd_string_view_t input,
                                                const cd_check_t* check,
                                                const cd_call_options_t* options,
                                                cd_completion_fn cb, void* user,
                                                cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Execute, true, options);
            r->input = encode(copy(input), r->options.encoding);
            r->check = Check(check);

            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_execute_string(cd_connection_t* c, cd_string_view_t input,
                                          const cd_check_t* check, const cd_call_options_t* options,
                                          cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_execute_string_async(c, input, check, options, nullptr,
                                                                  nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_execute_bytes_async(cd_connection_t* c, cd_bytes_view_t input,
                                               const cd_check_t* check,
                                               const cd_call_options_t* options,
                                               cd_completion_fn cb, void* user,
                                               cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Execute, false, options);
            r->input = copy(input);
            r->check = Check(check);

            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_execute_bytes(cd_connection_t* c, cd_bytes_view_t input,
                                         const cd_check_t* check, const cd_call_options_t* options,
                                         cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_execute_bytes_async(c, input, check, options, nullptr,
                                                                 nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_execute_until_string_async(cd_connection_t* c, cd_string_view_t input,
                                                      const cd_check_t* check,
                                                      const cd_call_options_t* options,
                                                      cd_completion_fn cb, void* user,
                                                      cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::ExecuteUntil, true, options);
            r->input = encode(copy(input), r->options.encoding);
            r->check = Check(check);
            require(r->check.kind == CD_CHECK_CONTAINS || r->check.kind == CD_CHECK_REGEX,
                    "ExecuteUntil requires Contains or Regex");

            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_execute_until_string(cd_connection_t* c, cd_string_view_t input,
                                                const cd_check_t* check,
                                                const cd_call_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_execute_until_string_async(c, input, check, options,
                                                                        nullptr, nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_execute_until_bytes_async(cd_connection_t* c, cd_bytes_view_t input,
                                                     const cd_check_t* check,
                                                     const cd_call_options_t* options,
                                                     cd_completion_fn cb, void* user,
                                                     cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::ExecuteUntil, false, options);
            r->input = copy(input);
            r->check = Check(check);
            require(r->check.kind == CD_CHECK_CONTAINS || r->check.kind == CD_CHECK_REGEX,
                    "ExecuteUntil requires Contains or Regex");

            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_execute_until_bytes(cd_connection_t* c, cd_bytes_view_t input,
                                               const cd_check_t* check,
                                               const cd_call_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_execute_until_bytes_async(c, input, check, options,
                                                                       nullptr, nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_upload_async(cd_connection_t* c, cd_string_view_t source,
                                        cd_string_view_t destination,
                                        const cd_transfer_options_t* options, cd_completion_fn cb,
                                        void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Upload, false, nullptr);
            r->source = copy(source);
            r->destination = copy(destination);
            text_argument(r->source, "Source required");
            text_argument(r->destination, "Destination required");
            r->options.timeout = c->value->config.defaults.execution_ms;

            if (options)
            {
                require(options->struct_size >= sizeof(*options), "Invalid TransferOptions");
                r->overwrite = options->overwrite != 0;

                if (options->timeout_ms)
                    r->options.timeout = options->timeout_ms;

                r->options.cancellation = cancellation(options->cancellation);
            }

            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_upload(cd_connection_t* c, cd_string_view_t source,
                                  cd_string_view_t destination,
                                  const cd_transfer_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_upload_async(c, source, destination, options, nullptr,
                                                          nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }

    cd_status_t CD_CALL cd_download_async(cd_connection_t* c, cd_string_view_t source,
                                          cd_string_view_t destination,
                                          const cd_transfer_options_t* options, cd_completion_fn cb,
                                          void* user, cd_operation_t** out)
    {
        if (out)

            *out = nullptr;
        try
        {
            auto r = request(c, Action::Download, false, nullptr);
            r->source = copy(source);
            r->destination = copy(destination);
            text_argument(r->source, "Source required");
            text_argument(r->destination, "Destination required");
            r->options.timeout = c->value->config.defaults.execution_ms;

            if (options)
            {
                require(options->struct_size >= sizeof(*options), "Invalid TransferOptions");
                r->overwrite = options->overwrite != 0;

                if (options->timeout_ms)
                    r->options.timeout = options->timeout_ms;

                r->options.cancellation = cancellation(options->cancellation);
            }

            return submit(c, std::move(r), cb, user, out);
        }
        catch (...)
        {
            return exception_result(nullptr);
        }
    }

    cd_status_t CD_CALL cd_download(cd_connection_t* c, cd_string_view_t source,
                                    cd_string_view_t destination,
                                    const cd_transfer_options_t* options, cd_result_t** out)
    {
        try
        {
            return synchronous(c, out,
                               [&](cd_operation_t** op)
                               {
                                   return cd_download_async(c, source, destination, options,
                                                            nullptr, nullptr, op);
                               });
        }
        catch (...)
        {
            return exception_result(out);
        }
    }
}
