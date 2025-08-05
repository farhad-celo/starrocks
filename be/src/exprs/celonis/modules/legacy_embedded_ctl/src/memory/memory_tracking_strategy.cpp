#include "legacy_embedded_ctl/memory/memory_tracking_strategy.h"

#include "legacy_embedded_ctl/memory/global_memory_tracking_strategy.h"

namespace celonis::accelerator::legacy_embedded_ctl {

memory_tracking_strategy::memory_tracking_strategy(abstract_strategy_t downstream_tracking_strategy) noexcept
    : downstream_strategy_{std::move(downstream_tracking_strategy)} {}

abstract_strategy_t memory_tracking_strategy::get_default_strategy() noexcept {
  return global_memory_tracking_strategy::get_global_memory_tracking_strategy();
}

void memory_tracking_strategy::register_downstream(const std::size_t bytes) const {
  if (downstream_strategy_ != nullptr) {
    downstream_strategy_->register_allocation(bytes);
  }
}

void memory_tracking_strategy::deregister_downstream(const std::size_t bytes) const {
  if (downstream_strategy_ != nullptr) {
    downstream_strategy_->deregister_allocation(bytes);
  }
}

bool memory_tracking_strategy::downstream_equal(const memory_tracking_strategy& other) const {
  return this->downstream_strategy_ == other.downstream_strategy_ ||
         (this->downstream_strategy_ != nullptr && other.downstream_strategy_ != nullptr &&
          this->downstream_strategy_->is_equal(*other.downstream_strategy_));
}

abstract_strategy_t memory_tracking_strategy::get_downstream_strategy() const { return downstream_strategy_; }

}  // namespace celonis::accelerator::legacy_embedded_ctl
