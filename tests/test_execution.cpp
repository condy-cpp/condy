#include <version>

#if defined(__cpp_lib_senders)

#include "condy/async_operations.hpp"
#include "condy/channel.hpp"
#include "condy/execution.hpp"
#include "condy/runtime.hpp"
#include "condy/sender_operations.hpp"
#include "condy/sync_wait.hpp"
#include <doctest.h>
#include <stdexcept>
#include <tuple>

namespace ex = std::execution;

TEST_CASE("test execution - schedule") {
    condy::Runtime runtime;
    std::thread::id runtime_thread_id;
    std::thread runtime_thread([&] {
        runtime_thread_id = std::this_thread::get_id();
        runtime.run();
    });

    auto scheduler = condy::get_scheduler(runtime);

    bool executed = false;
    ex::sender auto sender = ex::schedule(scheduler) | ex::then([&] {
                                 executed = true;
                                 return std::this_thread::get_id();
                             });

    auto [thread_id] = ex::sync_wait(sender).value();
    REQUIRE(executed);
    REQUIRE(thread_id == runtime_thread_id);
    REQUIRE(runtime_thread_id != std::this_thread::get_id());

    runtime.allow_exit();
    runtime_thread.join();
}

TEST_CASE("test execution - sender") {
    condy::Runtime runtime;
    std::thread runtime_thread([&] { runtime.run(); });

    auto scheduler = condy::get_scheduler(runtime);

    bool executed = false;
    ex::sender auto sender = ex::schedule(scheduler) | ex::let_value([&] {
                                 executed = true;
                                 return condy::async_nop();
                             });

    auto [r] = ex::sync_wait(sender).value();
    REQUIRE(executed);
    REQUIRE(r == 0);

    runtime.allow_exit();
    runtime_thread.join();
}

TEST_CASE("test execution - when_all") {
    condy::Runtime runtime;
    std::thread runtime_thread([&] { runtime.run(); });

    auto scheduler = condy::get_scheduler(runtime);

    bool executed1 = false;
    bool executed2 = false;

    auto sender1 = ex::schedule(scheduler) | ex::then([&] {
                       executed1 = true;
                       return 42;
                   });
    auto sender2 = ex::schedule(scheduler) | ex::then([&] {
                       executed2 = true;
                       return 0;
                   });

    auto when_all_sender = ex::when_all(sender1, sender2);
    auto [r1, r2] = ex::sync_wait(when_all_sender).value();

    REQUIRE(executed1);
    REQUIRE(executed2);
    REQUIRE(r1 == 42);
    REQUIRE(r2 == 0);

    runtime.allow_exit();
    runtime_thread.join();
}

TEST_CASE("test execution - when_all error") {
    condy::Runtime runtime;
    std::thread runtime_thread([&] { runtime.run(); });

    auto scheduler = condy::get_scheduler(runtime);

    __kernel_timespec ts = {
        .tv_sec = 60ll * 60ll,
        .tv_nsec = 0,
    };
    auto sender = ex::schedule(scheduler) | ex::let_value([&] {
                      return ex::when_all(condy::async_timeout(&ts, 0, 0),
                                          ex::just() | ex::then([]() -> int {
                                              throw std::runtime_error(
                                                  "error in when_all");
                                          }));
                  });

    REQUIRE_THROWS_AS(ex::sync_wait(sender), std::runtime_error);

    runtime.allow_exit();
    runtime_thread.join();
}

TEST_CASE("test execution - when_all error with different thread") {
    condy::Runtime runtime1, runtime2;
    std::thread thread1([&] { runtime1.run(); });
    std::thread thread2([&] { runtime2.run(); });

    auto scheduler1 = condy::get_scheduler(runtime1);
    auto scheduler2 = condy::get_scheduler(runtime2);

    __kernel_timespec ts = {
        .tv_sec = 60ll * 60ll,
        .tv_nsec = 0,
    };
    auto sender1 = ex::schedule(scheduler1) | ex::let_value([&] {
                       return condy::async_timeout(&ts, 0, 0);
                   });
    auto sender2 = ex::schedule(scheduler2) | ex::then([]() -> int {
                       throw std::runtime_error("error in when_all");
                   });

    REQUIRE_THROWS_AS(ex::sync_wait(ex::when_all(sender1, sender2)),
                      std::runtime_error);

    runtime1.allow_exit();
    runtime2.allow_exit();
    thread1.join();
    thread2.join();
}

TEST_CASE("test execution - condy when_all") {
    using condy::operators::operator&&;

    condy::Runtime runtime;
    std::thread runtime_thread([&] { runtime.run(); });
    auto scheduler = condy::get_scheduler(runtime);

    ex::sender auto sender = ex::schedule(scheduler) | ex::let_value([&] {
                                 return condy::async_nop() &&
                                        condy::async_nop() &&
                                        condy::async_nop();
                             }) |
                             ex::then([](int r1, int r2, int r3) {
                                 REQUIRE(r1 == 0);
                                 REQUIRE(r2 == 0);
                                 REQUIRE(r3 == 0);
                                 return r1 + r2 + r3;
                             });

    auto [total] = ex::sync_wait(sender).value();
    REQUIRE(total == 0);

    runtime.allow_exit();
    runtime_thread.join();
}

