#include "rl_align_configs.h"

namespace celonis::accelerator::operators::process::alignment::rl_align {

rl_align_config make_default_rl_align_config() {
  return {
      solver_config{0.15f, 100, 1000.0f, 0.0005f, 30},
      constraints_config_grid{{15, 25, 40, 55},
                              {-50, -100, -175, -275},
                              {-200, -275, -350, -450},
                              {5, 15, 25, 45},
                              {-75, -150, -275, -400}},
      problem_building_config{50},
      lcs_chunk_size_type{20},
  };
}

rl_align_config make_small_rl_align_config() {
  // We start from the single best constraint set from the paper, and add one value to each kind of constraint; since
  // only the ratios between constraints matter, add a larger (absolute) value for each constraint type. Choosing this
  // constraint basically means "make this constraint type more important" (conversely, choosing the larger value for
  // all but one constraint type means "make this constraint type less important"). All of those values are 4 times the
  // original value, as that feels like a reasonable factor and gives good results in the benchmarks.
  return {
      solver_config{0.15f, 100, 1000.0f, 0.0005f, 5'000, 30},
      constraints_config_grid{{15, 60}, {-100, -400}, {-300, -1200}, {5, 20}, {-150, -600}},
      problem_building_config{50},
      lcs_chunk_size_type{20},
  };
}

}  // namespace celonis::accelerator::operators::process::alignment::rl_align
