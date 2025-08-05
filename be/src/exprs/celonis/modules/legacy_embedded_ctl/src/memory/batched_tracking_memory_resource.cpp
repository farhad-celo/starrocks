#include "legacy_embedded_ctl/memory/batched_tracking_memory_resource.h"

#include <utility>

#include "legacy_embedded_ctl/bits/memory_utils.h"
#include "legacy_embedded_ctl/memory/memory_consumption_tracker.h"

namespace celonis::accelerator::legacy_embedded_ctl {

namespace {

void throw_or_register(std::size_t bytes, const utils::allocation_reason& reason) {
  details::throw_if_not_enough_memory_for_allocation(bytes, reason.to_string());
  global_memory_consumption_tracker::get_consumption_tracker().register_allocation(bytes);
}

}  // namespace

batched_tracking_memory_resource::batched_tracking_memory_resource(abstract_resource_t upstream_resource,
                                                                   memory_tracking_options options) noexcept
    : memory_resource_with_upstream_resource{std::move(upstream_resource)}, options_{std::move(options)} {
  legacy_embedded_ctl::global_memory_consumption_tracker::get_consumption_tracker().register_batched_tracker();
}

batched_tracking_memory_resource::~batched_tracking_memory_resource() noexcept {
  auto remaining{excess_registered_.load(std::memory_order_relaxed)};
  if (remaining != 0) {
    global_memory_consumption_tracker::get_consumption_tracker().deregister_allocation(remaining);
  }
  legacy_embedded_ctl::global_memory_consumption_tracker::get_consumption_tracker().deregister_batched_tracker();
}

void* batched_tracking_memory_resource::do_allocate(std::size_t bytes, std::size_t alignment) {
  if (options_.threshold_for_memory_check_in_bytes < bytes) {
    return allocate_above_threshold(bytes, alignment);
  }
  return allocate_up_to_threshold(bytes, alignment);
}

void batched_tracking_memory_resource::do_deallocate(void* p, std::size_t bytes, std::size_t alignment) {
  if (options_.threshold_for_memory_check_in_bytes < bytes) {
    deallocate_above_threshold(p, bytes, alignment);
  } else {
    deallocate_up_to_threshold(p, bytes, alignment);
  }
}

void* batched_tracking_memory_resource::allocate_up_to_threshold(std::size_t bytes, std::size_t alignment) {
  const auto previous_excess{excess_registered_.fetch_sub(static_cast<excess_type>(bytes), std::memory_order_relaxed)};
  try {
    // if after subtraction, the excess is still non-negative, we can allocate straight away.
    // otherwise, we need to check whether there is enough memory left, and register more
    if (std::cmp_less(previous_excess, bytes)) {
      register_to_threshold();
    }
    return get_upstream_memory_resource()->allocate(bytes, alignment);
  } catch (const std::exception&) {
    excess_registered_.fetch_add(static_cast<excess_type>(bytes), std::memory_order_relaxed);
    throw;
  }
}

void* batched_tracking_memory_resource::allocate_above_threshold(std::size_t bytes, std::size_t alignment) {
  throw_or_register(bytes, options_.reason);
  try {
    return get_upstream_memory_resource()->allocate(bytes, alignment);
  } catch (const std::exception&) {
    global_memory_consumption_tracker::get_consumption_tracker().deregister_allocation(bytes);
    throw;
  }
}

void batched_tracking_memory_resource::deallocate_up_to_threshold(void* p, std::size_t bytes, std::size_t alignment) {
  get_upstream_memory_resource()->deallocate(p, bytes, alignment);
  const auto previous_excess{excess_registered_.fetch_add(static_cast<excess_type>(bytes), std::memory_order_relaxed)};
  if (std::cmp_less_equal(2 * options_.threshold_for_memory_check_in_bytes, previous_excess + bytes)) {
    deregister_blocking();
  }
}

void batched_tracking_memory_resource::deallocate_above_threshold(void* p, std::size_t bytes, std::size_t alignment) {
  get_upstream_memory_resource()->deallocate(p, bytes, alignment);
  global_memory_consumption_tracker::get_consumption_tracker().deregister_allocation(bytes);
}

bool batched_tracking_memory_resource::do_is_equal(const abstract_resource_base& other) const noexcept {
  const auto* const other_batched{dynamic_cast<const batched_tracking_memory_resource*>(std::addressof(other))};
  return other_batched != nullptr &&
         get_upstream_memory_resource()->is_equal(*other_batched->get_upstream_memory_resource());
}

void batched_tracking_memory_resource::register_to_threshold() {
  // (de-)registering can only happen when the lock is held.
  // We effectively serialize all allocations that may have exceeded the currently registered quota.
  // This is important so that we either only register once, or have all allocation requests fail.
  // (because there is not enough memory left)
  std::scoped_lock lock{register_lock_};
  if (const auto current_excess{excess_registered_.load(std::memory_order_relaxed)}; current_excess < 0) {
    const auto to_register{options_.threshold_for_memory_check_in_bytes - current_excess};
    throw_or_register(to_register, options_.reason);
    excess_registered_.fetch_add(static_cast<excess_type>(to_register));
  }
}

void batched_tracking_memory_resource::deregister_blocking() noexcept {
  std::scoped_lock lock{register_lock_};
  if (const auto current_excess{excess_registered_.load(std::memory_order_relaxed)};
      std::cmp_less_equal(2 * options_.threshold_for_memory_check_in_bytes, current_excess)) {
    const auto to_deregister{current_excess - options_.threshold_for_memory_check_in_bytes};
    excess_registered_.fetch_sub(static_cast<excess_type>(to_deregister), std::memory_order_relaxed);
    global_memory_consumption_tracker::get_consumption_tracker().deregister_allocation(to_deregister);
  }
}

}  // namespace celonis::accelerator::legacy_embedded_ctl