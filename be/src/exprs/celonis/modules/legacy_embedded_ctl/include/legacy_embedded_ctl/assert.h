#pragma once

#include <string_view>
#include <type_traits>

#include <fmt/format.h>

#include "legacy_embedded_ctl/source_location.h"
#include "legacy_embedded_ctl/system_constants.h"

namespace celonis::accelerator::legacy_embedded_ctl {

namespace details {

[[nodiscard]] bool is_abort_disabled();

[[noreturn]] void abort_assert_fail();

[[noreturn]] void abort_assert_fail(std::string_view message);

[[noreturn]] void debug_assert_fail(source_location source_location, std::string_view message);

[[noreturn]] void debug_assert_fail(source_location source_location);

// This functions will throw or log and return, depending on the build type
#ifdef NDEBUG
void warning_assert_fail(source_location source_location, std::string_view message);
void warning_assert_fail(source_location source_location);
#else
[[noreturn]] void warning_assert_fail(source_location source_location, std::string_view message);
[[noreturn]] void warning_assert_fail(source_location source_location);
#endif

/* All 'debug_assert_impl' must be constexpr to be able to call 'debug_assert' in a constexpr context */
template <typename GET_SOURCE_LOC>
constexpr void debug_assert_impl(GET_SOURCE_LOC&& get_source_loc, const bool condition) {
  if (!condition) {
    debug_assert_fail(get_source_loc());
  }
}

template <typename GET_SOURCE_LOC>
constexpr void debug_assert_impl(GET_SOURCE_LOC&& get_source_loc, const bool condition,
                                 const std::string_view message) {
  if (!condition) {
    debug_assert_fail(get_source_loc(), message);
  }
}

template <typename GET_SOURCE_LOC, typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
constexpr void debug_assert_impl(GET_SOURCE_LOC&& get_source_loc, const bool condition, fmt::format_string<ARGS...> fmt,
                                 ARGS&&... args) {
  if (!condition) {
    debug_assert_fail(get_source_loc(), fmt::format(fmt, std::forward<ARGS>(args)...));
  }
}

constexpr void warning_assert_impl(source_location source_location, const bool condition) {
  if (!condition) {
    warning_assert_fail(source_location);
  }
}

constexpr void warning_assert_impl(source_location source_location, const bool condition,
                                   const std::string_view message) {
  if (!condition) {
    warning_assert_fail(source_location, message);
  }
}

template <typename... ARGS, typename = std::enable_if<sizeof...(ARGS) != 0>>
constexpr void warning_assert_impl(source_location source_location, const bool condition,
                                   fmt::format_string<ARGS...> fmt, ARGS&&... args) {
  if (!condition) {
    warning_assert_fail(source_location, fmt::format(fmt, std::forward<ARGS>(args)...));
  }
}

}  // namespace details

/**
 * @brief custom assert which is also checked in Release builds. If the condition fails, std::abort is called.
 * @param condition the condition which shall be asserted to be true
 * @example abort_assert((1 << 4) % 2 == 0);
 * @note in case the environment variable 'DISABLE_CUSTOM_ASSERT' is set, a failed condition does not lead to an abort
 * but a 'failed_assertion' exception is thrown instead.
 */
void abort_assert(bool condition);

/**
 * See above for further information.
 * @example abort_assert((1 << 4) % 2 == 0, "The number is not even.");
 */
void abort_assert(bool condition, std::string_view message);

/**
 * See above for further information.
 * @example abort_assert((1 << 4) % 2 == 0, "The number {} is not even.", (1 << 4));
 */
template <typename... ARGS, typename = std::enable_if_t<sizeof...(ARGS) != 0>>
void abort_assert(const bool condition, fmt::format_string<ARGS...> fmt, ARGS&&... args) {
  if (!condition) {
    details::abort_assert_fail(fmt::format(fmt, std::forward<ARGS>(args)...));
  }
}

/** Can be used similarly to '__builtin_unreachable' but is less risky as it throws instead of being UB */
[[noreturn]] void assert_unreachable(source_location src = source_location{});

}  // namespace celonis::accelerator::legacy_embedded_ctl

/**
 * Similar to cassert, this assertion is only checked in Debug builds. However, this version also syntactically checks
 * the input expression. This eliminates potential syntax errors introduced in assert() checks, which are completely
 * ignored in Release builds and therefore only occur in Debug builds. For this reason, this assertion
 * should be preferred over the normal assert().
 *
 * Example:
 * legacy_embedded_debug_assert(value == 1);
 * legacy_embedded_debug_assert(value == 1, "Value should be 1, but got {}", value);
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define legacy_embedded_debug_assert(...)                                                                                              \
  if constexpr (celonis::accelerator::legacy_embedded_ctl::IS_DEBUG_BUILD) {                                                           \
    celonis::accelerator::legacy_embedded_ctl::details::debug_assert_impl([] { return celonis::accelerator::legacy_embedded_ctl::source_location{}; }, \
                                                          __VA_ARGS__);                                                \
  }                                                                                                                    \
  static_assert(true, "Place a semicolon after warning_assert")

/**
 * This will always perform a check. Do not use it to perform expensive checks.
 * On debug mode it behaves just like a normal assert. On release mode it will log a warning to datadog.
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define legacy_embedded_warning_assert(...)                                                                                           \
  celonis::accelerator::legacy_embedded_ctl::details::warning_assert_impl(celonis::accelerator::legacy_embedded_ctl::source_location{}, __VA_ARGS__); \
  static_assert(true, "Place a semicolon after warning_assert")
