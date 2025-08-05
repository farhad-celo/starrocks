#pragma once

#include <atomic>
#include <mutex>
#include <utility>

#include "legacy_embedded_ctl/memory/memory_resource_with_upstream_resource.h"
#include "legacy_embedded_ctl/memory/memory_tracking_options.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * A memory resource that will register allocations (de-register de-allocations), but only in larger chunks.
 * When the current chunk would be exceeded by an allocation, we register what's missing and a threshold on top.
 * Likewise, if de-allocations would end in having twice the threshold over-registered, we de-register to the threshold.
 * In other words, we keep the amount of over-registered memory in [0, 2*threshold) and, if we leave that range,
 * (de-)register enough to bounce back to a value equal to the threshold.
 *
 * This implementation is thread-safe.
 */
class batched_tracking_memory_resource final : public memory_resource_with_upstream_resource {
 public:
  explicit batched_tracking_memory_resource(abstract_resource_t upstream_resource = nullptr,
                                            memory_tracking_options options = {}) noexcept;

  ~batched_tracking_memory_resource() noexcept override;

 private:
  using excess_type = std::make_signed_t<size_t>;

  void* do_allocate(std::size_t bytes, std::size_t alignment) override;

  void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;

  [[nodiscard]] bool do_is_equal(const abstract_resource_base& other) const noexcept override;

  void* allocate_up_to_threshold(std::size_t bytes, std::size_t alignment);
  void* allocate_above_threshold(std::size_t bytes, std::size_t alignment);
  void deallocate_up_to_threshold(void* p, std::size_t bytes, std::size_t alignment);
  void deallocate_above_threshold(void* p, std::size_t bytes, std::size_t alignment);

  void register_to_threshold();

  void deregister_blocking() noexcept;

  memory_tracking_options options_{};
  std::atomic<excess_type> excess_registered_{0};
  std::mutex register_lock_{};
};

}  // namespace celonis::accelerator::legacy_embedded_ctl