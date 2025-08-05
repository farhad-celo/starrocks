#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/type_traits.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/operators/process/bpmn/edge.h"
#include "modules/operators/process/bpmn/vertex_types.h"

namespace celonis::accelerator::operators::process::bpmn {

using bpmn_block_id_t = cel_int_t;
using bpmn_block_ids_t = std::vector<bpmn_block_id_t>;
using bpmn_block_object_id_t = object_id;

/// !!! Note !!! Please be careful with changing the order of these values and/or their underlying integer value.
/// Other code relies on these values and needs to be adjusted when there is a change (also on the Java receiver side)
enum class bpmn_block_type {
  ACTIVITY = 0,
  EXCLUSIVE = 1,
  PARALLEL = 2,
  SEQUENCE = 3,
  REDO = 4,
  ROOT = 5,
  INVALID = -1
};

/**
 * @brief The string representation of each BPMN block type
 * @note !!! We assume the order here to match the one in 'bpmn_block_type' above such that the enum value can be used
 * as index into the array!!!
 */
static constexpr std::array<std::string_view, 6> BPMN_BLOCK_TYPE_STRINGS{"ACTIVITY", "EXCLUSIVE", "PARALLEL",
                                                                         "SEQUENCE", "REDO",      "ROOT"};

[[nodiscard]] inline cel_int_t to_column_value(const bpmn_block_type value) {
  legacy_embedded_debug_assert(value != bpmn_block_type::INVALID);
  return legacy_embedded_ctl::enum_to_underlying_type(value);
}

[[nodiscard]] inline std::string to_string(const bpmn_block_type value) {
  if (value == bpmn_block_type::INVALID) {
    return "INVALID";
  }
  return std::string{BPMN_BLOCK_TYPE_STRINGS.at(to_column_value(value))};
}

struct bpmn_block final {
  bpmn_block_id_t block_id;
  bpmn_block_id_t parent_id;
  bpmn_block_object_id_t object_id;
  bpmn_block_type block_type;
};

using bpmn_blocks_t = std::vector<bpmn_block>;

[[nodiscard]] inline std::string to_string(const bpmn_block& value) {
  const auto& [block_id, parent_id, object_id, block_type]{value};
  return fmt::format("block_id=[{}], parent_id=[{}], object_id=[{}], block_type=[{}]", block_id, parent_id, object_id,
                     to_string(block_type));
}

[[nodiscard]] inline bool is_root_block(const bpmn_block& value) { return value.block_type == bpmn_block_type::ROOT; }
[[nodiscard]] inline bool is_sequence_block(const bpmn_block& value) {
  return value.block_type == bpmn_block_type::SEQUENCE;
}

using vertex_ids_t = std::vector<vertex_id_type>;
using block_id_to_contained_vertices_mapping_t = std::vector<vertex_ids_t>;

static constexpr bpmn_block_id_t INVALID_BLOCK_ID{-1};
static constexpr bpmn_block_id_t NO_BLOCK_PARENT_ID{INVALID_BLOCK_ID};

}  // namespace celonis::accelerator::operators::process::bpmn
