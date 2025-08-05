#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <scoped_allocator>
#include <string_view>
#include <vector>

#include "legacy_embedded_ctl/array_view.h"
#include "legacy_embedded_ctl/buffer_fwd.h"
#include "legacy_embedded_ctl/conversion.h"
#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/exceptions.h"

namespace celonis::accelerator::legacy_embedded_ctl {

namespace details {

template <typename TYPE>
class buffer_allocation_node final {
 public:
  static constexpr std::string_view BUFFER_ALLOC_MSG{"Allocation for the buffer"};  // NOLINT(cert-err58-cpp)

  using value_type = TYPE;
  using size_type = size_t;
  using allocator_type = default_tracking_allocator_t<value_type>;

  explicit buffer_allocation_node(size_type size, const allocator_type& allocator)
      : buffer_{legacy_embedded_ctl::make_static_array_for_overwrite<value_type>(size, LEGACY_EMBEDDED_ALLOC_MSG(BUFFER_ALLOC_MSG), allocator)} {}

  // These constructors are required for std::uses_allocator construction
  // See https://en.cppreference.com/w/cpp/memory/uses_allocator
  buffer_allocation_node(const buffer_allocation_node& other, const allocator_type& /*allocator*/)
      : buffer_allocation_node{other} {}
  buffer_allocation_node(buffer_allocation_node&& other, const allocator_type& /*allocator*/) noexcept
      : buffer_allocation_node{std::move(other)} {}

  [[nodiscard]] size_type size() const noexcept { return buffer_.size(); }
  [[nodiscard]] const value_type* buffer_address() const noexcept { return buffer_.data(); }
  [[nodiscard]] value_type* buffer_address() noexcept { return buffer_.data(); }

  size_type used{0};  // NOLINT(misc-non-private-member-variables-in-classes)

 private:
  static_array<value_type> buffer_;
};

}  // namespace details

/**
 * Buffer for allocating memory chunks in larger blocks.
 */
template <typename TYPE>
class buffer {
 public:
  using value_type = TYPE;
  using size_type = std::size_t;
  using allocator_type = default_tracking_allocator_t<value_type>;

  explicit buffer(allocator_type allocator);
  explicit buffer(const utils::allocation_reason& reason = LEGACY_EMBEDDED_ALLOC_MSG(node_t::BUFFER_ALLOC_MSG),
                  utils::allocation_priority priority = utils::allocation_priority::LOW);

  /**
   * Request a block of memory large enough to hold 'size' elements.
   * @param size The size of the memory array to allocate
   * @return The offset of the allocated memory in the buffer and a pointer to the start of the allocated memory array
   */
  [[nodiscard]] std::pair<size_type, value_type*> request_with_offset(size_type size);

  /**
   * Request a block of memory large enough to hold 'size' elements.
   * @param size The size of the memory array to allocate
   * @return A pointer to the start of the allocated memory array
   * @note If 'size' is small, internally a larger block of memory might be allocated and shared with a follow-up
   * allocation. Nevertheless, the caller of this function is only allowed to work within the memory region which was
   * requested. That is, given the returned pointer address 'x', the caller must only work with the memory array in the
   * range [x, x + size).
   */
  [[nodiscard]] value_type* request(size_type size);

  /**
   * In contrast to the above function 'request', this call always respects the caller's requested size and will
   * allocate a memory array with exactly the requested size.
   */
  [[nodiscard]] value_type* request_exactly(size_type size);

  /**
   * Pop the last allocation. That is, free the memory of the last allocation and make it available for future requests.
   *
   * @note The parameter size must match the last requested allocation size exactly. There can only be one pop_last
   * after a request. Also note, that this function can only be reliably be used in a singled-threaded context. That is,
   * if the caller can be certain about the requested allocation sequences.
   */
  void pop_last(size_type size);

  /**
   * Return the actual size of the last allocated memory array (can differ from the requested size @see 'request').
   */
  [[nodiscard]] size_type last_chunk_size() const;

  /**
   * Return the size of the last allocated memory array which is currently in use.
   */
  [[nodiscard]] size_type last_chunk_used() const;

  /**
   * Combines all allocated memory blocks into one continuous memory block.
   * This could be an expensive operation, as memory consumption will be doubled.
   * @return concatenated memory block of allocated chunks as static_array
   */
  [[nodiscard]] static_array<value_type> reallocate_to_continuous_buffer() const;

  /**
   * Combines all allocated memory blocks into the provided continuous memory block.
   */
  void copy_to_given_continuous_buffer_and_reset(legacy_embedded_ctl::array_view<value_type> target_buffer);

  /**
   * Combines all allocated memory blocks into one continuous memory block.
   * This could be an expensive operation, as memory consumption will temporarily be doubled.
   * After filling the continuous buffer, the used buffer allocations are freed up.
   * @return concatenated memory block of allocated chunks as static_array
   */
  [[nodiscard]] static_array<value_type> reallocate_to_continuous_buffer_and_reset();

  /**
   * If a memory block was requested but is not (completely) in use by according algorithm, the usage can be decreased
   * by the caller. The buffer by itself does not maintain a semantic model or guarantees null terminated strings,
   * the plain usage counter will be decreased and memory blocks will be freed up accordingly.
   * TODO(n.weber): Look at implementation in more detail and adapt docu if necessary
   */
  void make_requested_allocations_available(size_type size);

  // TODO(n.weber): Docu
  void release_last_requested(size_type size);

  /**
   * Sets the used size of each allocation to 0 (i.e. no de-allocation will take place, only the already requested
   * memory will be marked as unused again).
   */
  void clear();

  /**
   * Returns the size of all allocations.
   */
  [[nodiscard]] size_type size() const;

 private:
  static constexpr size_type DEFAULT_ALLOCATION_SIZE{1 << 17};  // 131072
  static constexpr double GROWTH_FACTOR{1.5};

  /**
   * This internal function does not acquire locks, please make sure to acquire a lock before calling this method.
   */
  [[nodiscard]] static_array<value_type> reallocate_to_continuous_buffer_unlocked() const;

  void copy_to_given_continuous_buffer_unlocked(legacy_embedded_ctl::array_view<value_type> target_buffer) const;

  using node_t = details::buffer_allocation_node<value_type>;
  using buffer_node_allocator_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<node_t>;
  std::vector<node_t, std::scoped_allocator_adaptor<buffer_node_allocator_type>> allocations_{};
  size_type total_used_{0};
};

template <typename TYPE>
buffer<TYPE>::buffer(allocator_type allocator)
    : allocations_(std::scoped_allocator_adaptor<buffer_node_allocator_type>{std::move(allocator)}) {}

template <typename TYPE>
buffer<TYPE>::buffer(const utils::allocation_reason& reason, utils::allocation_priority priority)
    : allocations_(std::scoped_allocator_adaptor<buffer_node_allocator_type>{
          make_default_tracking_allocator<TYPE>(reason, priority)}) {}

template <typename TYPE>
inline std::pair<typename buffer<TYPE>::size_type, typename buffer<TYPE>::value_type*>
buffer<TYPE>::request_with_offset(size_type size) {
  const auto offset{total_used_};
  return {offset, request(size)};
}

template <typename TYPE>
inline typename buffer<TYPE>::value_type* buffer<TYPE>::request(const size_type size) {
  if (allocations_.empty()) {
    allocations_.emplace_back(std::max(size, DEFAULT_ALLOCATION_SIZE));
  }
  if (allocations_.back().size() < allocations_.back().used + size) {
    size_type new_char_buffer_size{std::max(allocations_.back().size(), DEFAULT_ALLOCATION_SIZE)};
    do {
      new_char_buffer_size = static_cast<size_type>(static_cast<double>(new_char_buffer_size) * GROWTH_FACTOR);
    } while (new_char_buffer_size < size);

    allocations_.emplace_back(new_char_buffer_size);
  }

  value_type* raw{allocations_.back().buffer_address() + allocations_.back().used};
  allocations_.back().used += size;
  total_used_ += size;
  return raw;
}

template <typename TYPE>
inline typename buffer<TYPE>::value_type* buffer<TYPE>::request_exactly(const size_type size) {
  allocations_.emplace_back(size);
  value_type* raw{allocations_.back().buffer_address()};
  allocations_.back().used += size;
  total_used_ += size;
  return raw;
}

template <typename TYPE>
inline void buffer<TYPE>::pop_last(const size_type size) {
  allocations_.back().used -= size;
  total_used_ -= size;
}

template <typename TYPE>
inline typename buffer<TYPE>::size_type buffer<TYPE>::last_chunk_size() const {
  return allocations_.empty() ? 0 : allocations_.back().size();
}

template <typename TYPE>
inline typename buffer<TYPE>::size_type buffer<TYPE>::last_chunk_used() const {
  return allocations_.empty() ? 0 : allocations_.back().used;
}

template <typename TYPE>
inline static_array<typename buffer<TYPE>::value_type> buffer<TYPE>::reallocate_to_continuous_buffer() const {
  return reallocate_to_continuous_buffer_unlocked();
}

template <typename TYPE>
inline void buffer<TYPE>::copy_to_given_continuous_buffer_and_reset(
    legacy_embedded_ctl::array_view<buffer<TYPE>::value_type> target_buffer) {
  copy_to_given_continuous_buffer_unlocked(target_buffer);
  total_used_ = 0;
  allocations_.clear();
  allocations_.shrink_to_fit();
}

template <typename TYPE>
inline static_array<typename buffer<TYPE>::value_type> buffer<TYPE>::reallocate_to_continuous_buffer_and_reset() {
  auto continuous_buffer{reallocate_to_continuous_buffer_unlocked()};
  total_used_ = 0;
  allocations_.clear();
  allocations_.shrink_to_fit();
  return continuous_buffer;
}

template <typename TYPE>
inline void buffer<TYPE>::make_requested_allocations_available(const size_type size) {
  if (allocations_.empty()) {
    return;
  }

  total_used_ = total_used_ > size ? total_used_ - size : 0;
  const size_type char_buffer_used{allocations_.back().used};
  if (char_buffer_used >= size) {
    allocations_.back().used = char_buffer_used - size;
    if (allocations_.back().used == 0) {
      allocations_.pop_back();
    }
  } else {
    size_type released_size{0};
    do {
      const size_type current_char_buffer_used{allocations_.back().used};
      if (current_char_buffer_used >= (size - released_size)) {
        allocations_.back().used = current_char_buffer_used - (size - released_size);
        if (allocations_.back().used == 0) {
          allocations_.pop_back();
        }
        return;
      }
      released_size += current_char_buffer_used;
      allocations_.pop_back();
    } while (released_size < size && !allocations_.empty());
  }
}

template <typename TYPE>
inline void buffer<TYPE>::release_last_requested(const size_type size) {
  if (!allocations_.empty() && allocations_.back().used >= size) {
    allocations_.back().used -= size;
    total_used_ -= size;
  }
}

template <typename TYPE>
inline static_array<typename buffer<TYPE>::value_type> buffer<TYPE>::reallocate_to_continuous_buffer_unlocked() const {
  const auto size{this->size()};

  using buffer_allocator_type = typename std::allocator_traits<buffer_node_allocator_type>::template rebind_alloc<TYPE>;

  auto concatenated_buffer{make_static_array_for_overwrite<value_type>(
      size, LEGACY_EMBEDDED_ALLOC_MSG(node_t::BUFFER_ALLOC_MSG),
      buffer_allocator_type{allocations_.get_allocator().outer_allocator()})};
  value_type* raw_buffer{concatenated_buffer.data()};
  for (const auto& allocation : allocations_) {
    raw_buffer = std::copy_n(allocation.buffer_address(), allocation.used, raw_buffer);
  }
  return concatenated_buffer;
}

template <typename TYPE>
inline void buffer<TYPE>::copy_to_given_continuous_buffer_unlocked(
    legacy_embedded_ctl::array_view<buffer<TYPE>::value_type> target_buffer) const {
  if (target_buffer.size() < total_used_) {
    throw common::internal_exception::with_context(
        {
            {"required_size", total_used_},
            {"target_buffer_size", target_buffer.size()},
        },
        "Target buffer is not large enough");
  }

  value_type* current_raw_buffer_pos{target_buffer.begin()};
  for (const auto& allocation : allocations_) {
    current_raw_buffer_pos = std::copy_n(allocation.buffer_address(), allocation.used, current_raw_buffer_pos);
  }
}

template <typename TYPE>
inline void buffer<TYPE>::clear() {
  if (!allocations_.empty()) {
    allocations_.erase(allocations_.begin(), std::prev(allocations_.end()));
    allocations_.back().used = 0;
  }
  total_used_ = 0;
}

template <typename TYPE>
inline typename buffer<TYPE>::size_type buffer<TYPE>::size() const {
  return total_used_;
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
