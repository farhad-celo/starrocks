#pragma once

#include "legacy_embedded_ctl/memory/checked_memory_resource.h"
#include "legacy_embedded_ctl/memory/global_memory_tracking_strategy.h"
#include "legacy_embedded_ctl/memory/tracking_memory_resource.h"
#include "legacy_embedded_ctl/static_array_fwd.h"

namespace celonis::accelerator::legacy_embedded_ctl {
constexpr size_t STATIC_ARRAY_MIN_BYTES_FOR_MEM_CHECK{0};

/** Construct and return default allocator using the default memory resource */
template <typename T>
[[nodiscard]] static_array_allocator_type<T> make_default_tracking_allocator(
    const utils::allocation_reason& reason, utils::allocation_priority priority = utils::allocation_priority::LOW,
    bool using_value_init = true) {
  auto tracking_resource{tracking_memory_resource::get_global_tracking_resource()};
  auto checked_resource{std::make_shared<checked_memory_resource>(
      reason, priority, STATIC_ARRAY_MIN_BYTES_FOR_MEM_CHECK, using_value_init, std::move(tracking_resource))};
  return static_array_allocator_type<T>{std::move(checked_resource)};
}

/** Construct and return default allocator for metadata */
template <typename T>
[[nodiscard]] static_array_allocator_type<T> make_metadata_allocator(
    const utils::allocation_reason& reason, utils::allocation_priority priority = utils::allocation_priority::LOW,
    bool using_value_init = true) {
  abstract_strategy_t tracking_strategy{
      std::make_shared<global_memory_tracking_strategy>(nullptr, register_as_metadata_t{true})};
  abstract_resource_t tracking_resource{std::make_shared<tracking_memory_resource>(std::move(tracking_strategy))};
  auto checked_resource{std::make_shared<checked_memory_resource>(
      reason, priority, STATIC_ARRAY_MIN_BYTES_FOR_MEM_CHECK, using_value_init, std::move(tracking_resource))};
  return static_array_allocator_type<T>{std::move(checked_resource)};
}
}  // namespace celonis::accelerator::legacy_embedded_ctl