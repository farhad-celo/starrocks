#pragma once

#include <optional>
#include <vector>

#include <cpml/model/bpmn_graph.h>

#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::operators::process::align_model {

using variants = memory::cache::variant_trace_cache_t;

enum class alignment_move_type { UNMAPPED_MOVE, LOG_MOVE, MODEL_MOVE, SYNC_MOVE, GATEWAY_MOVE };

struct alignment_move {
  alignment_move_type move_type{alignment_move_type::UNMAPPED_MOVE};
  std::optional<row_id> move_on_log{std::nullopt};
  std::optional<cpml::model::bpmn::vertex_id_type> move_on_model{std::nullopt};
  auto operator<=>(const alignment_move&) const noexcept = default;  // NOLINT(modernize-use-nullptr)
};

using alignment_t = std::vector<alignment_move>;
using alignments_t = std::vector<std::optional<alignment_t>>;

// avoid circular includes
class replay_result_type;
using replay_results_t = std::vector<std::optional<replay_result_type>>;

}  // namespace celonis::accelerator::operators::process::align_model
