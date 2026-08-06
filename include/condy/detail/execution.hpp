/**
 * @file execution.hpp
 * @brief Internal sender/receiver adapters for std::execution integration.
 */

#pragma once

#ifdef CONDY_HAS_STDEXEC

#include "condy/detail/utils.hpp"
#include "condy/runtime.hpp"
#include <stdexec/execution.hpp>
#include <type_traits>

namespace condy {

namespace detail {

class Scheduler {
public:
    using scheduler_concept = stdexec::scheduler_tag;

    Scheduler(Runtime &runtime) : runtime_(&runtime) {}

    bool operator==(const Scheduler &other) const noexcept {
        return runtime_ == other.runtime_;
    }

    /**
     * @brief Schedule a task that runs on the runtime's event loop.
     * @return A sender that completes on the runtime thread.
     */
    auto schedule() const noexcept { return ScheduleSender{*runtime_}; }

    /**
     * @brief Get the runtime this scheduler is backed by.
     * @return Reference to the runtime.
     */
    Runtime &runtime() const noexcept { return *runtime_; }

private:
    template <typename Receiver>
    class OperationState
        : public InvokerAdapter<OperationState<Receiver>, WorkInvoker> {
    public:
        using operation_state_concept = stdexec::operation_state_tag;

        OperationState(Runtime &runtime, Receiver receiver)
            : runtime_(runtime), receiver_(std::move(receiver)) {}

        CONDY_DELETE_COPY_MOVE(OperationState);

    public:
        void start() noexcept { runtime_.schedule_internal(this); }

        void invoke() noexcept { stdexec::set_value(std::move(receiver_)); }

    private:
        Runtime &runtime_;
        Receiver receiver_;
    };

    class ScheduleSender {
    public:
        using sender_concept = stdexec::sender_tag;

        using completion_signatures =
            stdexec::completion_signatures<stdexec::set_value_t()>;

        ScheduleSender(Runtime &runtime) : runtime_(runtime) {}

        template <typename Receiver> auto connect(Receiver receiver) {
            return OperationState<std::decay_t<Receiver>>(runtime_,
                                                          std::move(receiver));
        }

        struct Env {
            Runtime &runtime;
            Scheduler
            query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>)
                const noexcept {
                return Scheduler{runtime};
            }
        };
        Env get_env() const noexcept { return {runtime_}; }

    private:
        Runtime &runtime_;
    };

private:
    Runtime *runtime_;
};

template <typename R> struct set_value_traits {
    using type = stdexec::set_value_t(R);
};
template <typename R1, typename R2> struct set_value_traits<std::pair<R1, R2>> {
    using type = stdexec::set_value_t(R1, R2);
};
template <typename... R> struct set_value_traits<std::tuple<R...>> {
    using type = stdexec::set_value_t(R...);
};
template <typename R>
using set_value_traits_t = typename set_value_traits<R>::type;

template <typename SenderImpl>
class [[nodiscard]] StandardSender : public SenderImpl {
public:
    using SenderImpl::SenderImpl;

    using sender_concept = stdexec::sender_tag;
    using completion_signatures = stdexec::completion_signatures<
        set_value_traits_t<typename SenderImpl::ReturnType>,
        stdexec::set_error_t(std::error_code), stdexec::set_stopped_t()>;

    template <typename Receiver> auto connect(Receiver receiver) noexcept {
        using OpState = decltype(this->connect_impl(
            ReceiverWrapper<Receiver>{std::move(receiver)}));
        return OperationStateWrapper<OpState>{
            this->connect_impl(ReceiverWrapper<Receiver>{std::move(receiver)})};
    }

    // All condy senders are affine
    auto affine() && noexcept { return std::move(*this); }

private:
    template <typename Receiver> struct ReceiverWrapper {
        Receiver receiver;

        void operator()(int32_t res) noexcept {
            if (res >= 0) {
                stdexec::set_value(std::move(receiver), res);
            } else if (res == -ECANCELED) {
                stdexec::set_stopped(std::move(receiver));
            } else {
                stdexec::set_error(
                    std::move(receiver),
                    std::error_code(-res, std::generic_category()));
            }
        }

        template <typename T>
        void operator()(std::pair<int32_t, T> res) noexcept {
            auto &[res_code, payload] = res;
            if (res_code >= 0) {
                stdexec::set_value(std::move(receiver), std::move(res_code),
                                   std::move(payload));
            } else if (res_code == -ECANCELED) {
                stdexec::set_stopped(std::move(receiver));
            } else {
                stdexec::set_error(
                    std::move(receiver),
                    std::error_code(-res_code, std::generic_category()));
            }
        }

        template <typename... T>
        void operator()(std::tuple<T...> res) noexcept {
            std::apply(
                [this](auto &&...args) {
                    stdexec::set_value(std::move(receiver),
                                       std::forward<decltype(args)>(args)...);
                },
                std::move(res));
        }

        template <typename T> void operator()(T &&res) noexcept {
            stdexec::set_value(std::move(receiver), std::forward<T>(res));
        }

        auto get_stop_token() const noexcept {
            auto env = stdexec::get_env(receiver);
            return stdexec::get_stop_token(env);
        }
    };

