#include "legacy_embedded_ctl/exception_traits.h"

#include <fmt/format.h>

namespace celonis::accelerator::legacy_embedded_ctl {

namespace {

[[nodiscard]] std::string make_external_error_message(const std::string_view prefix,
                                                      const std::optional<std::string_view> reason) {
  return reason.has_value() ? fmt::format("{} Reason: '{}'.", prefix, reason.value()) : std::string{prefix};
}

[[nodiscard]] std::string make_external_error_message_with_id(const std::string_view prefix,
                                                              const std::optional<std::string_view> optional_reason,
                                                              const std::string_view error_id) {
  return fmt::format("{} Error ID: [{}].", make_external_error_message(prefix, optional_reason), error_id);
}

}  // anonymous namespace

internal_error::internal_error(const std::string_view external_message_prefix,
                               const std::optional<const std::string_view> optional_external_message_reason) {
  const auto error_id_hex_string{error_id::generate().as_hex_string()};
  external_message_with_error_id_ = make_external_error_message_with_id(
      external_message_prefix, optional_external_message_reason, error_id_hex_string);
  // For server errors we add the error ID to the JSON data
  add_or_overwrite(legacy_embedded_format::json::json_key_t{error_id::ERROR_ID_KEY}, error_id_hex_string);
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
