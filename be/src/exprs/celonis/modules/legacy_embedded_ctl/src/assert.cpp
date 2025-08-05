#include "legacy_embedded_ctl/assert.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include "legacy_embedded_ctl/exception.h"
#include "log/log.h"

namespace celonis::accelerator::legacy_embedded_ctl {

namespace details {

bool is_abort_disabled() {
  // NOLINTNEXTLINE (concurrency-mt-unsafe)
  static const bool is_abort_disabled{std::getenv("DISABLE_CUSTOM_ASSERT_ABORT") != nullptr};
  return is_abort_disabled;
}

void abort_assert_fail() {
  if (is_abort_disabled()) {
    throw failed_assertion{};
  }
  std::abort();
}

void abort_assert_fail(const std::string_view message) {
  if (is_abort_disabled()) {
    throw failed_assertion{std::string{message}};
  }
  std::cerr << message << std::endl;
  std::abort();
}

void debug_assert_fail(source_location source_location, const std::string_view message) {
  std::cerr << fmt::format("Assertion failed {} - {}", source_location, message) << std::endl;
  std::abort();
}

void debug_assert_fail(source_location source_location) {
  std::cerr << fmt::format("Assertion failed {}", source_location) << std::endl;
  std::abort();
}

void warning_assert_fail(source_location source_location, const std::string_view message) {
  if constexpr (legacy_embedded_ctl::IS_DEBUG_BUILD) {
    debug_assert_fail(source_location, message);
  } else {
    log::warn(fmt::format("Assertion failed {} - {}", source_location, message));
  }
}

void warning_assert_fail(source_location source_location) {
  if constexpr (legacy_embedded_ctl::IS_DEBUG_BUILD) {
    debug_assert_fail(source_location);
  } else {
    log::warn(fmt::format("Assertion failed {}", source_location));
  }
}

}  // namespace details

void abort_assert(const bool condition) {
  if (!condition) {
    // Having the if-check separated from the heavy "body" of the block makes it possible for compilers to inline the
    // if-check but not the handler. Otherwise, they might not inline the ctl_assert at all which is bad if the
    // ctl_assert is used in a relatively hot loop
    details::abort_assert_fail();
  }
}

void abort_assert(const bool condition, const std::string_view message) {
  if (!condition) {
    details::abort_assert_fail(message);
  }
}

void assert_unreachable(const source_location src) {
  throw failed_assertion{"Source code at [{}] is expected to be unreachable but was reached anyways", src};
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
