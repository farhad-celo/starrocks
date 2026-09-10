#include "align_model_types_proxy.h"

#include <ctl/assert.h>

#include "modules/operators/process/align_model/align_model_types.h"

namespace celonis::accelerator::operators::process::align_model {

namespace {

edge_type constexpr to_saola(cs_edge_type type) {
  switch (type) {
    case cs_edge_type::SYNC:
      return edge_type::SYNC;
    case cs_edge_type::MODEL:
      return edge_type::MODEL;
    case cs_edge_type::LOG:
      return edge_type::LOG;
    case cs_edge_type::SKIP:
      return edge_type::SKIP;
    case cs_edge_type::UNMAPPED:
      return edge_type::UNMAPPED;
    case cs_edge_type::L1_MISSING:
      return edge_type::L1_MISSING;
    case cs_edge_type::L1_EXCLUSIVE_VIOLATION:
      return edge_type::L1_EXCLUSIVE_VIOLATION;
    case cs_edge_type::L1_INCOMPLETE_VIOLATION:
      return edge_type::L1_INCOMPLETE_VIOLATION;
    default:
      ctl::assert_unreachable();
  }
}
}  // namespace

[[nodiscard]] std::string_view cs_edge_type_to_string_v2(cs_edge_type type) {
  return edge_type_to_string_v2(to_saola(type));
}
}  // namespace celonis::accelerator::operators::process::align_model