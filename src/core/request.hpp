#pragma once
#include "core/options.hpp"
#include "core/operation.hpp"
#include "execution/text_codec.hpp"

namespace cd
{
    enum class Action
    {
        Open,
        Close,
        CloseInput,
        Read,
        Write,
        Execute,
        ExecuteUntil,
        Upload,
        Download
    };

    struct Request
    {
        Action action;
        bool text = false;
        std::string input, source, destination;
        Check check;
        Options options;
        bool overwrite = true;
        std::shared_ptr<Operation> operation;
        Clock::time_point started = Clock::now();
        std::shared_ptr<Result> result = std::make_shared<Result>();
        std::shared_ptr<asio::steady_timer> deadline, idle;
        bool sent = false, writing = false, early = false;
        std::string delimiter;
        size_t delimiter_scanned = 0;
        DecodedText boundary_text, check_text;
        size_t check_scanned = 0;
        std::string marker;
        int marker_mask = 0;
        std::optional<int> marker_exit;
        bool queued = false;
        std::list<std::shared_ptr<Request>>::iterator queue_position;

        Request(Action a, bool t, Options o) : action(a), text(t), options(std::move(o))
        {
        }
    };
}
