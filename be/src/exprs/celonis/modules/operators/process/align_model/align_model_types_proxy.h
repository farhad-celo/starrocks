#pragma once

#include <array>
#include <cstdint>

#include <ctl/type_traits.h>

namespace celonis::accelerator::operators::process::align_model {

enum class cs_edge_type : std::uint8_t {
  SYNC,
  MODEL,
  SKIP,
  LOG,
  UNMAPPED,
  L1_MISSING,
  L1_EXCLUSIVE_VIOLATION,
  SIZE  // Not an actual edge type, exists to encode the size of the enum at compile time
};

constexpr std::array<cs_edge_type, ctl::enum_to_underlying_type(cs_edge_type::SIZE)> CS_EDGE_TYPES{
    cs_edge_type::SYNC,
    cs_edge_type::MODEL,
    cs_edge_type::SKIP,
    cs_edge_type::LOG,
    cs_edge_type::UNMAPPED,
    cs_edge_type::L1_MISSING,
    cs_edge_type::L1_EXCLUSIVE_VIOLATION};

[[nodiscard]] std::string_view cs_edge_type_to_string_v2(cs_edge_type type);

}  // namespace celonis::accelerator::operators::process::align_model