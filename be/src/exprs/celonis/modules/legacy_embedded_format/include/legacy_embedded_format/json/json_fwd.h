#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

#include "modules/common/int_types.h"

namespace celonis::accelerator::legacy_embedded_format::json {
// Forward declaration required for recursive usage
// This class inherits from json::json_value_base_t
class json_value;
using json_value_t = json_value;

/**
 * Convenience type for instantiating JSON structures
 * @see json::json_value for usage of JSON format
 */
using json_t = json_value_t;

using json_key_t = std::string;
using json_object_t = std::map<json_key_t, json_value_t>;
using json_array_t = std::vector<json_value_t>;

using json_value_base_t =
    std::variant<bool, int64_t, uint64_t, double, std::string, std::nullptr_t, json_array_t, json_object_t>;

/**
 * Default type for creating JSON structures
 */
using default_json_value_t = json_object_t;

}  // namespace celonis::accelerator::legacy_embedded_format::json