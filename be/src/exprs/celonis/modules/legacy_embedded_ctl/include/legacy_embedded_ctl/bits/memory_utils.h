#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "legacy_embedded_ctl/byte_literals.h"
#include "legacy_embedded_ctl/exception.h"
#include "legacy_embedded_ctl/memory/deregistering_deleter.h"
#include "legacy_embedded_ctl/memory/memory_consumption_tracker.h"
#include "legacy_embedded_ctl/source_location_fwd.h"
#include "legacy_embedded_ctl/utils/allocation_priority.h"
#include "legacy_embedded_ctl/utils/allocation_reason.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * Build a string containing the n largest allocations which were not deallocated and information about the source files
 * and lines of the respective allocations. Note that allocations are tracked using a circular buffer of size 2000, so
 * any allocations older than the most recent 2000 are not considered.
 * @param n Number of allocations to show (sort descending by size).
 * @return A string containing the list of allocations with size and source.
 */
[[nodiscard]] std::string build_string_containing_n_largest_open_allocations(size_t n);

namespace details {

/**
 * @brief Used to validate that enough memory is available for the allocation request of 'bytes_to_allocate' bytes
 * @param bytes_to_allocate the number of bytes requested in the allocation request
 * @param reason the allocation reason string
 * @param priority the priority the allocation request has (a higher value leads to lower maintained memory threshold)
 * @throws short_of_memory_exception when the allocation can not be fulfilled due to memory pressure
 */
void throw_if_not_enough_memory_for_allocation(std::size_t bytes_to_allocate, std::string_view reason,
                                               utils::allocation_priority priority = utils::allocation_priority::LOW);

void do_track_allocation(std::uintptr_t allocation_address, std::size_t allocation_size,
                         const source_location& allocation_location, bool is_value_init);

void do_track_deallocation(std::uintptr_t allocation_address, std::size_t deallocation_size,
                           const source_location& allocation_location, bool is_value_init);

void log_large_allocation_warning(std::size_t number_of_elements_to_allocate, std::size_t byte_size_of_each_element,
                                  const utils::allocation_reason& allocation_reason);

void log_memory_tracking_state(size_t mem_available_kib, size_t mem_estimated_available_kib, size_t in_use_by_process,
                               const std::string& prefix = "");

template <typename T, typename ALLOCATOR_TYPE, bool DO_CONSTRUCT>
[[nodiscard]] deregistering_unique_ptr<T[], ALLOCATOR_TYPE> do_alloc(ALLOCATOR_TYPE& allocator,
                                                                     const std::size_t size) {
  // create temporary unique pointer with deallocating deleter, so memory gets deallocated in case of exception
  auto deallocating_deleter{
      [&](auto* ptr) { std::allocator_traits<ALLOCATOR_TYPE>::deallocate(allocator, ptr, size); }};
  std::unique_ptr<T[], decltype(deallocating_deleter)> alloc_ptr{
      std::allocator_traits<ALLOCATOR_TYPE>::allocate(allocator, size), deallocating_deleter};
  if constexpr (DO_CONSTRUCT) {
    std::size_t i{0};
    try {
      for (; i < size; i++) {
        std::allocator_traits<ALLOCATOR_TYPE>::construct(allocator, &alloc_ptr[i]);
      }
    } catch (...) {
      // undo creation that were already performed in case a constructor throws
      for (std::size_t j{0}; j != i; j++) {
        std::allocator_traits<ALLOCATOR_TYPE>::destroy(allocator, &alloc_ptr[j]);
      }
      throw;
    }
  }

  return make_deregistering_unique<T[], ALLOCATOR_TYPE>(alloc_ptr.release(), size, std::move(allocator));
}

inline auto make_deallocation_registerer(const source_location& allocation_location, bool value_init) {
  return [allocation_location, value_init](std::uintptr_t allocation_address, std::size_t deallocation_size) {
    do_track_deallocation(allocation_address, deallocation_size, allocation_location, value_init);
  };
}

template <typename T, typename ALLOCATOR_TYPE>
[[nodiscard]] deregistering_unique_ptr<T[], ALLOCATOR_TYPE> do_tracked_alloc_value_init(const std::size_t size,
                                                                                        ALLOCATOR_TYPE allocator) {
  return do_alloc<T, ALLOCATOR_TYPE, true>(allocator, size);
}

template <typename T, typename ALLOCATOR_TYPE>
[[nodiscard]] deregistering_unique_ptr<T[], ALLOCATOR_TYPE> do_tracked_alloc_overwrite_init(const std::size_t size,
                                                                                            ALLOCATOR_TYPE allocator) {
  if constexpr (!std::is_trivially_default_constructible_v<T>) {
    return do_tracked_alloc_value_init<T, ALLOCATOR_TYPE>(size, allocator);
  }
  return do_alloc<T, ALLOCATOR_TYPE, false>(allocator, size);
}

/**
 * @brief Internal implementation of the tracked allocation procedure
 * @tparam T the data type of the elements for the array to allocate
 * @tparam DO_VALUE_INIT whether the array values should be value initialized or not
 * @param size the number of elements of type 'T' to allocate
 * @param reason the allocation reason to provide context in error scenarios
 * @return the successfully allocated array with 'size' elements of type 'T'
 * TODO(n.weber): In the future it might make sense to pack all allocation related meta data (such as the alloc reason,
 * alloc prio, ..) into a more general config structure in order to reduce the bloat of the interface.
 */
template <typename T, typename ALLOCATOR_TYPE, bool DO_VALUE_INIT>
[[nodiscard]] deregistering_unique_ptr<T[], ALLOCATOR_TYPE> tracked_allocation(const std::size_t size,
                                                                               const utils::allocation_reason& reason,
                                                                               ALLOCATOR_TYPE allocator) {
  const std::size_t request_size_in_bytes = sizeof(T) * size;  // NOLINT(bugprone-sizeof-expression)
  if (request_size_in_bytes > 200_GiB) {
    log_large_allocation_warning(size, sizeof(T), reason);  // NOLINT(bugprone-sizeof-expression)
  }

  try {
    if constexpr (DO_VALUE_INIT) {
      return do_tracked_alloc_value_init<T, ALLOCATOR_TYPE>(size, std::move(allocator));
    } else {
      return do_tracked_alloc_overwrite_init<T, ALLOCATOR_TYPE>(size, std::move(allocator));
    }
  } catch (const std::bad_alloc&) {
    throw bad_alloc{size, sizeof(T), reason.to_string()};  // NOLINT(bugprone-sizeof-expression)
  }
}

}  // namespace details

}  // namespace celonis::accelerator::legacy_embedded_ctl