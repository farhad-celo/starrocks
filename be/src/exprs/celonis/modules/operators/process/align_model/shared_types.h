#pragma once

#include <optional>
#include <vector>

#include <cpml/model/bpmn_graph.h>

#include "modules/memory/cache/variant_trace_cache_fwd.h"

namespace celonis::accelerator::operators::process::align_model {

// avoid circular includes
class replay_result_type;
using replay_results_t = ctl::static_array<std::optional<replay_result_type>>;
using replay_results_view_t = ctl::array_view<const replay_results_t::value_type>;

}  // namespace celonis::accelerator::operators::process::align_model
