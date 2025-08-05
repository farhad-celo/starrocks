#include "legacy_embedded_ctl/memory/batched_memory_tracking_strategy.h"

#include "legacy_embedded_ctl/memory/memory_consumption_tracker.h"

namespace celonis::accelerator::legacy_embedded_ctl::memory {

batched_memory_tracking_strategy::batched_memory_tracking_strategy(excess_type minimum_batch_size) noexcept
    : batched_memory_tracking_strategy(nullptr, minimum_batch_size) {}

batched_memory_tracking_strategy::batched_memory_tracking_strategy(abstract_strategy_t downstream_strategy,
                                                                   excess_type minimum_batch_size) noexcept
    : memory_tracking_strategy{std::move(downstream_strategy)}, minimum_batch_size_{minimum_batch_size} {
  legacy_embedded_ctl::global_memory_consumption_tracker::get_consumption_tracker().register_batched_tracker();
}

batched_memory_tracking_strategy::~batched_memory_tracking_strategy() {
  auto remaining{excess_registered_.load(std::memory_order_relaxed)};
  if (remaining != 0) {
    deregister_downstream(remaining);
  }
  legacy_embedded_ctl::global_memory_consumption_tracker::get_consumption_tracker().deregister_batched_tracker();
}

void batched_memory_tracking_strategy::register_allocation(const std::size_t bytes) {
  if (std::cmp_less(minimum_batch_size_, bytes)) {
    register_downstream(bytes);
  } else {
    register_up_to_threshold(bytes);
  }
}

void batched_memory_tracking_strategy::deregister_allocation(const std::size_t bytes) {
  if (std::cmp_less(minimum_batch_size_, bytes)) {
    deregister_downstream(bytes);
  } else {
    deregister_up_to_threshold(bytes);
  }
}

bool batched_memory_tracking_strategy::is_equal(const memory_tracking_strategy& other) const {
  const auto* const other_batched{dynamic_cast<const batched_memory_tracking_strategy*>(std::addressof(other))};
  return other_batched != nullptr && downstream_equal(other);
}

void batched_memory_tracking_strategy::register_up_to_threshold(const std::size_t bytes) {
  const auto previous_excess{excess_registered_.fetch_sub(static_cast<excess_type>(bytes), std::memory_order_relaxed)};
  try {
    // if after subtraction, the excess is still non-negative, we can allocate straight away.
    // otherwise, we need to check whether there is enough memory left, and register more
    if (std::cmp_less(previous_excess, bytes)) {
      register_blocking();
    }
  } catch (const std::exception&) {
    excess_registered_.fetch_add(static_cast<excess_type>(bytes), std::memory_order_relaxed);
    throw;
  }
}

void batched_memory_tracking_strategy::deregister_up_to_threshold(const std::size_t bytes) {
  const auto previous_excess{excess_registered_.fetch_add(static_cast<excess_type>(bytes), std::memory_order_relaxed)};
  if (std::cmp_less_equal(2 * minimum_batch_size_, previous_excess + bytes)) {
    deregister_blocking();
  }
}

// (de-)registering of excess can only happen when the lock is held.
// We effectively serialize all allocations that may have exceeded the currently registered quota.
// This is important so that we either only register once, or have all allocation requests fail.
// (because there is not enough memory left)
void batched_memory_tracking_strategy::register_blocking() {
  std::scoped_lock lock{register_lock_};
  if (const auto current_excess{excess_registered_.load(std::memory_order_relaxed)}; current_excess < 0) {
    const auto to_register{minimum_batch_size_ - current_excess};
    register_downstream(to_register);
    excess_registered_.fetch_add(to_register);
  }
}

void batched_memory_tracking_strategy::deregister_blocking() {
  std::scoped_lock lock{register_lock_};
  if (const auto current_excess{excess_registered_.load(std::memory_order_relaxed)};
      std::cmp_less_equal(2 * minimum_batch_size_, current_excess)) {
    const auto to_deregister{current_excess - minimum_batch_size_};
    excess_registered_.fetch_sub(to_deregister, std::memory_order_relaxed);
    deregister_downstream(to_deregister);
  }
}

}  // namespace celonis::accelerator::legacy_embedded_ctl::memory