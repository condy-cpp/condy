cmake_minimum_required(VERSION 3.13)

project(condy LANGUAGES CXX)

option(BUILD_EXAMPLES "Build examples" OFF)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_MODULE "Build C++ module interface" OFF)
option(TESTS_STATIC_LINK "Use static linking for tests" OFF)
option(TESTS_TSAN "Enable thread sanitizer for tests" OFF)
option(TESTS_ASAN "Enable address and undefined behavior sanitizers for tests" OFF)
option(LINK_LIBURING "Use liburing from third_party and link it to condy" ON)

add_library(condy INTERFACE)
target_include_directories(condy INTERFACE include)
target_compile_features(condy INTERFACE cxx_std_20)

if(BUILD_EXAMPLES OR BUILD_BENCHMARKS OR BUILD_TESTS OR LINK_LIBURING)
    enable_language(C)
    include(ExternalProject)

    # Use $(MAKE) for Make-based generators so -j flags are inherited automatically.
    if(CMAKE_GENERATOR MATCHES "Make")
        set(MAKE_EXECUTABLE $(MAKE))
    else()
        find_program(MAKE_EXECUTABLE NAMES gmake make REQUIRED)
    endif()

    # liburing
    set(LIBURING_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/liburing)
    message(STATUS "Configuring liburing in ${LIBURING_SOURCE_DIR}")

    ExternalProject_Add(
        liburing_ext
        SOURCE_DIR ${LIBURING_SOURCE_DIR}
        CONFIGURE_COMMAND ./configure --cc=${CMAKE_C_COMPILER} --cxx=${CMAKE_CXX_COMPILER}
        BUILD_COMMAND ${MAKE_EXECUTABLE} -C src
        BUILD_IN_SOURCE 1
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS ${LIBURING_SOURCE_DIR}/src/liburing.a
    )

    add_library(uring STATIC IMPORTED GLOBAL)
    set_target_properties(uring PROPERTIES IMPORTED_LOCATION ${LIBURING_SOURCE_DIR}/src/liburing.a)
    target_include_directories(uring INTERFACE ${LIBURING_SOURCE_DIR}/src/include)
    add_dependencies(uring liburing_ext)
endif()

if(LINK_LIBURING)
    target_link_libraries(condy INTERFACE uring)
endif()

if(BUILD_MODULE)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
    message(STATUS "Configuring C++ module interface in ${CMAKE_CURRENT_SOURCE_DIR}/module")
    add_subdirectory(module)
endif()

# Warnings as errors
add_compile_options(-Wall -Wextra -Werror)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # False positive with GCC
    # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109224
    add_compile_options(-Wno-mismatched-new-delete)
    # False positive with GCC: coroutine frame variables (e.g.
    # structured bindings from co_await) are reported as maybe-uninitialized
    add_compile_options(-Wno-maybe-uninitialized)
endif()

if(BUILD_EXAMPLES)
    message(STATUS "Configuring examples in ${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_subdirectory(examples)
endif()

if(BUILD_BENCHMARKS)
    message(STATUS "Configuring benchmarks in ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks")
    add_subdirectory(benchmarks)
endif()

if(BUILD_TESTS)
    # doctest
    add_subdirectory(third_party/doctest)
    message(STATUS "Configuring tests in ${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()