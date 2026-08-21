#pragma once

#include <optional>

#include <ctl/array_view.h>
#include <ctl/static_array.h>

namespace celonis::accelerator::operators::process::align_model {

// avoid circular includes
class replay_result_type;
using replay_results_t = ctl::static_array<std::optional<replay_result_type>>;
using replay_results_view_t = ctl::array_view<const replay_results_t::value_type>;

}  // namespace celonis::accelerator::operators::process::align_model
