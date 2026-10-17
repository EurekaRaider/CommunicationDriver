#include "core/connection.hpp"
#include <algorithm>
#include <charconv>
#include <random>
#include <sstream>

namespace cd
{
    Connection::Connection(std::shared_ptr<Runtime> r, ConnectionConfig c)
        : runtime(std::move(r)), config(std::move(c)),
          capabilities(CD_CAP_READ | CD_CAP_WRITE | CD_CAP_EXECUTE | CD_CAP_EXECUTE_UNTIL)
    {
        capabilities = config.traits.capabilities;
    }

    Connection::~Connection()
    {
        if (persistent)
            persistent->close();
        if (one_shot)
            one_shot->close();
    }

    void Connection::attach(const std::shared_ptr<Backend>& backend, bool isolated)
    {
        auto weak = weak_from_this();
        uint64_t g = generation;
        backend->output = [weak, g, isolated](int stream, std::string data, bool end)
        {
            if (auto self = weak.lock(); self && self->generation == g)
                self->receive(stream, std::move(data), end, isolated);
        };
        backend->failure = [weak, g](cd_status_t s, std::string m, int64_t n)
        {
            if (auto self = weak.lock(); self && self->generation == g)
            {
                self->state = CD_STATE_FAULTED;
                if (self->active)
                    self->finish(s, std::move(m), n);
            }
        };
        backend->exited = [weak, g, isolated](std::optional<int> code)
        {
            if (auto self = weak.lock();
                self && self->generation == g && (!self->isolating || isolated))
            {
                self->process_exit = code;
                self->process_finished = true;

                if (!isolated && self->config.traits.mode == CD_PERSISTENT)
                    self->state = CD_STATE_FAULTED;

                self->process_data();
            }
        };
        backend->command_output = [weak, g](int stream, std::string data, bool end)
        {
            if (auto self = weak.lock(); self && self->generation == g)
                self->receive(stream, std::move(data), end, true);
        };
        backend->command_exited = [weak, g](std::optional<int> code)
        {
            if (auto self = weak.lock(); self && self->generation == g)
            {
                self->process_exit = code;
                self->process_finished = true;
                self->process_data();
            }
        };
    }

    void Connection::submit(std::shared_ptr<Request> request)
    {
        auto self = shared_from_this();
        auto op = request->operation;
        {
            std::lock_guard lock(op->mutex);
            op->cancel_hook = [weak = weak_from_this(), w = std::weak_ptr<Request>(request)]
            {
                if (auto c = weak.lock())
                    asio::post(c->runtime->state->io,
                               [c, w]
                               {
                                   if (auto r = w.lock())
                                       c->abort(r, CD_CANCELLED, "Operation cancelled");
                               });
            };
        }

        if (request->options.cancellation)
            request->options.cancellation->subscribe(op);

        asio::post(runtime->state->io,
                   [self, request]
                   {
                       request->deadline =
                           std::make_shared<asio::steady_timer>(self->runtime->state->io);
                       request->deadline->expires_at(
                           request->started + std::chrono::milliseconds(request->options.timeout));
                       request->deadline->async_wait(
                           [weak = self->weak_from_this(),
                            w = std::weak_ptr<Request>(request)](ErrorCode e)
                           {
                               if (!e)
                                   if (auto c = weak.lock())
                                       if (auto r = w.lock())
                                           c->abort(r, CD_TIMEOUT, "Operation timed out");
                           });

                       if (request->action == Action::Close)
                       {
                           self->force_close();
                           ++self->closing_count;
                           self->runtime->after_files(
                               [self, request]
                               {
                                   --self->closing_count;
                                   self->finish_close(request);
                                   self->next();
                               });

                           return;
                       }

                       request->queue_position = self->queue.insert(self->queue.end(), request);
                       request->queued = true;

                       if (request->operation->cancelled)
                           self->abort(request, CD_CANCELLED, "Operation cancelled");

                       self->next();
                   });
    }

    void Connection::next()
    {
        if (active || closing_count || queue.empty())
            return;

        active = queue.front();
        active->queued = false;
        queue.pop_front();

        try
        {
            start();
        }
        catch (const Fault& error)
        {
            finish(error.code, error.message, error.native);
        }
        catch (const std::exception& error)
        {
            finish(CD_INTERNAL_ERROR, error.what());
        }
    }

