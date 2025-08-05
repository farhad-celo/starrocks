#pragma once

#include <mutex>

#include "legacy_embedded_ctl/memory/memory_tracking_strategy.h"
#include "legacy_embedded_ctl/named_type.h"

namespace celonis::accelerator::legacy_embedded_ctl {

using register_as_metadata_t = legacy_embedded_ctl::named_type<bool, struct register_as_metadata_tag>;

/**
 * A memory tracking strategy that will register allocations and de-register deallocations in the global memory
 * consumption tracker.
 */
class global_memory_tracking_strategy final : public memory_tracking_strategy {
 public:
  explicit global_memory_tracking_strategy(abstract_strategy_t downstream_strategy = nullptr,
                                           register_as_metadata_t register_as_metadata = register_as_metadata_t{
                                               false}) noexcept;

  void register_allocation(std::size_t bytes) override;

  void deregister_allocation(std::size_t bytes) override;

  [[nodiscard]] bool is_equal(const memory_tracking_strategy& other) const override;

  [[nodiscard]] static abstract_strategy_t get_global_memory_tracking_strategy() noexcept;

 private:
  const bool register_as_metadata_;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl