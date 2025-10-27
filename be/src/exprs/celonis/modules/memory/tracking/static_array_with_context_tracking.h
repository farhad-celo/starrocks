#pragma once

#include "legacy_embedded_ctl/static_array.h"
#include "spawn_allocator_from_context.h"

namespace celonis::accelerator::memory::tracking {

/**
 * @brief Factory to create a static array with the given number of elements (value initialized)
 * @note Value-initialization ('T()') for built-in types basically means zero-initialization. If you intend to
 * overwrite all elements anyways, prefer using the 'make_static_array_for_overwrite' factory (for built-in types) as
 * this has no value-initialization overhead. The construction is equivalent to std::vector<T>(size) in regards to
 * initialization.
 */
template <typename T>
[[nodiscard]] legacy_embedded_ctl::static_array<T> make_static_array_value_init(
    size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

/**
 * @brief Factory to create a static array with the given number of elements (default initialized)
 * @note This factory only makes sense for built-in types if for performance reasons one wants to be explicit whether to
 * do default or value initialization of the underlying storage. For custom (i.e., class) types, value initialization
 * does not have any performance impact (use the regular, less verbose, 'make_static_array' factory in this case).
 */
template <typename T>
[[nodiscard]] legacy_embedded_ctl::static_array<T> make_static_array_for_overwrite(
    size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

/**
 * @brief Factory to create a static array with the given number of elements
 */
template <typename T>
[[nodiscard]] legacy_embedded_ctl::static_array<T> make_static_array(
    size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

/**
 * @brief Factory to create a static array with the given number of elements initialized to the given 'init_value'
 */
template <typename T>
[[nodiscard]] legacy_embedded_ctl::static_array<T> make_static_array(
    size_t size, const T& init_value, const legacy_embedded_ctl::utils::allocation_reason& reason,
    const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

/**
 * @brief Factory to create a static array with the given elements in the initializer list
 */
template <typename T>
[[nodiscard]] legacy_embedded_ctl::static_array<T> make_static_array(
    std::initializer_list<T> init, const legacy_embedded_ctl::utils::allocation_reason& reason,
    const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

/**
 * @brief What follows are the equivalent declarations as above but for arrays of shared ownership
 */
template <typename T>
[[nodiscard]] legacy_embedded_ctl::shared_static_array<T> make_shared_static_array_value_init(
    size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] legacy_embedded_ctl::shared_static_array<T> make_shared_static_array_for_overwrite(
    size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] legacy_embedded_ctl::shared_static_array<T> make_shared_static_array(
    size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason, const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] legacy_embedded_ctl::shared_static_array<T> make_shared_static_array(
    size_t size, const T& init_value, const legacy_embedded_ctl::utils::allocation_reason& reason,
    const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] legacy_embedded_ctl::shared_static_array<T> make_shared_static_array(
    std::initializer_list<T> init, const legacy_embedded_ctl::utils::allocation_reason& reason,
    const common::execution_context& context,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW);

/*
 ***********************************************************************************************************************
 *** Implementation section of above declarations
 ***********************************************************************************************************************
 */

template <typename T>
inline legacy_embedded_ctl::static_array<T> make_static_array_value_init(
    size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason, const common::execution_context& context,
    const legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::make_static_array_value_init<T>(size, reason,
                                                              spawn_allocator<T>(context, reason, priority));
}

template <typename T>
inline legacy_embedded_ctl::static_array<T> make_static_array_for_overwrite(
    size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason, const common::execution_context& context,
    const legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::make_static_array_for_overwrite<T>(size, reason,
                                                                 spawn_allocator<T>(context, reason, priority, false));
}

template <typename T>
inline legacy_embedded_ctl::static_array<T> make_static_array(
    const size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason,
    const common::execution_context& context, const legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::make_static_array<T>(size, reason, spawn_allocator<T>(context, reason, priority));
}

template <typename T>
inline legacy_embedded_ctl::static_array<T> make_static_array(
    const size_t size, const T& init_value, const legacy_embedded_ctl::utils::allocation_reason& reason,
    const common::execution_context& context, const legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::make_static_array<T>(size, init_value, reason,
                                                   spawn_allocator<T>(context, reason, priority));
}

template <typename T>
inline legacy_embedded_ctl::static_array<T> make_static_array(
    std::initializer_list<T> init, const legacy_embedded_ctl::utils::allocation_reason& reason,
    const common::execution_context& context, const legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::make_static_array<T>(std::move(init), reason,
                                                   spawn_allocator<T>(context, reason, priority));
}

template <typename T>
inline legacy_embedded_ctl::shared_static_array<T> make_shared_static_array_value_init(
    size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason, const common::execution_context& context,
    const legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::make_shared_static_array_value_init<T>(size, reason,
                                                                     spawn_allocator<T>(context, reason, priority));
}

template <typename T>
inline legacy_embedded_ctl::shared_static_array<T> make_shared_static_array_for_overwrite(
    size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason, const common::execution_context& context,
    const legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::make_shared_static_array_for_overwrite<T>(
      size, reason, spawn_allocator<T>(context, reason, priority, false));
}

template <typename T>
inline legacy_embedded_ctl::shared_static_array<T> make_shared_static_array(
    const size_t size, const legacy_embedded_ctl::utils::allocation_reason& reason,
    const common::execution_context& context, const legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::make_shared_static_array<T>(size, reason, spawn_allocator<T>(context, reason, priority));
}

template <typename T>
inline legacy_embedded_ctl::shared_static_array<T> make_shared_static_array(
    const size_t size, const T& init_value, const legacy_embedded_ctl::utils::allocation_reason& reason,
    const common::execution_context& context, const legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::make_shared_static_array<T>(size, init_value, reason,
                                                          spawn_allocator<T>(context, reason, priority));
}

template <typename T>
inline legacy_embedded_ctl::shared_static_array<T> make_shared_static_array(
    std::initializer_list<T> init, const legacy_embedded_ctl::utils::allocation_reason& reason,
    const common::execution_context& context, const legacy_embedded_ctl::utils::allocation_priority priority) {
  return legacy_embedded_ctl::make_shared_static_array<T>(std::move(init), reason,
                                                          spawn_allocator<T>(context, reason, priority));
}

}  // namespace celonis::accelerator::memory::tracking