    void Connection::ensure_persistent(Completion callback)
    {
        if (persistent && !(!config.traits.reuse_command_channel && persistent->command_channels()))
        {
            persistent->ensure_stdio(std::move(callback));
            return;
        }

        if (persistent)
        {
            persistent->close();
            persistent.reset();
        }

        persistent = make_backend(runtime, config, true);
        attach(persistent);
        persistent->open(std::move(callback));
    }

    void Connection::start()
    {
        auto r = active;
        auto self = shared_from_this();
        r->result->backend = config.name();

        if (r->action == Action::Read)
            r->result->partial_stream = r->options.read_stream;

        if (r->operation->cancelled)
        {
            finish(CD_CANCELLED, "Operation cancelled");
            return;
        }

        if (Clock::now() >= r->deadline->expiry())
        {
            finish(CD_TIMEOUT, "Operation timed out before execution");
            return;
        }

        if (runtime->closed && r->action != Action::Close)
        {
            finish(CD_CLOSED, "Driver is closed");
            return;
        }

        if (r->action == Action::Open)
        {
            if (state == CD_STATE_OPEN)
            {
                finish();
                return;
            }

            if (state == CD_STATE_NEEDS_RECOVERY || state == CD_STATE_FAULTED)
            {
                finish(CD_NEEDS_RECOVERY, "Close before reopening");
                return;
            }

            state = CD_STATE_OPENING;
            ++generation;
            pending[0].clear();
            pending[1].clear();
            messages[0].clear();
            messages[1].clear();
            eof[0] = eof[1] = false;
            process_exit.reset();
            process_finished = false;
            persistent = make_backend(runtime, config);
            attach(persistent);
            persistent->open(
                [self, r](cd_status_t s, std::string m, int64_t n)
                {
                    if (self->active != r)
                        return;
                    self->state = s == CD_OK ? CD_STATE_OPEN : CD_STATE_FAULTED;
                    self->finish(s, std::move(m), n);
                });

            return;
        }

        if (state != CD_STATE_OPEN)
        {
            finish(state == CD_STATE_NEEDS_RECOVERY ? CD_NEEDS_RECOVERY
                   : state == CD_STATE_CLOSED       ? CD_CLOSED
                                                    : CD_NOT_OPEN,
                   "Connection is not ready");
            return;
        }

        if (!(capabilities & CD_CAP_STDERR) && (r->options.read_stream || r->options.check_stream))
        {
            finish(CD_NOT_SUPPORTED, "Backend has no independent stderr");
            return;
        }

        if ((r->action == Action::Upload || r->action == Action::Download) &&
            !(capabilities & CD_CAP_UPLOAD))
        {
            finish(CD_NOT_SUPPORTED, "Backend has no file transfer");
            return;
        }

        if (r->action == Action::CloseInput && !(capabilities & CD_CAP_CLOSE_INPUT))
        {
            finish(CD_NOT_SUPPORTED, "Backend has no input EOF");
            return;
        }

        if (r->options.rule.kind == CD_RECEIVE_MESSAGE && !(capabilities & CD_CAP_MESSAGE))
        {
            finish(CD_NOT_SUPPORTED, "Backend has no native message boundary");
            return;
        }

        bool exec = r->action == Action::Execute || r->action == Action::ExecuteUntil;

        if (exec && config.traits.mode != CD_ONE_SHOT && eof[0] &&
            (!(capabilities & CD_CAP_STDERR) || eof[1]))
        {
            finish(CD_END_OF_STREAM, "Response stream has ended");
            return;
        }

        bool managed = config.managed();

        if (r->action == Action::Execute && !managed &&
            !(config.traits.commands && config.traits.mode == CD_ONE_SHOT) &&
            (r->options.rule.kind == CD_RECEIVE_NONE ||
             r->options.rule.kind == CD_RECEIVE_AVAILABLE))
        {
            finish(CD_INVALID_ARGUMENT,
                   "Execute requires a reliable ReceiveUntil rule before sending");
            return;
        }

        if (r->action == Action::ExecuteUntil &&
            (r->check.kind == CD_CHECK_NONE || r->check.kind == CD_CHECK_EQUALS))
        {
            finish(CD_INVALID_ARGUMENT, "ExecuteUntil requires Contains or Regex");
            return;
        }

        if (exec && !(config.traits.commands && config.traits.mode == CD_ONE_SHOT) &&
            (!pending[0].empty() || !pending[1].empty() || !messages[0].empty() ||
             !messages[1].empty()))
        {
            state = CD_STATE_NEEDS_RECOVERY;
            finish(CD_NEEDS_RECOVERY, "Unattributed output exists; close/reopen before executing");
            return;
        }

        if (exec)
            r->result->kind = r->text ? CD_RESULT_EXECUTION_TEXT : CD_RESULT_EXECUTION_BYTES;
        else if (r->action == Action::Read)
            r->result->kind = r->text ? CD_RESULT_TEXT : CD_RESULT_BYTES;
        else if (r->action == Action::Upload || r->action == Action::Download)
            r->result->kind = CD_RESULT_TRANSFER;

        if (r->action == Action::Upload || r->action == Action::Download)
        {
            r->result->source = r->source;
            r->result->destination = r->destination;
            r->sent = true;
            persistent->transfer(
                r->action == Action::Upload, r->source, r->destination, r->overwrite,
                [weak = std::weak_ptr<Request>(r)](uint64_t bytes)
                {
                    if (auto req = weak.lock())
                        req->result->progress = bytes;
                },
                [self, r](cd_status_t status, std::string message, int64_t native)
                {
                    if (self->active == r)
                    {
                        if (status != CD_OK)
                        {
                            r->result->partial_file = self->persistent->transfer_partial_file;
                            if (status != CD_PERMISSION_DENIED && status != CD_INVALID_ARGUMENT)
                            {
                                self->state = CD_STATE_NEEDS_RECOVERY;
                                ++self->generation;
                                self->persistent->close();
                            }
                        }

                        self->finish(status, message, native);
                    }
                });

            return;
        }

        if (exec && config.traits.commands && config.traits.mode == CD_ONE_SHOT)
        {
            isolating = true;

            for (int stream = 0; stream < 2; ++stream)
            {
                stdio_pending[stream] = std::exchange(pending[stream], {});
                stdio_eof[stream] = eof[stream];
            }

            eof[0] = eof[1] = false;
            process_exit.reset();
            process_finished = false;

            if (config.traits.reuse_command_channel)
            {
                one_shot = persistent;
            }
            else
            {
                one_shot = make_backend(runtime, config);
                attach(one_shot, true);
            }

            r->sent = true;
            r->writing = true;
            one_shot->command(r->input,
                              [self, r](cd_status_t s, std::string m, int64_t n)
                              {
                                  if (self->active != r)
                                      return;
                                  r->writing = false;
                                  if (s != CD_OK)
                                      self->finish(s, m, n);
                                  else
                                      self->process_data();
                              });

            return;
        }

        ensure_persistent(
            [self, r, exec, managed](cd_status_t s, std::string m, int64_t n)
            {
                if (self->active != r)
                    return;

                if (s != CD_OK)
                {
                    self->finish(s, m, n);
                    return;
                }

                if (r->action == Action::CloseInput)
                {
                    self->persistent->close_input(
                        [self, r](cd_status_t status, std::string message, int64_t native)
                        {
                            if (self->active == r)
                                self->finish(status, message, native);
                        });
                    return;
                }

                if (r->action == Action::Read)
                {
                    self->process_data();
                    return;
                }

                std::string data = r->input;

                if (exec && managed)
                {
                    std::random_device random;
                    std::ostringstream marker;
                    marker << "__CD_" << std::hex << random() << random() << random() << "__";
                    r->marker = marker.str();
                    data = self->persistent->frame_command(data, r->marker);
                }

                r->sent = true;
                r->writing = true;
                self->persistent->write(
                    std::move(data),
                    [self, r, exec](cd_status_t status, std::string message, int64_t native)
                    {
                        if (self->active != r)
                            return;

                        r->writing = false;

                        if (status != CD_OK)
                            self->finish(status, message, native);
                        else if (!exec)
                            self->finish();
                        else
                            self->process_data();
                    });
            });
    }

