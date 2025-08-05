#pragma once

#include "legacy_embedded_ctl/memory/memory_resource_with_upstream_resource.h"
#include "legacy_embedded_ctl/memory/memory_tracking_strategy.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * A memory resource that will register allocations and de-register deallocations with a given memory tracking strategy.
 */
class tracking_memory_resource final : public memory_resource_with_upstream_resource {
 public:
  explicit tracking_memory_resource(
      abstract_strategy_t tracking_strategy = memory_tracking_strategy::get_default_strategy(),
      abstract_resource_t upstream_resource = nullptr) noexcept
      : memory_resource_with_upstream_resource{std::move(upstream_resource)},
        tracking_strategy_{std::move(tracking_strategy)} {}

  [[nodiscard]] static abstract_resource_t get_global_tracking_resource() noexcept;

 private:
  [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override;

  void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;

  [[nodiscard]] bool do_is_equal(const abstract_resource_base& other) const noexcept override;

  abstract_strategy_t tracking_strategy_;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl