#include "splittable_eventlog_config.h"

#include "modules/common/exceptions.h"
#include "modules/memory/column.h"

namespace celonis::accelerator::operators::process {

size_t grain_size_from_splittable_eventlog_config(const splittable_eventlog_config_t& config) {
  return std::visit([](const auto& config) { return config.grain_size; }, config);
}

splittable_eventlog_config_t make_splittable_eventlog_config(memory::column_t activities, memory::column_t cases,
                                                             const size_t grain_size, cube::filter_bitset_t selections,
                                                             const common::execution_context& operator_context) {
  if (const auto activity_row_count{activities->get_row_count(operator_context)};
      std::cmp_not_equal(activity_row_count, selections.size())) {
    throw common::internal_exception{"Event filter size [{}] does not match activity row count [{}].",
                                     selections.size(), activity_row_count};
  }
  return splittable_eventlog_config_for_using_entire_eventlog{.activities = std::move(activities),
                                                              .cases = std::move(cases),
                                                              .grain_size = grain_size,
                                                              .selections = std::move(selections)};
}

splittable_eventlog_config_t make_splittable_eventlog_config(memory::column_t activities, memory::column_t cases,
                                                             const size_t grain_size,
                                                             const common::execution_context& operator_context) {
  auto selections{cube::filter_bitset_t(activities->get_row_count(operator_context), true)};
  return make_splittable_eventlog_config(std::move(activities), std::move(cases), grain_size, std::move(selections),
                                         operator_context);
}

splittable_eventlog_config_t make_splittable_eventlog_config(
    memory::cache::variant_trace_cache_t variant_trace_cache_ptr, const size_t grain_size) {
  return splittable_eventlog_config_for_using_variants{.variant_trace_cache_ptr = std::move(variant_trace_cache_ptr),
                                                       .grain_size = grain_size};
}

}  // namespace celonis::accelerator::operators::process
