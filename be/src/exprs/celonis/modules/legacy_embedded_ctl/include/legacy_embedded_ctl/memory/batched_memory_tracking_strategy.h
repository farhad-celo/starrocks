#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

#include "legacy_embedded_ctl/memory/memory_tracking_strategy.h"

namespace celonis::accelerator::legacy_embedded_ctl::memory {

/**
 * A batched memory tracking strategy that will only register memory in downstream tracking strategies in batches rather
 * than for each allocation. The strategy registers excessively and keeps track of the currently registered excess.
 * If an allocation is registered that is smaller than the current excess, it will just consume the corresponding part
 * of the excess and the allocation will not be forwarded to the downstream strategies. If an allocation is registered
 * that is bigger than the excess, then ( in addition to consuming the current excess ) <minimum_batch_size> bytes are
 * registered in addition so that we never register less than <minimum_batch_size> bytes at a time.
 */
class batched_memory_tracking_strategy final : public memory_tracking_strategy {
 public:
  using excess_type = std::make_signed_t<std::size_t>;

  explicit batched_memory_tracking_strategy(excess_type minimum_batch_size) noexcept;

  batched_memory_tracking_strategy(abstract_strategy_t downstream_strategy, excess_type minimum_batch_size) noexcept;

  ~batched_memory_tracking_strategy() override;

  batched_memory_tracking_strategy(batched_memory_tracking_strategy const&) = delete;
  batched_memory_tracking_strategy& operator=(batched_memory_tracking_strategy const&) = delete;
  batched_memory_tracking_strategy(batched_memory_tracking_strategy&&) = delete;
  batched_memory_tracking_strategy& operator=(batched_memory_tracking_strategy&&) = delete;

  void register_allocation(std::size_t bytes) override;

  void deregister_allocation(std::size_t bytes) override;

  [[nodiscard]] bool is_equal(const memory_tracking_strategy& other) const override;

 private:
  void register_up_to_threshold(std::size_t bytes);
  void deregister_up_to_threshold(std::size_t bytes);

  void register_blocking();
  void deregister_blocking();

  excess_type minimum_batch_size_;
  std::atomic<excess_type> excess_registered_{0};
  std::mutex register_lock_{};
};

}  // namespace celonis::accelerator::legacy_embedded_ctl::memory