# Condy🍬

![C++](https://img.shields.io/badge/C++-20-blue)
![License](https://img.shields.io/github/license/wokron/condy)
![Release](https://img.shields.io/github/v/release/wokron/condy)
![Stars](https://img.shields.io/github/stars/wokron/condy?style=social)

![CI (Main)](https://github.com/wokron/condy/actions/workflows/build-and-test.yml/badge.svg?branch=master)
![CI (Latest Kernel)](https://github.com/wokron/condy/actions/workflows/build-and-test-latest-kernel.yml/badge.svg?branch=master)
![CI (Versions)](https://github.com/wokron/condy/actions/workflows/build-and-test-liburing-versions.yml/badge.svg?branch=master)
![CI (Static Check)](https://github.com/wokron/condy/actions/workflows/static-check.yml/badge.svg?branch=master)
![Deploy Docs](https://github.com/wokron/condy/actions/workflows/deploy-gh-pages.yaml/badge.svg?branch=master)

***C++ Asynchronous System Call Layer for Linux, Powered by io_uring and C++20 Coroutines***

Condy is designed to provide an intuitive, high-performance coroutine runtime on top of io_uring:

- 🛠️ **Comprehensive io_uring Integration**
  Designed to integrate and maintain support for most io_uring features, with ongoing updates to track kernel and liburing advancements.

- 🏃 **Low Overhead**
  Efficient template-based abstractions and precise lifetime management eliminate nearly all heap allocations outside coroutine frames, resulting in extremely low runtime overhead.

- 💡 **Intuitive Programming Model**
  Write asynchronous code in a direct, readable style using C++20 coroutines—no callbacks. Friendly APIs, high-level combinators, and channels make complex async flows easy to express.

## Quick Start

```cpp
// hello.cpp
#include <condy.hpp>

condy::Coro<> co_main() {
    std::string msg = "Hello, Condy!\n";
    co_await condy::async_write(STDOUT_FILENO, condy::buffer(msg), 0);
}

int main() { condy::sync_wait(co_main()); }
```

```bash
# Make sure liburing (>=2.3) is installed on your system
# On Ubuntu: sudo apt install liburing-dev
c++ hello.cpp -o hello -std=c++20 -luring -I./include
./hello
# Hello, Condy!
```

See [Documentation](#documentation) for more details.

## Documentation

- **[Online Docs (GitHub Pages)](https://wokron.github.io/condy/)**
- **[Building and Usage](docs/build.md):** How to build and integrate Condy in your project.
- **[User Guide](docs/guide.md):** Step-by-step introduction to Condy’s concepts and usage.
- **[Benchmarks](docs/bench.md):** Performance comparison between Condy and other frameworks.
- **[Examples](docs/examples.md):** Practical Condy code samples.
- **[Async Operation Types](docs/ops.md):** Overview and classification of supported io_uring async operation variants.

## Support

- For questions, bug reports, or feature requests, please open an [issue](https://github.com/wokron/condy/issues).
- [Pull requests](https://github.com/wokron/condy/pulls) are welcome!
