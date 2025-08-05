#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "legacy_embedded_ctl/exception_framework.h"

/**
 * This file contains traits for exceptions. Traits are used to categorize errors and enrich these with properties.
 * A multiple inheritance pattern is used to allow for differentiating between different error categories.
 */
namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief If an exception inherits from this type the exception is considered to be retryable (e.g., OOM, I/O Errors...)
 *
 * Example:
 * try {
 *  // ...
 * } catch (const retryable_error& ex) {
 *   // We still have access to the base_exception part
 * } catch (const base_exception& ex) {
 *  // ...
 * }
 * TODO(n.weber): Investigate whether to integrate this into 'execute_with_retry'
 */
class retryable_error : virtual public base_exception {
 protected:
  retryable_error() noexcept = default;  // Required for multiple inheritance
};

/** Indicates a non-client issue. Has an external message to communicate to the client with an error ID specified. */
class internal_error : virtual public base_exception {
 public:
  [[nodiscard]] std::string external_message() const final { return external_message_with_error_id_; }

 protected:
  internal_error() noexcept = default;  // Required for multiple inheritance
  internal_error(std::string_view external_message_prefix,
                 std::optional<const std::string_view> optional_external_message_reason);

 private:
  std::string external_message_with_error_id_;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
