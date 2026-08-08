# Building and Usage

@brief How to build and integrate Condy in your project.

## Direct Header-Only Usage

Condy is a header-only library. You can use it by simply including the header file in your project.  
Make sure `liburing` (≥2.3) is installed on your system.

```cpp
#include <condy.hpp>
```

Compile your code:

```bash
c++ main.cpp -std=c++20 -luring -I/path/to/condy    
```

## Using Condy as a Submodule

You can add Condy to your project via Git submodule:

```bash
git submodule add https://github.com/wokron/condy.git third_party/condy
```

In your `CMakeLists.txt`:

```cmake
add_subdirectory(third_party/condy)
add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE condy)
```

> [!NOTE]
> - C++20 is required for coroutine support.
> - Condy depends on **liburing ≥ 2.3**.
> - Both **GCC** and **Clang** compilers are supported.
> - By default, Condy downloads and links liburing via FetchContent (`CONDY_LINK_LIBURING=ON`). Set `CONDY_LINK_LIBURING_VERSION` to use a specific liburing version, e.g. `-DCONDY_LINK_LIBURING_VERSION=2.15`.
> - To link against the system liburing, set `CONDY_LINK_LIBURING=OFF` and install liburing manually.

### Using Condy via FetchContent

Alternatively, you can add Condy with FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(
    condy
    GIT_REPOSITORY https://github.com/wokron/condy.git
    GIT_TAG v1.9  # Change to the tag you want
)
FetchContent_MakeAvailable(condy)
add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE condy)
```

## Building Examples / Benchmarks / Tests

Condy provides CMake options to build examples, benchmarks, and tests:

```bash
cmake -B build -S . \
    -DCONDY_BUILD_EXAMPLES=ON \
    -DCONDY_BUILD_BENCHMARKS=ON \
    -DCONDY_BUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

After building:

- Run all tests:

```bash
ctest --test-dir build
```

- Run example programs:

```bash
./build/examples/link-cp from.bin to.bin
```

## C++20 Module Support

Condy provides an optional C++20 module interface as an alternative to `#include <condy.hpp>`.

```cpp
import condy;
```

### Requirements

- **CMake 3.28+** with **Ninja** generator
- **GCC 14+** or **Clang 16+**

### Building the Example

```bash
cmake -B build -S . -G Ninja \
    -DCONDY_BUILD_MODULE=ON \
    -DCONDY_BUILD_EXAMPLES=ON
ninja -C build module-hello

./build/examples/module-hello
# Hello, Condy (via module)!
```

### Using in Your Project

The module target is `condy_module`:

```cmake
set(CONDY_BUILD_MODULE ON)
add_subdirectory(third_party/condy)
add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE condy_module)
```

## Version Compatibility

Condy supports **liburing ≥ 2.3**. When building with a specific version of liburing, Condy assumes that all related interfaces provided by that version are already supported by your Linux kernel.

> [!NOTE]
> If your kernel version is older than the liburing version you are using, some features may not be available at runtime, even if compilation succeeds.  
> For best compatibility and feature support, it is recommended to use a Linux kernel version that matches or exceeds the requirements of your chosen liburing version.