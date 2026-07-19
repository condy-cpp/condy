#include <coroutine>
#include <string>
#include <unistd.h>

import condy;

condy::Coro<> co_main() {
    std::string msg = "Hello, Condy (via module)!\n";
    co_await condy::async_write(STDOUT_FILENO, condy::buffer(msg), 0);
}

int main() noexcept(false) { condy::sync_wait(co_main()); }
