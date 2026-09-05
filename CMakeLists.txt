cmake_minimum_required(VERSION 3.13)

# Version is defined once in the version header.
set(CONDY_VERSION_HEADER include/condy/version.hpp)
file(READ ${CONDY_VERSION_HEADER} _condy_version_h)
string(REGEX MATCH "#define CONDY_VERSION_MAJOR ([0-9]+)" _condy_ver_m "${_condy_version_h}")
set(CONDY_VERSION_MAJOR ${CMAKE_MATCH_1})
string(REGEX MATCH "#define CONDY_VERSION_MINOR ([0-9]+)" _condy_ver_m "${_condy_version_h}")
set(CONDY_VERSION_MINOR ${CMAKE_MATCH_1})
if(NOT CONDY_VERSION_MAJOR OR NOT CONDY_VERSION_MINOR)
    message(FATAL_ERROR "Failed to parse version from ${CONDY_VERSION_HEADER}")
endif()
set(CONDY_VERSION_STRING "v${CONDY_VERSION_MAJOR}.${CONDY_VERSION_MINOR}")

project(condy VERSION ${CONDY_VERSION_MAJOR}.${CONDY_VERSION_MINOR} LANGUAGES CXX)
message(STATUS "Configuring condy ${CONDY_VERSION_STRING}")