    template <typename OperationState> struct OperationStateWrapper {
        using operation_state_concept = stdexec::operation_state_tag;

        OperationState op_state;
        void start() noexcept { op_state.start(0); }
    };
};

template <typename... Ts> struct wait_variant {
    static_assert(sizeof...(Ts) == 1, "Requires only one value type");
};
template <typename T> struct wait_variant<T> {
    using type = T;
};
template <typename... Ts>
using wait_variant_t = typename wait_variant<Ts...>::type;

template <typename Sender> class [[nodiscard]] WaitSenderAwaiter {
private:
    struct Env {
        Runtime &runtime;
        Scheduler query(stdexec::get_scheduler_t) const noexcept {
            return Scheduler{runtime};
        }
        Scheduler query(stdexec::get_start_scheduler_t) const noexcept {
            return Scheduler{runtime};
        }
        Scheduler query(stdexec::get_delegation_scheduler_t) const noexcept {
            return Scheduler{runtime};
        }
    };
    using ValueType =
        stdexec::value_types_of_t<Sender, Env, std::tuple, wait_variant_t>;

public:
    WaitSenderAwaiter(Sender sender, Runtime &runtime)
        : runtime_(runtime),
          op_state_(stdexec::connect(std::move(sender), Receiver{this})) {}

    CONDY_DELETE_COPY_MOVE(WaitSenderAwaiter);

public:
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        runtime_.pend_work_internal();
        stdexec::start(op_state_);
        auto h = std::exchange(handle_, handle);
        return h == std::noop_coroutine();
    }

    std::optional<ValueType> await_resume() {
        runtime_.resume_work_internal();
        auto &result = *result_;
        if (result.index() == 1) {
            std::rethrow_exception(std::get<1>(std::move(result)));
        }
        if (result.index() == 2) {
            return std::nullopt;
        }
        return std::get<0>(std::move(result));
    }

private:
    struct Receiver {
        using receiver_concept = stdexec::receiver_tag;
        WaitSenderAwaiter *self;

        template <typename... Args> void set_value(Args &&...args) noexcept {
            self->complete_value_(std::forward<Args>(args)...);
        }

        template <typename E> void set_error(E &&e) noexcept {
            self->complete_error_(std::forward<E>(e));
        }

        void set_stopped() noexcept { self->complete_stopped_(); }

        auto get_env() const noexcept { return Env{self->runtime_}; }
    };

    template <typename E> static std::exception_ptr to_exception_(E &&e) {
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            return std::forward<E>(e);
        } else if constexpr (std::is_same_v<std::decay_t<E>, std::error_code>) {
            return std::make_exception_ptr(
                std::system_error(std::forward<E>(e)));
        } else {
            return std::make_exception_ptr(std::forward<E>(e));
        }
    }

    template <typename... Args> void complete_value_(Args &&...args) noexcept {
        result_.emplace(std::in_place_index<0>,
                        ValueType(std::forward<Args>(args)...));
        resume_();
    }

    template <typename E> void complete_error_(E &&e) noexcept {
        result_.emplace(std::in_place_index<1>,
                        to_exception_(std::forward<E>(e)));
        resume_();
    }

    void complete_stopped_() noexcept {
        result_.emplace(std::in_place_index<2>, std::monostate{});
        resume_();
    }

    void resume_() noexcept {
        auto h = std::exchange(handle_, nullptr);
        h.resume();
    }

    using ResultType =
        std::variant<ValueType, std::exception_ptr, std::monostate>;

    Runtime &runtime_;
    stdexec::connect_result_t<Sender, Receiver> op_state_;
    std::coroutine_handle<> handle_ = std::noop_coroutine();
    std::optional<ResultType> result_;
};

} // namespace detail

} // namespace condy

#endif