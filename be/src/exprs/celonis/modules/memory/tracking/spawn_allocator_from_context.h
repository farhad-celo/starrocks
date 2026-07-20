#pragma once

#include "legacy_embedded_ctl/memory/batched_memory_tracking_strategy.h"
#include "legacy_embedded_ctl/memory/checked_memory_resource.h"
#include "legacy_embedded_ctl/memory/global_memory_tracking_strategy.h"
#include "legacy_embedded_ctl/memory/resource_owning_allocator.h"
#include "legacy_embedded_ctl/memory/tracking_memory_resource.h"

namespace celonis::accelerator::memory::tracking {

namespace details {

template <typename T>
[[nodiscard]] legacy_embedded_ctl::resource_owning_allocator<T> build_checked_allocator_for_memory_tracking_strategy(
    legacy_embedded_ctl::abstract_strategy_t memory_tracking_strategy,
    const legacy_embedded_ctl::utils::allocation_reason& reason,
    legacy_embedded_ctl::utils::allocation_priority priority, bool using_value_init,
    size_t minimum_size_for_memory_check, legacy_embedded_ctl::abstract_resource_t upstream_memory_resource) {
  legacy_embedded_ctl::abstract_resource_t tracking_resource{
      std::make_shared<legacy_embedded_ctl::tracking_memory_resource>(std::move(memory_tracking_strategy),
                                                                      std::move(upstream_memory_resource))};
  auto checked_resource{std::make_shared<legacy_embedded_ctl::checked_memory_resource>(
      reason, priority, minimum_size_for_memory_check, using_value_init, std::move(tracking_resource))};
  return legacy_embedded_ctl::resource_owning_allocator<T>{std::move(checked_resource)};
}

}  // namespace details

constexpr size_t DEFAULT_MINIMUM_SIZE_FOR_MEMORY_CHECK_IN_BYTES{0};

template <typename T>
[[nodiscard]] inline legacy_embedded_ctl::resource_owning_allocator<T> spawn_allocator(
    [[maybe_unused]] const common::execution_context& context,
    const legacy_embedded_ctl::utils::allocation_reason& reason,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW,
    bool using_value_init = true, size_t minimum_size_for_memory_check = DEFAULT_MINIMUM_SIZE_FOR_MEMORY_CHECK_IN_BYTES,
    legacy_embedded_ctl::abstract_resource_t upstream_memory_resource =
        legacy_embedded_ctl::get_default_memory_resource()) {
  auto context_strategy{legacy_embedded_ctl::global_memory_tracking_strategy::get_global_memory_tracking_strategy()};
  return details::build_checked_allocator_for_memory_tracking_strategy<T>(
      std::move(context_strategy), reason, priority, using_value_init, minimum_size_for_memory_check,
      std::move(upstream_memory_resource));
}

constexpr size_t DEFAULT_MINIMUM_BATCH_SIZE_IN_BYTES{65536};

template <typename T>
[[nodiscard]] inline legacy_embedded_ctl::resource_owning_allocator<T> spawn_batched_allocator(
    [[maybe_unused]] const common::execution_context& context,
    const legacy_embedded_ctl::utils::allocation_reason& reason,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW,
    bool using_value_init = true, size_t min_batch_size = DEFAULT_MINIMUM_BATCH_SIZE_IN_BYTES,
    size_t minimum_size_for_memory_check = DEFAULT_MINIMUM_SIZE_FOR_MEMORY_CHECK_IN_BYTES,
    legacy_embedded_ctl::abstract_resource_t upstream_memory_resource =
        legacy_embedded_ctl::get_default_memory_resource()) {
  auto context_strategy{legacy_embedded_ctl::global_memory_tracking_strategy::get_global_memory_tracking_strategy()};
  auto batched_strategy{std::make_shared<legacy_embedded_ctl::memory::batched_memory_tracking_strategy>(
      std::move(context_strategy), min_batch_size)};
  return details::build_checked_allocator_for_memory_tracking_strategy<T>(
      std::move(batched_strategy), reason, priority, using_value_init, minimum_size_for_memory_check,
      std::move(upstream_memory_resource));
}

}  // namespace celonis::accelerator::memory::tracking