option(CONDY_BUILD_EXAMPLES "Build examples" OFF)
option(CONDY_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(CONDY_BUILD_TESTS "Build tests" OFF)
option(CONDY_BUILD_MODULE "Build C++ module interface" OFF)
option(CONDY_BUILD_DOCS "Build Doxygen documentation" OFF)
option(CONDY_TESTS_STATIC_LINK "Use static linking for tests" OFF)
option(CONDY_TESTS_TSAN "Enable thread sanitizer for tests" OFF)
option(CONDY_TESTS_ASAN "Enable address and undefined behavior sanitizers for tests" OFF)
option(CONDY_LINK_LIBURING "Use liburing and link it to condy" ON)
set(CONDY_LINK_LIBURING_VERSION "2.15" CACHE STRING "liburing version to fetch and link")
option(CONDY_ENABLE_STDEXEC "Enable stdexec execution integration" OFF)
option(CONDY_ENABLE_BEMAN "Enable beman execution integration" OFF)

# Backward compatibility for the unprefixed option names.
macro(condy_legacy_option legacy new)
    if(DEFINED ${legacy})
        message(DEPRECATION "The ${legacy} option is deprecated, use ${new} instead")
        get_property(_condy_${new}_desc CACHE ${new} PROPERTY HELPSTRING)
        set(${new} ${${legacy}} CACHE BOOL "${_condy_${new}_desc}" FORCE)
    endif()
endmacro()
condy_legacy_option(BUILD_EXAMPLES CONDY_BUILD_EXAMPLES)
condy_legacy_option(BUILD_BENCHMARKS CONDY_BUILD_BENCHMARKS)
condy_legacy_option(BUILD_TESTS CONDY_BUILD_TESTS)
condy_legacy_option(BUILD_MODULE CONDY_BUILD_MODULE)
condy_legacy_option(TESTS_STATIC_LINK CONDY_TESTS_STATIC_LINK)
condy_legacy_option(TESTS_TSAN CONDY_TESTS_TSAN)
condy_legacy_option(TESTS_ASAN CONDY_TESTS_ASAN)
condy_legacy_option(LINK_LIBURING CONDY_LINK_LIBURING)

include(FetchContent)

set(DOXYGEN_AWESOME_CSS_VERSION v2.4.1)
set(DOCTEST_VERSION v2.5.0)
set(LIBURING_VERSION liburing-${CONDY_LINK_LIBURING_VERSION})
set(STDEXEC_VERSION 2c56ffe)
set(BEMAN_VERSION d24898d)

FetchContent_Declare(
    doxygen-awesome-css
    URL https://raw.githubusercontent.com/jothepro/doxygen-awesome-css/${DOXYGEN_AWESOME_CSS_VERSION}/doxygen-awesome.css
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(
    doctest
    URL https://raw.githubusercontent.com/doctest/doctest/${DOCTEST_VERSION}/doctest/doctest.h
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(
    liburing
    URL https://github.com/axboe/liburing/archive/refs/tags/${LIBURING_VERSION}.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    stdexec
    URL https://github.com/NVIDIA/stdexec/archive/${STDEXEC_VERSION}.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    beman
    URL https://github.com/bemanproject/execution/archive/${BEMAN_VERSION}.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

add_library(condy INTERFACE)
target_include_directories(condy INTERFACE include)
target_compile_features(condy INTERFACE cxx_std_20)

if(CONDY_BUILD_EXAMPLES OR CONDY_BUILD_BENCHMARKS OR CONDY_BUILD_TESTS OR CONDY_LINK_LIBURING)
    enable_language(C)
    include(ExternalProject)

    # Use $(MAKE) for Make-based generators so -j flags are inherited automatically.
    if(CMAKE_GENERATOR MATCHES "Make")
        set(MAKE_EXECUTABLE $(MAKE))
    else()
        find_program(MAKE_EXECUTABLE NAMES gmake make REQUIRED)
    endif()

    # liburing
    message(STATUS "Fetching liburing ${LIBURING_VERSION}")
    FetchContent_MakeAvailable(liburing)

    ExternalProject_Add(
        liburing_ext
        SOURCE_DIR ${liburing_SOURCE_DIR}
        CONFIGURE_COMMAND ./configure --cc=${CMAKE_C_COMPILER} --cxx=${CMAKE_CXX_COMPILER}
        BUILD_COMMAND ${MAKE_EXECUTABLE} -C src
        BUILD_IN_SOURCE 1
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS ${liburing_SOURCE_DIR}/src/liburing.a
    )

    add_library(uring STATIC IMPORTED GLOBAL)
    set_target_properties(uring PROPERTIES IMPORTED_LOCATION ${liburing_SOURCE_DIR}/src/liburing.a)
    target_include_directories(uring INTERFACE ${liburing_SOURCE_DIR}/src/include)
    add_dependencies(uring liburing_ext)
endif()

if(CONDY_LINK_LIBURING)
    target_link_libraries(condy INTERFACE uring)
endif()

if(CONDY_ENABLE_STDEXEC)
    # stdexec
    message(STATUS "Fetching stdexec")
    FetchContent_GetProperties(stdexec)
    if(NOT stdexec_POPULATED)
        FetchContent_Populate(stdexec)
    endif()
    add_library(stdexec INTERFACE)
    target_include_directories(stdexec SYSTEM INTERFACE ${stdexec_SOURCE_DIR}/include)
    target_link_libraries(condy INTERFACE stdexec)
    target_compile_definitions(condy INTERFACE CONDY_HAS_STDEXEC CONDY_HAS_EXECUTION)
elseif(CONDY_ENABLE_BEMAN)
    # beman execution
    message(STATUS "Fetching beman execution")
    FetchContent_GetProperties(beman)
    if(NOT beman_POPULATED)
        FetchContent_Populate(beman)
    endif()
    add_library(beman INTERFACE)
    target_include_directories(beman SYSTEM INTERFACE ${beman_SOURCE_DIR}/include)
    target_link_libraries(condy INTERFACE beman)
    target_compile_definitions(condy INTERFACE CONDY_HAS_BEMAN CONDY_HAS_EXECUTION)
    target_compile_features(condy INTERFACE cxx_std_23)
endif()

if(CONDY_BUILD_MODULE)
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

if(CONDY_BUILD_EXAMPLES)
    message(STATUS "Configuring examples in ${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_subdirectory(examples)
endif()

if(CONDY_BUILD_BENCHMARKS)
    message(STATUS "Configuring benchmarks in ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks")
    add_subdirectory(benchmarks)
endif()

if(CONDY_BUILD_TESTS)
    message(STATUS "Configuring tests in ${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()

if(CONDY_BUILD_DOCS)
    add_subdirectory(docs)
endif()