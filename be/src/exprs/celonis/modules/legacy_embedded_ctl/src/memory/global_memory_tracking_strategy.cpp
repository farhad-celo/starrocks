#include "legacy_embedded_ctl/memory/global_memory_tracking_strategy.h"

#include "legacy_embedded_ctl/memory/memory_consumption_tracker.h"

namespace celonis::accelerator::legacy_embedded_ctl {

global_memory_tracking_strategy::global_memory_tracking_strategy(
    abstract_strategy_t downstream_strategy, const register_as_metadata_t register_as_metadata) noexcept
    : memory_tracking_strategy{std::move(downstream_strategy)}, register_as_metadata_{register_as_metadata.get()} {}

void global_memory_tracking_strategy::register_allocation(const std::size_t bytes) {
  global_memory_consumption_tracker::get_consumption_tracker().register_allocation(bytes, register_as_metadata_);
  register_downstream(bytes);
}

void global_memory_tracking_strategy::deregister_allocation(const std::size_t bytes) {
  global_memory_consumption_tracker::get_consumption_tracker().deregister_allocation(bytes, register_as_metadata_);
  deregister_downstream(bytes);
}

bool global_memory_tracking_strategy::is_equal(const memory_tracking_strategy& other) const {
  const auto* const other_strategy{dynamic_cast<const global_memory_tracking_strategy*>(std::addressof(other))};
  return other_strategy != nullptr && other_strategy->register_as_metadata_ == register_as_metadata_ &&
         downstream_equal(other);
}

abstract_strategy_t global_memory_tracking_strategy::get_global_memory_tracking_strategy() noexcept {
  static global_memory_tracking_strategy static_strategy{};
  static abstract_strategy_t strategy_ptr{
      std::shared_ptr<memory_tracking_strategy>{std::shared_ptr<void>{}, &static_strategy}};
  return strategy_ptr;
}

}  // namespace celonis::accelerator::legacy_embedded_ctl