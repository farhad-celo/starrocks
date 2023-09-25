#pragma once

#include "ctl/memory/batched_memory_tracking_strategy.h"
#include "ctl/memory/checked_memory_resource.h"
#include "ctl/memory/global_memory_tracking_strategy.h"
#include "ctl/memory/resource_owning_allocator.h"
#include "ctl/memory/tracking_memory_resource.h"
#include "modules/common/execution_context.h"

namespace celonis::accelerator::memory::tracking {

namespace details {

[[nodiscard]] inline ctl::abstract_strategy_t extract_memory_tracking_strategy_from_context(
    const common::execution_context& context) {
  auto context_strategy{context.get_memory_tracking_strategy()};
  return context_strategy == nullptr ? ctl::global_memory_tracking_strategy::get_global_memory_tracking_strategy()
                                     : std::move(context_strategy);
}

template <typename T>
[[nodiscard]] ctl::resource_owning_allocator<T> build_checked_allocator_for_memory_tracking_strategy(
    ctl::abstract_strategy_t memory_tracking_strategy, const ctl::utils::allocation_reason& reason,
    ctl::utils::allocation_priority priority, bool using_value_init, size_t minimum_size_for_memory_check,
    ctl::abstract_resource_t upstream_memory_resource) {
  ctl::abstract_resource_t tracking_resource{std::make_shared<ctl::tracking_memory_resource>(
      std::move(memory_tracking_strategy), std::move(upstream_memory_resource))};
  auto checked_resource{std::make_shared<ctl::checked_memory_resource>(reason, priority, minimum_size_for_memory_check,
                                                                       using_value_init, std::move(tracking_resource))};
  return ctl::resource_owning_allocator<T>{std::move(checked_resource)};
}

}  // namespace details

constexpr size_t DEFAULT_MINIMUM_SIZE_FOR_MEMORY_CHECK_IN_BYTES{0};

template <typename T>
[[nodiscard]] inline ctl::resource_owning_allocator<T> spawn_allocator(
    const common::execution_context& context, const ctl::utils::allocation_reason& reason,
    ctl::utils::allocation_priority priority = ctl::utils::allocation_priority::LOW, bool using_value_init = true,
    size_t minimum_size_for_memory_check = DEFAULT_MINIMUM_SIZE_FOR_MEMORY_CHECK_IN_BYTES,
    ctl::abstract_resource_t upstream_memory_resource = ctl::get_default_memory_resource()) {
  auto context_strategy{details::extract_memory_tracking_strategy_from_context(context)};
  return details::build_checked_allocator_for_memory_tracking_strategy<T>(
      std::move(context_strategy), reason, priority, using_value_init, minimum_size_for_memory_check,
      std::move(upstream_memory_resource));
}

constexpr size_t DEFAULT_MINIMUM_BATCH_SIZE_IN_BYTES{65536};

template <typename T>
[[nodiscard]] inline ctl::resource_owning_allocator<T> spawn_batched_allocator(
    const common::execution_context& context, const ctl::utils::allocation_reason& reason,
    ctl::utils::allocation_priority priority = ctl::utils::allocation_priority::LOW, bool using_value_init = true,
    size_t min_batch_size = DEFAULT_MINIMUM_BATCH_SIZE_IN_BYTES,
    size_t minimum_size_for_memory_check = DEFAULT_MINIMUM_SIZE_FOR_MEMORY_CHECK_IN_BYTES,
    ctl::abstract_resource_t upstream_memory_resource = ctl::get_default_memory_resource()) {
  auto context_strategy{details::extract_memory_tracking_strategy_from_context(context)};
  auto batched_strategy{
      std::make_shared<ctl::memory::batched_memory_tracking_strategy>(std::move(context_strategy), min_batch_size)};
  return details::build_checked_allocator_for_memory_tracking_strategy<T>(
      std::move(batched_strategy), reason, priority, using_value_init, minimum_size_for_memory_check,
      std::move(upstream_memory_resource));
}

}  // namespace celonis::accelerator::memory::tracking
