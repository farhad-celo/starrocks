#pragma once

#include "modules/common/execution_context.h"
#include "modules/operators/process/petri_net/petri_net_fwd.h"
#include "modules/operators/process/alignment/petri_net_information.h"
#include "modules/operators/process/alignment/sequence_aligner.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment::fallback {

// Returns the Petri net run corresponding to the shortest trace in the Petri net
[[nodiscard]] std::optional<sequence_aligner::sequence_type> get_shortest_pn_trace(
    const petri_net::petri_net_accessor& pn_accessor, const petri_net_information& pn_information,
    int max_num_iterations, std::string_view operator_name, const common::execution_context& context);

class fallback_aligner {
 public:
  fallback_aligner(const petri_net::petri_net_accessor& pn_accessor, const petri_net_information& pn_information,
                   int max_shortest_trace_iterations, int max_a_star_iterations, std::string_view operator_name,
                   common::execution_context& context);

  [[nodiscard]] trace_alignment_t operator()(std::span<const row_id> variant,
                                             const common::execution_context& context) const;

 private:
  std::optional<sequence_aligner> impl_{std::nullopt};
};

}  // namespace celonis::accelerator::operators::process::alignment::fallback
