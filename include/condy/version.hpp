/**
 * @file version.hpp
 */

#pragma once

/**
 * @brief Condy major version.
 */
#define CONDY_VERSION_MAJOR 1

/**
 * @brief Condy minor version.
 */
#define CONDY_VERSION_MINOR 9

namespace condy {

/**
 * @brief Condy major version.
 */
inline constexpr int version_major = CONDY_VERSION_MAJOR;

/**
 * @brief Condy minor version.
 */
inline constexpr int version_minor = CONDY_VERSION_MINOR;

} // namespace condy
