#pragma once

#include "legacy_embedded_ctl/dynamic_bitset.h"
#include "spawn_allocator_from_context.h"

namespace celonis::accelerator::memory::tracking {

/**
 * @brief Factory that tries to allocate and return a dynamic bitset for 'size' using an allocator spawned from context.
 */
[[nodiscard]] legacy_embedded_ctl::dynamic_bitset_t make_tracked_dynamic_bitset_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, bool default_value,
    const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

[[nodiscard]] legacy_embedded_ctl::dynamic_bitset_t make_tracked_dynamic_bitset_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

/**
 * @brief Factory that tries to allocate a dynamic bitset for 'size' using an allocator spawned from context, returning
 * a shared pointer to this container.
 */
[[nodiscard]] std::shared_ptr<legacy_embedded_ctl::dynamic_bitset_t> make_tracked_shared_dynamic_bitset_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, bool default_value,
    const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

[[nodiscard]] std::shared_ptr<legacy_embedded_ctl::dynamic_bitset_t> make_tracked_shared_dynamic_bitset_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

/**
 * @brief Factory that tries to allocate and return a dynamic bitset (parallel) for 'size' using an allocator spawned
 * from context.
 */
[[nodiscard]] legacy_embedded_ctl::dynamic_bitset_parallel_t make_tracked_dynamic_bitset_parallel_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, bool default_value,
    const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

[[nodiscard]] legacy_embedded_ctl::dynamic_bitset_parallel_t make_tracked_dynamic_bitset_parallel_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

/**
 * @brief Factory that tries to allocate a dynamic bitset (parallel) for 'size' using an allocator spawned from context,
 * returning a shared pointer to this container.
 */
[[nodiscard]] std::shared_ptr<legacy_embedded_ctl::dynamic_bitset_parallel_t>
make_tracked_shared_dynamic_bitset_parallel_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, bool default_value,
    const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

[[nodiscard]] std::shared_ptr<legacy_embedded_ctl::dynamic_bitset_parallel_t>
make_tracked_shared_dynamic_bitset_parallel_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

/*
 ***********************************************************************************************************************
 *** Implementation section of above declarations
 ***********************************************************************************************************************
 */

inline legacy_embedded_ctl::dynamic_bitset_t make_tracked_dynamic_bitset_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const bool default_value,
    const common::execution_context& context, legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::dynamic_bitset_t{
      size, default_value,
      spawn_allocator<legacy_embedded_ctl::dynamic_bitset_t::block_type>(
          context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG), priority, default_value)};
}

inline legacy_embedded_ctl::dynamic_bitset_t make_tracked_dynamic_bitset_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority) {
  return make_tracked_dynamic_bitset_t(size, false, context, priority);
}

inline std::shared_ptr<legacy_embedded_ctl::dynamic_bitset_t> make_tracked_shared_dynamic_bitset_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, bool default_value,
    const common::execution_context& context, legacy_embedded_ctl::utils::allocation_priority priority) {
  return std::make_shared<legacy_embedded_ctl::dynamic_bitset_t>(
      make_tracked_dynamic_bitset_t(size, default_value, context, priority));
}

inline std::shared_ptr<legacy_embedded_ctl::dynamic_bitset_t> make_tracked_shared_dynamic_bitset_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority) {
  return std::make_shared<legacy_embedded_ctl::dynamic_bitset_t>(
      make_tracked_dynamic_bitset_t(size, context, priority));
}

inline legacy_embedded_ctl::dynamic_bitset_parallel_t make_tracked_dynamic_bitset_parallel_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const bool default_value,
    const common::execution_context& context, legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::dynamic_bitset_parallel_t{
      size, default_value,
      spawn_allocator<legacy_embedded_ctl::dynamic_bitset_parallel_t::block_type>(
          context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG), priority, default_value)};
}

inline legacy_embedded_ctl::dynamic_bitset_parallel_t make_tracked_dynamic_bitset_parallel_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority) {
  return make_tracked_dynamic_bitset_parallel_t(size, false, context, priority);
}

inline std::shared_ptr<legacy_embedded_ctl::dynamic_bitset_parallel_t> make_tracked_shared_dynamic_bitset_parallel_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const bool default_value,
    const common::execution_context& context, legacy_embedded_ctl::utils::allocation_priority priority) {
  return std::make_shared<legacy_embedded_ctl::dynamic_bitset_parallel_t>(
      make_tracked_dynamic_bitset_parallel_t(size, default_value, context, priority));
}

inline std::shared_ptr<legacy_embedded_ctl::dynamic_bitset_parallel_t> make_tracked_shared_dynamic_bitset_parallel_t(
    legacy_embedded_ctl::details::bitset_types::size_type size, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority) {
  return std::make_shared<legacy_embedded_ctl::dynamic_bitset_parallel_t>(
      make_tracked_dynamic_bitset_parallel_t(size, context, priority));
}

}  // namespace celonis::accelerator::memory::tracking
