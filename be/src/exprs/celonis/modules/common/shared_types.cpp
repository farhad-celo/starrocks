#include "shared_types.h"

#include <string_view>

#include <ctl/assert.h>

#include "modules/common/exceptions.h"

namespace celonis::accelerator {

namespace {

/// Converts type to string and returns a string_view to a compile-time string
[[nodiscard]] constexpr std::string_view convert_to_string_view(const data_type type) {
  switch (type) {
    case cel_int:
      return "INT";
    case cel_string:
      return "STRING";
    case cel_float:
      return "FLOAT";
    case cel_date:
      return "DATE";
    case cel_boolean:
      return "BOOL";
    case cel_uuid:
      return "UUID";
    case cel_null:
      return "NULL";
  }
  ctl::assert_unreachable();
}

}  // namespace

std::ostream& operator<<(std::ostream& os, cel_null_t /*null*/) {
  os << cel_null_t::TEXT_REPRESENTATION;
  return os;
}

std::ostream& operator<<(std::ostream& os, data_type type) { return os << convert_to_string_view(type); }

data_type convert_from_string(const std::string& type) {
  if (type == "INT") {
    return cel_int;
  }
  if (type == "STRING") {
    return cel_string;
  }
  if (type == "FLOAT") {
    return cel_float;
  }
  if (type == "DATE") {
    return cel_date;
  }
  if (type == "UUID") {
    return cel_uuid;
  }
  throw common::cpm_exception{"Unknown data type [{}].", type};
}

std::string convert_to_string(const data_type celonis_data_type) {
  return std::string{convert_to_string_view(celonis_data_type)};
}

[[nodiscard]] bool is_cel_int_value(cel_float_t value) noexcept {
  const auto cel_int_max{static_cast<cel_float_t>(std::numeric_limits<cel_int_t>::max())};
  const auto cel_int_min{static_cast<cel_float_t>(std::numeric_limits<cel_int_t>::min())};
  return (value == std::trunc(value) && !std::isnan(value) && !std::isinf(value) && value >= cel_int_min &&
          value < cel_int_max);
}
}  // namespace celonis::accelerator
