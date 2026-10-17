#pragma once
#include "execution/check.hpp"

namespace cd
{
    struct Result;

    struct BatchEntry
    {
        std::string key;
        int state = CD_BATCH_NOT_STARTED;
        std::shared_ptr<Result> result;
    };

    struct Result
    {
        cd_status_t status = CD_OK;
        int kind = CD_RESULT_VOID;
        std::string message, backend;
        int partial_stream = 0;
        int64_t native = 0;
        std::string raw[2], text[2], matched;
        bool check_present = false, passed = false, matched_is_text = true;
        std::vector<std::string> groups;
        std::string failure;
        int completion = CD_COMPLETION_NONE;
        std::optional<int> exit_code;
        uint64_t elapsed_ns = 0;
        std::optional<uint64_t> progress;
        std::string source, destination, partial_file;
        std::vector<BatchEntry> items;
        bool was_cancelled = false;

        bool succeeded() const
        {
            return status == CD_OK && (!check_present || passed);
        }

        void evaluate(const Check&, int stream, cd_encoding_t, bool final);
        void evaluate_data(const Check&, std::string_view, size_t start = 0);
    };

    std::shared_ptr<Result> error_result(const Fault&);
}