    void Connection::receive(int stream, std::string data, bool end, bool isolated)
    {
        auto r = active;

        if (isolated && (!r || (r->action != Action::Execute && r->action != Action::ExecuteUntil)))
            return;

        if (isolating && !isolated)
        {
            stdio_pending[stream] += data;
            stdio_eof[stream] |= end;

            if (stdio_pending[0].size() + stdio_pending[1].size() > config.defaults.max_bytes)
            {
                state = CD_STATE_NEEDS_RECOVERY;
                if (persistent)
                    persistent->close();
                finish(CD_LIMIT_EXCEEDED, "Persistent stdio buffer limit exceeded");
            }

            return;
        }

        if (end)
            eof[stream] = true;

        if ((capabilities & CD_CAP_MESSAGE) && !end)
            messages[stream].push_back(data.size());

        pending[stream] += data;

        uint64_t limit = r ? r->options.max_bytes : config.defaults.max_bytes;
        uint64_t wire_limit = limit;

        if (r && !r->marker.empty())
        {
            auto control = 2 * (r->marker.size() + 16);
            wire_limit = limit > UINT64_MAX - control ? UINT64_MAX : limit + control;
        }

        size_t total = pending[0].size() + pending[1].size();

        if (r)
            total += r->result->raw[0].size() + r->result->raw[1].size();

        if (total > wire_limit || messages[0].size() + messages[1].size() >
                                      std::max<uint64_t>(1024, limit / sizeof(size_t)))
        {
            state = CD_STATE_NEEDS_RECOVERY;

            if (r)
            {
                r->result->raw[stream] += pending[stream];
                pending[stream].clear();
                finish(CD_LIMIT_EXCEEDED, "Response buffer limit exceeded");
            }

            if (persistent)
                persistent->close();

            if (one_shot)
                one_shot->close();

            return;
        }

        process_data();
    }

