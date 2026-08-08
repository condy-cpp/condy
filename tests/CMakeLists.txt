message(STATUS "Fetching doctest")
FetchContent_MakeAvailable(doctest)

file(GLOB SRC CONFIGURE_DEPENDS "*.cpp")
add_executable(tests ${SRC})
target_link_libraries(tests PRIVATE condy uring)
target_include_directories(tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})

if (CONDY_TESTS_STATIC_LINK)
    message(STATUS "Configuring tests with static linking")
    target_link_options(tests PRIVATE -static)
elseif (CONDY_TESTS_TSAN)
    message(STATUS "Configuring tests with thread sanitizer")
    target_compile_options(tests PRIVATE -fsanitize=thread)
    target_link_options(tests PRIVATE -fsanitize=thread)
elseif (CONDY_TESTS_ASAN)
    message(STATUS "Configuring tests with address and undefined behavior sanitizers")
    target_compile_options(tests PRIVATE -fsanitize=address,undefined)
    target_link_options(tests PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME tests COMMAND tests)

add_subdirectory(test_crash)