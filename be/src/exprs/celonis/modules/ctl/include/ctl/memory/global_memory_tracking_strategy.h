#pragma once

#include <mutex>

#include "ctl/memory/memory_tracking_strategy.h"

namespace celonis::accelerator::ctl {

/**
 * A memory tracking strategy that will register allocations and de-register deallocations in the global memory
 * consumption tracker.
 */
class global_memory_tracking_strategy final : public memory_tracking_strategy {
 public:
  explicit global_memory_tracking_strategy(abstract_strategy_t downstream_strategy = nullptr) noexcept;

  void register_allocation(std::size_t bytes) override;

  void deregister_allocation(std::size_t bytes) override;

  [[nodiscard]] bool is_equal(const memory_tracking_strategy& other) const override;

  [[nodiscard]] static abstract_strategy_t get_global_memory_tracking_strategy() noexcept;
};

}  // namespace celonis::accelerator::ctl