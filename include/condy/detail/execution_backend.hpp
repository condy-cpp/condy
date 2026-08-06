/**
 * @file execution_backend.hpp
 * @brief Detection and aliasing of the available std::execution backend.
 */

#pragma once

#ifndef __has_include
#define __has_include(x) 0
#endif

#include <version>

// TODO: This path has not been tested, as no standard library provides
// std::execution yet.
#if defined(__cpp_lib_senders)
#include <execution>
#define CONDY_DETAIL_HAS_EXECUTION 1
#elif __has_include(<stdexec/execution.hpp>)
#include <stdexec/execution.hpp>
#define CONDY_DETAIL_HAS_EXECUTION 1
#elif __has_include(<beman/execution.hpp>)
#include <beman/execution.hpp>
#define CONDY_DETAIL_HAS_EXECUTION 1
#endif

#ifdef CONDY_DETAIL_HAS_EXECUTION

namespace condy {
namespace detail {

#if defined(__cpp_lib_senders)
namespace ex = std::execution;
#elif __has_include(<stdexec/execution.hpp>)
namespace ex = stdexec;
#elif __has_include(<beman/execution.hpp>)
namespace ex = beman::execution;
#endif

} // namespace detail
} // namespace condy

#endif