    void Connection::consume_pending(int stream, size_t count)
    {
        pending[stream].erase(0, count);

        auto& sizes = messages[stream];

        while (!sizes.empty())
        {
            if (sizes.front() > count)
            {
                sizes.front() -= count;
                break;
            }

            count -= sizes.front();
            sizes.pop_front();

            if (!count)
                break;
        }
    }

    void Connection::process_data()
    {
        auto r = active;

        if (!r || r->writing)
            return;

        try
        {
            if (r->action == Action::Read)
            {
                int stream = r->options.read_stream;
                size_t consume = 0, keep = 0;

                if (boundary(pending[stream], *r, consume, keep))
                {
                    if (r->text && r->options.rule.kind == CD_RECEIVE_AVAILABLE)
                    {
                        auto text = decode(pending[stream], r->options.encoding, false);
                        if (text.empty() && !pending[stream].empty())
                        {
                            if (eof[stream])
                                throw Fault(CD_ENCODING_ERROR, "Incomplete terminal scalar");
                            return;
                        }
                        consume = keep = encode(text, r->options.encoding).size();
                    }

                    r->result->raw[stream] = pending[stream].substr(0, keep);
                    consume_pending(stream, consume);
                    finish();

                    return;
                }

                if (r->options.rule.kind == CD_RECEIVE_IDLE && !pending[stream].empty())
                {
                    r->result->raw[stream] += pending[stream];
                    consume_pending(stream, pending[stream].size());

                    if (!r->idle)
                        r->idle = std::make_shared<asio::steady_timer>(runtime->state->io);

                    r->idle->expires_after(std::chrono::milliseconds(r->options.rule.idle_ms));

                    auto self = shared_from_this();
                    r->idle->async_wait(
                        [self, r](ErrorCode e)
                        {
                            if (!e && self->active == r)
                            {
                                r->result->completion = CD_COMPLETION_IDLE;
                                self->finish();
                            }
                        });
                }

                if (eof[stream])
                {
                    r->result->raw[stream] += std::exchange(pending[stream], {});
                    finish(CD_END_OF_STREAM, "Input reached EOF");
                }

                return;
            }

            if (r->action != Action::Execute && r->action != Action::ExecuteUntil)
                return;

            for (int stream = 0; stream < 2; ++stream)
            {
                if (!r->marker.empty())
                {
                    auto& data = pending[stream];
                    auto pos = data.find(r->marker + ":");

                    if (pos != std::string::npos)
                    {
                        auto end = data.find('\n', pos);

                        if (end != std::string::npos)
                        {
                            auto exit_text = data.substr(pos + r->marker.size() + 1,
                                                         end - pos - r->marker.size() - 1);

                            if (!exit_text.empty() && exit_text.back() == '\r')
                                exit_text.pop_back();

                            int code = 0;
                            auto parsed = std::from_chars(
                                exit_text.data(), exit_text.data() + exit_text.size(), code);

                            if (parsed.ec != std::errc())
                                throw Fault(CD_PROTOCOL_ERROR,
                                            "Invalid internal completion marker");

                            r->result->raw[stream] += data.substr(0, pos);
                            data.erase(0, end + 1);
                            r->marker_mask |= 1 << stream;
                            r->marker_exit = code;
                        }
                    }

                    // Retain only the possible marker suffix, so checks can observe output immediately.
                    if (!(r->marker_mask & (1 << stream)))
                    {
                        size_t retain = 0;

                        for (size_t n = 1; n <= std::min(data.size(), r->marker.size() + 1); ++n)
                            if (data.compare(data.size() - n, n, (r->marker + ":").substr(0, n)) ==
                                0)
                                retain = n;

                        auto prefix = data.size() - retain;

                        if (data.find(r->marker + ":") == std::string::npos)
                        {
                            r->result->raw[stream] += data.substr(0, prefix);
                            data.erase(0, prefix);
                        }
                    }
                }
                else if (config.traits.commands && config.traits.mode == CD_ONE_SHOT)
                    r->result->raw[stream] += std::exchange(pending[stream], {});
            }

            bool ended = config.traits.commands && process_finished && eof[0] && eof[1];
            bool command_done = (!r->marker.empty() && r->marker_mask == 3) ||
                                (ended && (config.managed() || config.traits.mode == CD_ONE_SHOT));

            if (ended)
            {
                r->result->exit_code = process_exit;
                for (int stream = 0; stream < 2; ++stream)
                    r->result->raw[stream] += std::exchange(pending[stream], {});
            }

            if (!r->marker.empty() &&
                r->result->raw[0].size() + r->result->raw[1].size() > r->options.max_bytes)
            {
                state = CD_STATE_NEEDS_RECOVERY;

                if (persistent)
                    persistent->close();

                if (one_shot)
                    one_shot->close();

                finish(CD_LIMIT_EXCEEDED, "Response buffer limit exceeded");

                return;
            }

            if (command_done && !ended)
                r->result->exit_code = r->marker_exit;

            bool raw_until = r->action == Action::ExecuteUntil && r->marker.empty() &&
                             !(config.traits.commands && config.traits.mode == CD_ONE_SHOT);
            bool raw_done = false;
            size_t raw_consume = 0, raw_keep = 0;

            if (raw_until)
            {
                r->result->raw[0] += std::exchange(pending[0], {});
                r->result->raw[1] += std::exchange(pending[1], {});
                if (r->options.rule.kind != CD_RECEIVE_NONE &&
                    r->options.rule.kind != CD_RECEIVE_AVAILABLE &&
                    r->options.rule.kind != CD_RECEIVE_IDLE)
                    raw_done = boundary(r->result->raw[0], *r, raw_consume, raw_keep);
            }

            if (r->action == Action::ExecuteUntil && !r->early)
            {
                size_t visible_size = r->result->raw[0].size();

                if (raw_until)
                {
                    if (raw_done)
                        visible_size = raw_keep;
                    else if (r->options.rule.kind == CD_RECEIVE_DELIMITER &&
                             !r->options.rule.include)
                    {
                        const auto& delimiter = r->delimiter;
                        for (size_t n = 1; n < delimiter.size() && n <= r->result->raw[0].size();
                             ++n)
                            if (r->result->raw[0].compare(r->result->raw[0].size() - n, n,
                                                          delimiter, 0, n) == 0)
                                visible_size = r->result->raw[0].size() - n;
                    }
                }

                std::string_view checked = r->result->raw[r->options.check_stream];

                if (r->options.check_stream == 0)
                    checked = checked.substr(0, visible_size);

                if (r->check.text)
                {
                    r->check_text.append(checked, r->options.encoding);
                    checked = r->check_text.text;
                }

                // Contains only needs the new data and a possible cross-block prefix.
                size_t overlap = r->check.value.empty() ? 0 : r->check.value.size() - 1;
                size_t start = r->check.kind == CD_CHECK_CONTAINS &&
                                       r->check_scanned <= checked.size() &&
                                       r->check_scanned > overlap
                                   ? r->check_scanned - overlap
                                   : 0;
                r->result->evaluate_data(r->check, checked, start);
                r->check_scanned = checked.size();

                if (r->result->passed)
                {
                    r->result->completion = CD_COMPLETION_MATCH;

                    bool cleanup = !r->marker.empty() ||
                                   (config.traits.commands && config.traits.mode == CD_ONE_SHOT) ||
                                   (raw_until && r->options.rule.kind != CD_RECEIVE_NONE &&
                                    r->options.rule.kind != CD_RECEIVE_AVAILABLE &&
                                    r->options.rule.kind != CD_RECEIVE_IDLE);

                    if (cleanup && !command_done && !raw_done)
                    {
                        auto delivered = std::make_shared<Result>(*r->result);
                        delivered->raw[0].resize(visible_size);
                        delivered->elapsed_ns =
                            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                                 r->started)
                                .count();

                        if (r->text)
                            for (int i = 0; i < 2; ++i)
                                delivered->text[i] =
                                    decode(delivered->raw[i], r->options.encoding, false);

                        r->early = true;
                        r->operation->complete(delivered);
                    }
                    else
                    {
                        if (raw_until && !cleanup)
                            state = CD_STATE_NEEDS_RECOVERY;

                        if (!raw_done)
                        {
                            messages[0].clear();
                            messages[1].clear();
                            finish();

                            return;
                        }
                    }
                }
            }

            if (raw_until && raw_done)
            {
                pending[0] = r->result->raw[0].substr(raw_consume) + pending[0];
                r->result->raw[0].resize(raw_keep);

                size_t left = raw_consume;

                while (!messages[0].empty())
                {
                    if (messages[0].front() > left)
                    {
                        messages[0].front() -= left;
                        break;
                    }

                    left -= messages[0].front();
                    messages[0].pop_front();

                    if (!left)
                        break;
                }

                if (r->early || r->result->passed)
                    finish();
                else
                    finish(CD_END_OF_STREAM, "Protocol response completed without matching");

                return;
            }

            if (command_done)
            {
                if (!r->early && r->action == Action::ExecuteUntil && !r->result->passed)
                {
                    finish(CD_END_OF_STREAM, "Command completed without matching");
                    return;
                }

                if (!r->early && r->result->completion != CD_COMPLETION_MATCH)
                    r->result->completion = CD_COMPLETION_COMMAND;

                finish();

                return;
            }

            if (r->marker.empty() &&
                !(config.traits.commands && config.traits.mode == CD_ONE_SHOT) &&
                r->action == Action::Execute)
            {
                size_t consume = 0, keep = 0;
                int stream = 0;

                if (boundary(pending[stream], *r, consume, keep))
                {
                    r->result->raw[stream] = pending[stream].substr(0, keep);
                    consume_pending(stream, consume);
                    r->result->completion = CD_COMPLETION_PROTOCOL;
                    finish();

                    return;
                }

                if (r->options.rule.kind == CD_RECEIVE_IDLE && !pending[0].empty())
                {
                    r->result->raw[0] += pending[0];
                    consume_pending(0, pending[0].size());

                    if (!r->idle)
                        r->idle = std::make_shared<asio::steady_timer>(runtime->state->io);

                    r->idle->expires_after(std::chrono::milliseconds(r->options.rule.idle_ms));

                    auto self = shared_from_this();
                    r->idle->async_wait(
                        [self, r](ErrorCode e)
                        {
                            if (!e && self->active == r)
                            {
                                r->result->completion = CD_COMPLETION_IDLE;
                                self->state = CD_STATE_NEEDS_RECOVERY;
                                self->finish();
                            }
                        });
                }
            }

            bool input_ended = eof[0] && (!(capabilities & CD_CAP_STDERR) || eof[1]);

            if (input_ended && !command_done &&
                !(config.traits.commands &&
                  (config.traits.mode == CD_ONE_SHOT || config.managed())))
            {
                r->result->raw[0] += std::exchange(pending[0], {});
                r->result->raw[1] += std::exchange(pending[1], {});
                finish(CD_END_OF_STREAM, "Input reached EOF before completion");
            }
        }
        catch (const Fault& f)
        {
            r->result->raw[0] += std::exchange(pending[0], {});
            r->result->raw[1] += std::exchange(pending[1], {});

            if (r->sent)
                state = CD_STATE_NEEDS_RECOVERY;

            finish(f.code, f.message, f.native);
        }
        catch (const std::exception& e)
        {
            finish(CD_INTERNAL_ERROR, e.what());
        }
    }

    void Connection::finish(cd_status_t status, std::string message, int64_t native, bool advance)
    {
        auto r = std::exchange(active, {});

        if (!r)
            return;

        r->deadline->cancel();

        if (r->idle)
            r->idle->cancel();

        r->result->status = status;
        r->result->message = std::move(message);
        r->result->native = native;
        r->result->elapsed_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - r->started).count();

        try
        {
            if (r->text)
                for (int i = 0; i < 2; ++i)
                    r->result->text[i] = status == CD_OK
                                             ? decode(r->result->raw[i], r->options.encoding, true)
                                             : partial_text(r->result->raw[i], r->options.encoding);
            if (status == CD_OK &&
                (r->action == Action::Execute || r->action == Action::ExecuteUntil))
                r->result->evaluate(r->check, r->options.check_stream, r->options.encoding, true);
        }
        catch (const Fault& f)
        {
            if (status == CD_OK)
            {
                r->result->status = f.code;
                r->result->message = f.message;
                r->result->native = f.native;
            }

            if (r->text)
                for (int i = 0; i < 2; ++i)
                    try
                    {
                        r->result->text[i] = partial_text(r->result->raw[i], r->options.encoding);
                    }
                    catch (const Fault&)
                    {
                    }
        }

        if (!r->early)
            r->operation->complete(r->result);

        if (one_shot && !config.traits.reuse_command_channel)
        {
            one_shot->close();
            one_shot.reset();
        }

        if (isolating)
        {
            for (int stream = 0; stream < 2; ++stream)
            {
                pending[stream] = std::exchange(stdio_pending[stream], {});
                eof[stream] = stdio_eof[stream];
            }
            isolating = false;
        }

        if (advance)
        {
            auto self = shared_from_this();
            asio::post(runtime->state->io,
                       [self]
                       {
                           self->next();
                       });
        }
    }

    void Connection::abort(const std::shared_ptr<Request>& r, cd_status_t status,
                           std::string message)
    {
        if (r->action == Action::Close)
        {
            if (r->deadline)
                finish_close(r, status, std::move(message));
            return;
        }

        if (r == active)
        {
            if (r->sent && (r->action == Action::Execute || r->action == Action::ExecuteUntil ||
                            r->action == Action::Write))
                state = CD_STATE_NEEDS_RECOVERY;

            r->result->raw[0] += std::exchange(pending[0], {});
            r->result->raw[1] += std::exchange(pending[1], {});

            if (r->action == Action::Upload || r->action == Action::Download)
            {
                if (r->sent && persistent)
                    r->result->partial_file = persistent->transfer_partial_file;

                state = CD_STATE_NEEDS_RECOVERY;
                ++generation;

                if (persistent)
                    persistent->close();
            }

            if (r->action == Action::Open)
            {
                state = CD_STATE_FAULTED;
                ++generation;

                if (persistent)
                    persistent->close();

                persistent.reset();
            }

            finish(status, std::move(message));
        }
        else
        {
            if (!r->queued)
                return;

            queue.erase(r->queue_position);
            r->queued = false;
            r->deadline->cancel();
            r->result->status = status;
            r->result->message = std::move(message);
            r->result->elapsed_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - r->started)
                    .count();
            r->operation->complete(r->result);
        }
    }

    void Connection::finish_close(const std::shared_ptr<Request>& r, cd_status_t status,
                                  std::string message)
    {
        if (r->operation->completed())
            return;

        r->deadline->cancel();
        r->result->status = status;
        r->result->message = std::move(message);

        if (status == CD_OK && r->operation->cancelled)
        {
            r->result->status = CD_CANCELLED;
            r->result->message = "Operation cancelled";
        }

        r->result->elapsed_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - r->started).count();
        r->operation->complete(r->result);
    }

    void Connection::force_close()
    {
        state = CD_STATE_CLOSING;
        ++generation;

        if (persistent)
            persistent->close();

        if (one_shot && one_shot != persistent)
            one_shot->close();

        persistent.reset();
        one_shot.reset();

        auto queued = std::exchange(queue, {});

        if (active)
        {
            active->result->raw[0] += std::exchange(pending[0], {});
            active->result->raw[1] += std::exchange(pending[1], {});
            finish(CD_CLOSED, "Connection closed", 0, false);
        }

        for (auto& r : queued)
        {
            r->queued = false;
            r->deadline->cancel();
            r->result->status = CD_CLOSED;
            r->result->message = "Connection closed";
            r->operation->complete(r->result);
        }

        pending[0].clear();
        pending[1].clear();
        messages[0].clear();
        messages[1].clear();
        stdio_pending[0].clear();
        stdio_pending[1].clear();
        isolating = false;
        state = CD_STATE_CLOSED;
    }
}