TEST_CASE("test execution - task") {
    condy::Runtime runtime;
    std::thread runtime_thread([&] { runtime.run(); });

    auto sch = condy::get_scheduler(runtime);

    auto my_task = []() -> ex::task<int> {
        auto r = co_await condy::async_nop();
        REQUIRE(r == 0);
        co_return 42;
    };

    auto [result] = ex::sync_wait(ex::starts_on(sch, my_task())).value();
    REQUIRE(result == 42);

    runtime.allow_exit();
    runtime_thread.join();
}

TEST_CASE("test execution - spawn with simple_counting_scope") {
    condy::Runtime runtime;
    std::thread runtime_thread([&] { runtime.run(); });

    auto sch = condy::get_scheduler(runtime);
    ex::simple_counting_scope scope;

    std::atomic<int> counter{0};
    ex::spawn(ex::schedule(sch) |
                  ex::then([&]() noexcept { counter.fetch_add(1); }),
              scope.get_token());
    ex::spawn(ex::schedule(sch) |
                  ex::then([&]() noexcept { counter.fetch_add(1); }),
              scope.get_token());

    ex::sync_wait(scope.join());

    REQUIRE(counter == 2);

    runtime.allow_exit();
    runtime_thread.join();
}

namespace {

struct StoppedSender {
    using sender_concept = ex::sender_t;
    using completion_signatures =
        ex::completion_signatures<ex::set_value_t(int), ex::set_stopped_t()>;

    template <typename Receiver> struct OperationState {
        Receiver receiver;
        void start() noexcept { ex::set_stopped(std::move(receiver)); }
    };

    template <typename Receiver>
    OperationState<std::decay_t<Receiver>> connect(Receiver &&receiver) {
        return {std::forward<Receiver>(receiver)};
    }
};

} // namespace

TEST_CASE("test execution - wait_sender") {
    condy::Runtime runtime;

    auto co = []() -> condy::Coro<> {
        // single value -> std::optional<std::tuple<int>>
        auto v = co_await condy::wait_sender(ex::just(42));
        REQUIRE(v.has_value());
        REQUIRE(std::get<0>(*v) == 42);

        // no value -> std::optional<std::tuple<>>
        auto empty = co_await condy::wait_sender(ex::just());
        REQUIRE(empty.has_value());

        // multiple values -> std::optional<std::tuple<int, double>>
        auto t = co_await condy::wait_sender(ex::just(1, 2.0));
        REQUIRE(t.has_value());
        REQUIRE(std::get<0>(*t) == 1);
        REQUIRE(std::get<1>(*t) == 2.0);

        // condy sender works too (StandardSender is a standard sender)
        auto r = co_await condy::wait_sender(condy::async_nop());
        REQUIRE(r.has_value());
        REQUIRE(std::get<0>(*r) == 0);

        // error: throws
        REQUIRE_THROWS_AS(
            co_await condy::wait_sender(ex::just(42) | ex::then([](int) -> int {
                                            throw std::runtime_error("boom");
                                        })),
            std::runtime_error);

        // stopped: returns nullopt
        auto s = co_await condy::wait_sender(StoppedSender{});
        REQUIRE(!s.has_value());

        // composed sender
        auto n = co_await condy::wait_sender(
            ex::just(7) | ex::then([](int x) { return x * 2; }));
        REQUIRE(n.has_value());
        REQUIRE(std::get<0>(*n) == 14);
    }();

    condy::sync_wait(runtime, std::move(co));
}

TEST_CASE("test execution - wait_sender cross runtime") {
    condy::Runtime runtime1, runtime2;
    std::thread thread2([&] { runtime2.run(); });

    auto func = [&]() -> condy::Coro<> {
        std::thread::id then_tid;
        auto before_tid = std::this_thread::get_id();
        auto r = co_await condy::wait_sender(
            ex::schedule(condy::get_scheduler(runtime2)) | ex::then([&] {
                then_tid = std::this_thread::get_id();
                return 42;
            }));
        REQUIRE(r.has_value());
        REQUIRE(std::get<0>(*r) == 42);

        auto after_tid = std::this_thread::get_id();
        REQUIRE(after_tid == before_tid);
        REQUIRE(then_tid != before_tid);
    };

    condy::sync_wait(runtime1, func());

    runtime2.allow_exit();
    thread2.join();
}

TEST_CASE("test execution - channel pop sender") {
    condy::Runtime runtime;
    std::thread runtime_thread([&] { runtime.run(); });
    auto scheduler = condy::get_scheduler(runtime);

    condy::Channel<int> ch(1);

    REQUIRE(ch.try_push(42) == 0);

    auto [item] = ex::sync_wait(ex::schedule(scheduler) |
                                ex::let_value([&] { return ch.pop(); }) |
                                ex::then([](auto r, auto item) {
                                    REQUIRE(r == 0);
                                    REQUIRE(item == 42);
                                    return item;
                                }))
                      .value();
    REQUIRE(item == 42);

    runtime.allow_exit();
    runtime_thread.join();
}

#endif