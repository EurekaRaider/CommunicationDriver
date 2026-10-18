#pragma once
#include "results.hpp"

namespace comm
{
    template <class T>
    class Operation
    {
        std::shared_ptr<detail::OperationControl> control_;

    public:
        explicit Operation(cd_operation_t* h, std::stop_token stop = {}, int stream = 0)
            : control_(std::make_shared<detail::OperationControl>(h, stream))
        {
            if (stop.stop_possible())
                control_->cancellation =
                    std::make_unique<std::stop_callback<std::function<void()>>>(stop,
                        [h]
                        {
                            cd_operation_cancel(h);
                        });
        }

        Operation(Operation&&) = default;
        Operation& operator=(Operation&&) = default;
        Operation(const Operation&) = delete;

        bool is_completed() const
        {
            return cd_operation_is_completed(control_->handle) != 0;
        }

        void wait() const
        {
            cd_result_t* h = nullptr;
            detail::checked(cd_operation_wait(control_->handle, CD_INFINITE, &h));
            cd_result_release(h);
        }

        bool wait_for(Duration timeout) const
        {
            cd_result_t* h = nullptr;
            auto s = cd_operation_wait(control_->handle, timeout.count(), &h);
            cd_result_release(h);

            if (s == CD_TIMEOUT)
                return false;

            detail::checked(s);

            return true;
        }

        T get()
        {
            if (control_->consumed.exchange(true))
                throw std::logic_error("Operation result already consumed");

            cd_result_t* h = nullptr;
            detail::checked(cd_operation_wait(control_->handle, CD_INFINITE, &h));

            detail::ResultHandle owner(h);

            if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, Bytes>)
            {
                detail::checked(cd_result_get_status(h), h);
                return detail::response<T>(h, control_->stream);
            }
            else
                return detail::convert<T>(h);
        }

        void cancel() const
        {
            cd_operation_cancel(control_->handle);
        }
    };
}
