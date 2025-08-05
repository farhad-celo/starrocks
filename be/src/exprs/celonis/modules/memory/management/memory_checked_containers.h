#pragma once

#include <scoped_allocator>
#include <unordered_set>
#include <vector>

#include <boost/multi_index_container.hpp>
#include <bytell_hash_map.hpp>

#include "legacy_embedded_ctl/type_traits.h"
#include "modules/memory/tracking/spawn_allocator_from_context.h"

/**
 * Provides container types that check whether there is enough memory for an allocation.
 * If there is not enough memory available the allocation throws with legacy_embedded_ctl::short_of_memory_exception.
 *
 * The matching allocator for each container is instantiated by calling checked_allocator<CONTAINER_TYPE>.
 *
 * To avoid the memory check overhead for small allocations the minimum size of bytes
 * to do the memory check can be configured for each container.
 */
namespace celonis::accelerator::memory::management {

/**
 * memory checked std::vector
 */
template <typename TYPE>
using checked_vector_t = std::vector<TYPE, std::scoped_allocator_adaptor<legacy_embedded_ctl::resource_owning_allocator<TYPE>>>;
constexpr size_t MIN_BYTES_STD_VECTOR_MEMORY_CHECK = 65536;

template <typename CONTAINER_TYPE,
          typename std::enable_if_t<legacy_embedded_ctl::is_base_of_template_v<std::vector, CONTAINER_TYPE>>* = nullptr>
[[nodiscard]] auto checked_allocator(const common::execution_context& context,
                                     const legacy_embedded_ctl::utils::allocation_reason& reason,
                                     legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW) {
  return memory::tracking::spawn_allocator<typename CONTAINER_TYPE::value_type>(context, reason, priority, false,
                                                                                MIN_BYTES_STD_VECTOR_MEMORY_CHECK);
}

/**
 * memory checked ska::bytell_hash_map
 */
template <typename KEY_TYPE, typename VALUE_TYPE, typename HASH_FUNCTION = std::hash<KEY_TYPE>,
          typename EQUALS_FUNCTION = std::equal_to<KEY_TYPE>>
using checked_ska_hash_map_t = ska::bytell_hash_map<
    KEY_TYPE, VALUE_TYPE, HASH_FUNCTION, EQUALS_FUNCTION,
    std::scoped_allocator_adaptor<legacy_embedded_ctl::resource_owning_allocator<std::pair<KEY_TYPE, VALUE_TYPE>>>>;
constexpr size_t MIN_BYTES_SKA_HASHMAP_MEMORY_CHECK = 65536;

template <typename CONTAINER_TYPE,
          typename std::enable_if_t<legacy_embedded_ctl::is_base_of_template_v<ska::bytell_hash_map, CONTAINER_TYPE>>* = nullptr>
[[nodiscard]] auto checked_allocator(const common::execution_context& context,
                                     const legacy_embedded_ctl::utils::allocation_reason& reason,
                                     legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW) {
  return memory::tracking::spawn_allocator<
      std::pair<typename CONTAINER_TYPE::key_type, typename CONTAINER_TYPE::mapped_type>>(
      context, reason, priority, false, MIN_BYTES_SKA_HASHMAP_MEMORY_CHECK);
}

/**
 * memory checked ska::bytell_hash_set
 */
template <typename TYPE, typename HASH_FUNCTION = std::hash<TYPE>, typename EQUALS_FUNCTION = std::equal_to<TYPE>>
using checked_ska_hash_set_t =
    ska::bytell_hash_set<TYPE, HASH_FUNCTION, EQUALS_FUNCTION,
                         std::scoped_allocator_adaptor<legacy_embedded_ctl::resource_owning_allocator<TYPE>>>;
constexpr size_t MIN_BYTES_SKA_HASHSET_MEMORY_CHECK = 65536;

template <typename CONTAINER_TYPE,
          typename std::enable_if_t<legacy_embedded_ctl::is_base_of_template_v<ska::bytell_hash_set, CONTAINER_TYPE>>* = nullptr>
[[nodiscard]] auto checked_allocator(const common::execution_context& context,
                                     const legacy_embedded_ctl::utils::allocation_reason& reason,
                                     legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW) {
  return memory::tracking::spawn_allocator<typename CONTAINER_TYPE::key_type>(context, reason, priority, false,
                                                                              MIN_BYTES_SKA_HASHSET_MEMORY_CHECK);
}

/**
 * memory checked std::unordered_set
 */
template <typename TYPE, typename HASH_FUNCTION = std::hash<TYPE>, typename EQUALS_FUNCTION = std::equal_to<TYPE>>
using checked_unordered_set = std::unordered_set<TYPE, HASH_FUNCTION, EQUALS_FUNCTION,
                                                 std::scoped_allocator_adaptor<legacy_embedded_ctl::resource_owning_allocator<TYPE>>>;
constexpr size_t THRESHOLD_BYTES_UNORDERED_SET_MEMORY_CHECK = 65536;

template <typename CONTAINER_TYPE,
          typename std::enable_if_t<legacy_embedded_ctl::is_base_of_template_v<std::unordered_set, CONTAINER_TYPE>>* = nullptr>
[[nodiscard]] auto checked_allocator(const common::execution_context& context,
                                     const legacy_embedded_ctl::utils::allocation_reason& reason,
                                     legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW) {
  return memory::tracking::spawn_batched_allocator<typename CONTAINER_TYPE::key_type>(
      context, reason, priority, false, THRESHOLD_BYTES_UNORDERED_SET_MEMORY_CHECK,
      THRESHOLD_BYTES_UNORDERED_SET_MEMORY_CHECK);
}

/**
 * memory checked std::unordered_map
 */
template <typename KEY, typename T, typename HASH_FUNCTION = std::hash<KEY>,
          typename EQUALS_FUNCTION = std::equal_to<KEY>>
using checked_unordered_map =
    std::unordered_map<KEY, T, HASH_FUNCTION, EQUALS_FUNCTION,
                       std::scoped_allocator_adaptor<legacy_embedded_ctl::resource_owning_allocator<std::pair<const KEY, T>>>>;
constexpr size_t THRESHOLD_BYTES_UNORDERED_MAP_MEMORY_CHECK = 65536;

template <typename CONTAINER_TYPE,
          typename std::enable_if_t<legacy_embedded_ctl::is_base_of_template_v<std::unordered_map, CONTAINER_TYPE>>* = nullptr>
[[nodiscard]] auto checked_allocator(const common::execution_context& context,
                                     const legacy_embedded_ctl::utils::allocation_reason& reason,
                                     legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW) {
  return memory::tracking::spawn_batched_allocator<typename CONTAINER_TYPE::value_type>(
      context, reason, priority, false, THRESHOLD_BYTES_UNORDERED_MAP_MEMORY_CHECK,
      THRESHOLD_BYTES_UNORDERED_MAP_MEMORY_CHECK);
}

/**
 * memory checked boost::multi_index
 */
template <typename TYPE, typename INDEX>
using checked_multi_index_t =
    //    boost::multi_index::multi_index_container<TYPE, INDEX>;
    boost::multi_index::multi_index_container<TYPE, INDEX,
                                              std::scoped_allocator_adaptor<legacy_embedded_ctl::resource_owning_allocator<TYPE>>>;
constexpr size_t THRESHOLD_BYTES_MULTI_INDEX_MEMORY_CHECK = 65536;
template <
    typename CONTAINER_TYPE,
    typename std::enable_if_t<legacy_embedded_ctl::is_base_of_template_v<boost::multi_index_container, CONTAINER_TYPE>>* = nullptr>
[[nodiscard]] auto checked_node_allocator(
    const common::execution_context& context, const legacy_embedded_ctl::utils::allocation_reason& reason,
    legacy_embedded_ctl::utils::allocation_priority priority = legacy_embedded_ctl::utils::allocation_priority::LOW) {
  return memory::tracking::spawn_batched_allocator<typename CONTAINER_TYPE::key_type>(
      context, reason, priority, false, THRESHOLD_BYTES_MULTI_INDEX_MEMORY_CHECK,
      THRESHOLD_BYTES_MULTI_INDEX_MEMORY_CHECK);
}

}  // namespace celonis::accelerator::memory::management
