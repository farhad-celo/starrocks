#pragma once

#include <span>

#include "legacy_embedded_ctl/bit.h"
#include "legacy_embedded_ctl/bits/dynamic_bitset_types.h"
#include "legacy_embedded_ctl/bits/dynamic_bitset_utils.h"
#include "legacy_embedded_ctl/dynamic_bitset_fwd.h"
#include "legacy_embedded_ctl/static_array.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief The general idea for this bitset implementation is to have a drop-in replacement for boost::dynamic_bitset.
 * @description The motivation for this bitset implementation is due to two main reasons:
 * - 1.) Having an extended interface compared to a boost::dynamic_bitset (e.g., count(begin, end)) which enables us
 * to achieve a better performance in common bitset use cases
 * - 2.) Making parallel writes to the bitset thread safe by making use of std::atomic's in the internal block type
 * In order to make the bitset parallel-writable, use the
 * 'details::bitset_types::parallelism_setting::ENABLE_PARALLELISM' tag as template argument. By this, the internal
 * block type of the bitset implementation will be std::atomic<uint64_t>. When not providing the
 * 'details::bitset_types::parallelism_setting::ENABLE_PARALLELISM' tag or by providing the
 * 'details::bitset_types::parallelism_setting::DISABLE_PARALLELISM' tag, the bitset will use plain uint64_t as its
 * internal block type and is thus not suitable for parallel writes.
 * @tparam PARALLELISM_SETTING controls whether the bitset internals are such that it can work with parallel writes
 * @disclaimer Currently, several functionalities provided by boost::dynamic_bitset (such as shift operators or
 * resizing) are not provided in our custom implementation. If there is an actual need for these, consider creating a
 * PR/ticket for an interface extension.
 */
template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
class bitset_crtp_base {
 public:
  using value_type = details::bitset_types::value_type;
  using block_type = details::bitset_types::block_type<PARALLELISM_SETTING, value_type>;
  using bit_index_type = details::bitset_types::bit_index_type;
  using block_index_type = details::bitset_types::block_index_type;
  using size_type = details::bitset_types::size_type;

  /**
   * @brief used to check whether the bitset instantiation has parallelism 'enabled'
   */
  static constexpr bool PARALLELISM_ENABLED{details::bitset_types::parallelism_enabled_v<PARALLELISM_SETTING>};
  static_assert(PARALLELISM_ENABLED ? std::atomic<value_type>::is_always_lock_free : true,
                "No lock free atomic implementation.");
  /**
   * @brief the number of bits per block (currently this should usually always be 64)
   */
  static constexpr size_type BLOCK_SIZE = details::bitset_types::BLOCK_SIZE();
  static_assert(BLOCK_SIZE == sizeof(block_type) * CHAR_BIT, "atomic adds size");

  /**
   * @brief indicates an invalid bitset index (usually indicates that some search was not successful)
   */
  static constexpr bit_index_type npos = std::numeric_limits<bit_index_type>::max();

  class bit_reference;

  [[nodiscard]] bool operator==(const bitset_crtp_base& rhs) const = default;  // compiler also provides operator!=

  /**
   * @brief sets the bit at position 'index'
   * @param index the position to set the bit at
   * @return the bitset (for chaining)
   */
  DERIVED& set(bit_index_type index) noexcept;

  /**
   * @brief This function offers an atomic set on the non parallelized bitset.
   * Normally, functions that access a bitset in parallel should make use of a safe_aligned_block_range, but for
   * function where the row indices are already partitioned in another, unsafe way this might not be feasible.
   * A parallel bitset can also not be used since it has to be converted to a normal bitset with a copy.
   *
   * @param index the position to set the bit at
   * @return the bitset (for chaining)
   */
  DERIVED& experimental_atomic_set(bit_index_type index) noexcept requires(!PARALLELISM_SETTING.ENABLE_PARALLELISM);

  /**
   * @brief This function implements a variant of experimental_atomic_set that first tests the respective bit and
   * sets it if the test returned false. Semantically, this is equivalent to an atomic set, but may improve performance
   * when many threads attempt to set the same bit concurrently. However, note that wihtout write contention, a vanilla
   * atomic set will always be faster.
   */
  void experimental_atomic_set_if_unset(bit_index_type index) noexcept
      requires(!PARALLELISM_SETTING.ENABLE_PARALLELISM);

  /**
   * @brief sets all bits in the bitset
   * @return the bitset (for chaining)
   */
  DERIVED& set() noexcept;
  /**
   * @brief sets the bit at position 'index' to value 'val'
   * @param index the position to set the bit at
   * @param val the value to set the bit to
   * @return the bitset (for chaining)
   */
  DERIVED& set(bit_index_type index, bool val) noexcept;
  /**
   * @brief Sets the bits between [idx_from, idx_to).
   *
   * If idx_to <= idx_from, nothing happens. If idx_to is equal or bigger to the size of the bitset
   * all bits starting from idx_from are set.
   * @param idx_from The index of the first bit to be set
   * @param idx_to The index of the first bit after the last bit to be set
   * @return the bitset (for chaining)
   */
  DERIVED& set_range(bit_index_type idx_from, bit_index_type idx_to) noexcept;
  /**
   * @brief unsets the bit at position 'index'
   * @param index the position to unset the bit at
   * @return the bitset (for chaining)
   */
  DERIVED& reset(bit_index_type index) noexcept;
  /**
   * @brief unsets all bits in the bitset
   * @return the bitset (for chaining)
   */
  DERIVED& reset() noexcept;
  /**
   * @brief flips the bit at position 'index'
   * @param index the position to flip the bit at
   * @return the bitset (for chaining)
   */
  DERIVED& flip(bit_index_type index) noexcept;
  /**
   * @brief flips all bits in the bitset
   * @return the bitset (for chaining)
   */
  DERIVED& flip() noexcept;
  /**
   * @brief sets bit if it is unset to value
   * @param index the position of the bit
   * @param val the value it is set if it is unset
   * @return the bitset (for chaining)
   */
  DERIVED& set_if_unset(bit_index_type index, bool val = true) noexcept;
  /**
   * @brief same as the subscript operator but with bounds checking (throws if index out of bounds)
   */
  [[nodiscard]] bool at(bit_index_type index) const;
  /**
   * @brief same as the subscript operator but with bounds checking (throws if index out of bounds)
   */
  [[nodiscard]] bit_reference at(bit_index_type index);
  /**
   * @brief same as the subscript operator
   */
  [[nodiscard]] bool test(bit_index_type index) const noexcept;
  /**
   * @brief returns whether all bits in the bitset are set
   * @return true if all bits in the bitset are set, false otherwise
   */
  [[nodiscard]] bool all() const noexcept;
  /**
   * @brief returns whether any bit in the bitset is set
   * @return true if any bit in the bitset is set, false otherwise
   */
  [[nodiscard]] bool any() const noexcept;
  /**
   * @brief returns whether no bits in the bitset are set
   * @return true if no bits in the bitset are set, false otherwise
   */
  [[nodiscard]] bool none() const noexcept;
  /**
   * @brief tests whether the bit at position 'index' equals 'val' and sets it to 'val' if not equal
   * @param index the bit position to check
   * @param val the value to compare the bit against and set accordingly
   * @return true if the bit at position 'index' was equal to 'val' (before potential setting it)
   */
  [[nodiscard]] bool test_set(bit_index_type index, bool val = true) noexcept;

  /* bitset size */
  [[nodiscard]] size_type size() const noexcept { return static_cast<const DERIVED*>(this)->size(); }
  [[nodiscard]] block_index_type num_blocks() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

  /* utilities */
  /**
   * @brief counts the set bits in the range [idx_from, idx_to)
   * @param idx_from bit index (inclusive) to start counting from
   * @param idx_to bit index (exclusive) to stop counting at
   * @return the number of set bits in the range [idx_from, idx_to)
   */
  [[nodiscard]] size_type count(bit_index_type idx_from = 0, bit_index_type idx_to = npos) const noexcept;
  /**
   * @brief find the position of the first set bit in the range [idx_from, idx_to)
   * @param idx_from bit index (inclusive) to start searching from
   * @param idx_to bit index (exclusive) to stop searching at
   * @return the position of the first set bit in the range [idx_from, idx_to) or 'npos' if no bit is set
   */
  [[nodiscard]] bit_index_type find_first(bit_index_type idx_from = 0, bit_index_type idx_to = npos) const noexcept;
  /**
   * @brief find the position of the first set bit in the range (idx_from, idx_to)
   * @param index_from the position from where to start searching for a set bit
   * @param idx_to the upper bound index when to stop searching (exclusive; default: all blocks)
   * @return the position of the first set bit after position 'idx_from' or 'npos' if no bit is set
   */
  [[nodiscard]] bit_index_type find_next(bit_index_type idx_from, bit_index_type idx_to = npos) const noexcept;

  [[nodiscard]] const block_type* data() const noexcept requires(!PARALLELISM_SETTING.ENABLE_PARALLELISM);
  [[nodiscard]] block_type* data() noexcept requires(!PARALLELISM_SETTING.ENABLE_PARALLELISM);

  /**
   * @brief used to apply each index of a set bit in the range [idx_from, idx_to) to a given callable
   * @tparam CALLABLE the type of the given callable
   * @param callable the given callable to which apply each index of a set bit
   * @param idx_from the start of the range from where to start searching for set bits (inclusive)
   * @param idx_to the end of the range at which the search for set bits ends (exclusive)
   * @throws any exception thrown by the given callable
   */
  template <typename CALLABLE>
  void apply_in_range(CALLABLE&& callable, bit_index_type idx_from = 0, bit_index_type idx_to = npos) const
      noexcept(std::is_nothrow_invocable_v<CALLABLE, bit_index_type>);

  /**
   * @brief used to apply each index of an unset bit in the range [idx_from, idx_to) to a given callable
   * @tparam CALLABLE the type of the given callable
   * @param callable the given callable to which apply each index of an unset bit
   * @param idx_from the start of the range from where to start searching for unset bits (inclusive)
   * @param idx_to the end of the range at which the search for unset bits ends (exclusive)
   * @throws any exception thrown by the given callable
   */
  template <typename CALLABLE>
  void apply_on_unset_in_range(CALLABLE&& callable, bit_index_type idx_from = 0, bit_index_type idx_to = npos) const
      noexcept(std::is_nothrow_invocable_v<CALLABLE, bit_index_type>);

 protected:
  /* internal helper functions */
  [[nodiscard]] bit_index_type find_from(block_index_type block_index,
                                         block_index_type upper_bound_block_index) const noexcept;
  [[nodiscard]] static constexpr value_type get_block_value(const block_type& block) noexcept;
  [[nodiscard]] value_type get_block_value_at_index(block_index_type block_index) const noexcept;
  void zero_unused_bits() noexcept;
  template <bool DO_FLIP, typename CALLABLE>
  void apply_in_range_impl(CALLABLE&& callable, bit_index_type idx_from, bit_index_type idx_to) const
      noexcept(std::is_nothrow_invocable_v<CALLABLE, bit_index_type>);
  /**
   * @brief subscript operator; returns whether the bit at position 'index' is set
   * @param index the bit position to check
   * @return true if the bit at position 'index' is set, false otherwise
   */
  [[nodiscard]] bool operator[](bit_index_type index) const noexcept;
  /**
   * @brief (non-const) subscript operator; returns a reference proxy to the requested bit
   * @param index the bit position
   * @return proxy bit reference
   * @note the returned bit_reference object should never be captured by a reference as it is returned by value
   */
  [[nodiscard]] bit_reference operator[](bit_index_type index) noexcept;

 public:
  /**
   * @brief internal class representing a reference to an individual bit
   * to allow for direct bit manipulation via operator[]
   */
  class bit_reference final {
   public:
    /**
     * @brief implicit cast to bool operator
     * @return whether the bit is set (true) or not (false)
     */
    operator bool() const noexcept;  // NOLINT(google-explicit-constructor)
    /**
     * @brief bit flip operator
     * @return the negation of the bit (false if set, true otherwise)
     */
    [[nodiscard]] bool operator~() const noexcept;
    /**
     * @brief bit flip operator (modifying this bit reference)
     * @return the bit reference for chaining
     */
    bit_reference& flip() noexcept;
    /**
     * @brief bit assignment operator
     * @param b boolean indicating whether the bit shall be set or unset
     * @return the bit reference for chaining
     */
    bit_reference& operator=(bool b) noexcept;
    bit_reference& operator=(const bit_reference& b) noexcept;
    /**
     * @brief bit OR-assignment operator
     * @param b boolean indicating whether the bit shall be set
     * @return the bit reference for chaining
     */
    bit_reference& operator|=(bool b) noexcept;
    /**
     * @brief bit AND-assignment operator
     * @param b boolean indicating whether the bit shall be unset
     * @return the bit reference for chaining
     */
    bit_reference& operator&=(bool b) noexcept;
    /**
     * @brief bit XOR-assignment operator
     * @param b boolean indicating whether the bit shall be set or unset
     * @return the bit reference for chaining
     */
    bit_reference& operator^=(bool b) noexcept;

   private:
    /* internal helper functions */
    [[nodiscard]] static constexpr value_type get_block_value(const block_type& block) noexcept;

    friend class bitset_crtp_base<PARALLELISM_SETTING, DERIVED>;  // for constructor access
    bit_reference(block_type& block, block_index_type index) noexcept;
    block_type& block_;
    value_type mask_;
  };
};

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline DERIVED& bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::set(const bit_index_type index) noexcept {
  legacy_embedded_debug_assert(index < size());
  const auto [block_index, bit_index] = details::block_and_bit_index::get(index);
  auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
  mutable_span[block_index] |= details::BIT_MASK(bit_index);
  return static_cast<DERIVED&>(*this);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline DERIVED& bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::experimental_atomic_set(
    const bit_index_type index) noexcept requires(!PARALLELISM_SETTING.ENABLE_PARALLELISM) {
  legacy_embedded_debug_assert(!PARALLELISM_ENABLED);
  legacy_embedded_debug_assert(index < size());
  const auto [block_index, bit_index] = details::block_and_bit_index::get(index);
  // TODO(j.boettcher) Workaround for clang-14 issue that does not handle concepts properly.
  // With clang 15 this can be removed
  if constexpr (!PARALLELISM_ENABLED) {
    auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
    std::atomic_ref<value_type> block{mutable_span[block_index]};
    block.fetch_or(details::BIT_MASK(bit_index));
  }
  return static_cast<DERIVED&>(*this);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline void bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::experimental_atomic_set_if_unset(
    const bit_index_type index) noexcept requires(!PARALLELISM_SETTING.ENABLE_PARALLELISM) {
  legacy_embedded_debug_assert(!PARALLELISM_ENABLED);
  legacy_embedded_debug_assert(index < size());

  const auto [block_index, bit_index] = details::block_and_bit_index::get(index);

  // TODO(j.boettcher) Workaround for clang-14 issue that does not handle concepts properly.
  // With clang 15 this can be removed
  if constexpr (!PARALLELISM_ENABLED) {
    auto mask{details::BIT_MASK(bit_index)};
    auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
    std::atomic_ref<value_type> block{mutable_span[block_index]};

    auto value{block.load()};

    if ((value & mask) == 0) {
      block.fetch_or(mask);
    }
  }
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline DERIVED& bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::set(const bit_index_type index,
                                                                    const bool val) noexcept {
  return val ? set(index) : reset(index);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline DERIVED& bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::reset(const bit_index_type index) noexcept {
  legacy_embedded_debug_assert(index < size());
  const auto [block_index, bit_index] = details::block_and_bit_index::get(index);
  auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
  mutable_span[block_index] &= ~details::BIT_MASK(bit_index);
  return static_cast<DERIVED&>(*this);
}
template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline DERIVED& bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::flip(const bit_index_type index) noexcept {
  legacy_embedded_debug_assert(index < size());
  const auto [block_index, bit_index] = details::block_and_bit_index::get(index);
  auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
  mutable_span[block_index] ^= details::BIT_MASK(bit_index);
  return static_cast<DERIVED&>(*this);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline DERIVED& bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::set_if_unset(const bit_index_type index,
                                                                             bool val) noexcept {
  legacy_embedded_debug_assert(index < size());
  if (!val) {
    return static_cast<DERIVED&>(*this);
  }
  return set(index);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline bool bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::operator[](const bit_index_type index) const noexcept {
  legacy_embedded_debug_assert(index < size());
  const auto [block_index, bit_index] = details::block_and_bit_index::get(index);
  auto mutable_span{static_cast<const DERIVED*>(this)->to_block_span()};
  return (mutable_span[block_index] & details::BIT_MASK(bit_index)) != 0;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::operator[](const bit_index_type index) noexcept {
  legacy_embedded_debug_assert(index < size());
  const auto [block_index, bit_index] = details::block_and_bit_index::get(index);
  auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
  return bit_reference{mutable_span[block_index], bit_index};
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline bool bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::at(const bit_index_type index) const {
  if (index >= size()) {
    throw legacy_embedded_ctl::out_of_range{"Index [{}] is out of bounds for dynamic bitset of size [{}].", index, size()};
  }
  return operator[](index);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::at(const bit_index_type index) {
  if (index >= size()) {
    throw legacy_embedded_ctl::out_of_range{"Index [{}] is out of bounds for dynamic bitset of size [{}].", index, size()};
  }
  return operator[](index);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline bool bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::test(const bit_index_type index) const noexcept {
  return operator[](index);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline bool bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::test_set(const bit_index_type index,
                                                                     const bool val) noexcept {
  legacy_embedded_debug_assert(index < size());
  if constexpr (PARALLELISM_ENABLED) {
    const auto [block_index, bit_index] = details::block_and_bit_index::get(index);
    const value_type mask = details::BIT_MASK(bit_index);
    // 1) fetch block
    // 2) -if val true: apply mask with OR
    //    -if val false: apply negated mask with AND
    // 3) test and return whether bit was set in fetched block
    auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
    return ((val ? mutable_span[block_index].fetch_or(mask) : mutable_span[block_index].fetch_and(~mask)) & mask) != 0;
  } else {
    const bool is_set = test(index);
    if (is_set != val) {
      set(index, val);
    }
    return is_set;
  }
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline const typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::block_type*
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::data() const noexcept
    requires(!PARALLELISM_SETTING.ENABLE_PARALLELISM) {
  legacy_embedded_debug_assert(!PARALLELISM_ENABLED,
               "Access to the bitset's internal blocks is only allowed for the non-parallelized bitset.");
  legacy_embedded_debug_assert(std::is_same_v<block_type, value_type>,
               "In the non-parallelized case the block and value type must be equal.");
  auto view{static_cast<const DERIVED*>(this)->to_block_span()};
  return view.data();
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::block_type*
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::data() noexcept requires(!PARALLELISM_SETTING.ENABLE_PARALLELISM) {
  legacy_embedded_debug_assert(!PARALLELISM_ENABLED,
               "Access to the bitset's internal blocks is only allowed for the non-parallelized bitset.");
  legacy_embedded_debug_assert(std::is_same_v<block_type, value_type>,
               "In the non-parallelized case the block and value type must be equal.");
  auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
  return mutable_span.data();
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
template <typename CALLABLE>
inline void bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::apply_in_range(CALLABLE&& callable, bit_index_type idx_from,
                                                                           bit_index_type idx_to) const
    noexcept(std::is_nothrow_invocable_v<CALLABLE, bit_index_type>) {
  constexpr bool DO_FLIP{false};
  apply_in_range_impl<DO_FLIP>(callable, idx_from, idx_to);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
template <typename CALLABLE>
inline void bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::apply_on_unset_in_range(CALLABLE&& callable,
                                                                                    bit_index_type idx_from,
                                                                                    bit_index_type idx_to) const
    noexcept(std::is_nothrow_invocable_v<CALLABLE, bit_index_type>) {
  constexpr bool DO_FLIP{true};
  apply_in_range_impl<DO_FLIP>(callable, idx_from, idx_to);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
template <bool DO_FLIP, typename CALLABLE>
inline void bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::apply_in_range_impl(CALLABLE&& callable,
                                                                                bit_index_type idx_from,
                                                                                bit_index_type idx_to) const
    noexcept(std::is_nothrow_invocable_v<CALLABLE, bit_index_type>) {
  static_assert(std::is_invocable_v<CALLABLE, bit_index_type>, "The given callable is not invocable with a bit index.");

  if (idx_to > size()) {
    idx_to = size();
  }
  if (idx_from >= idx_to) {
    return;
  }
  legacy_embedded_debug_assert(idx_from < idx_to);

  auto apply_to_block = [](CALLABLE& callable, bit_index_type index, value_type block) {
    if constexpr (DO_FLIP) {
      block = ~block;
    }
    // Fast path for the case that all bits are set
    if (block == details::MASK_ALL_SET) {
      for (size_t i{0}; i < BLOCK_SIZE; ++i) {
        callable(index + i);
      }
      return;
    }
    while (block != 0) {
      callable(index + std::countr_zero(block));
      block &= block - 1;  // Unset the bottom bit.
    }
  };

  auto [current_block_idx, current_bit_idx] = details::block_and_bit_index::get(idx_from);
  const auto [to_block_idx, to_bit_idx] = details::block_and_bit_index::get(idx_to - 1);

  value_type current_block{get_block_value_at_index(current_block_idx)};
  if constexpr (DO_FLIP) {
    if (current_bit_idx > 0) {
      current_block |= details::bits_set_to(current_bit_idx - 1);
    }
  } else {
    current_block &= details::bits_set_from(current_bit_idx);
  }

  while (current_block_idx < to_block_idx) {
    apply_to_block(callable, details::block_and_bit_index::to_bit_index(current_block_idx, 0), current_block);
    ++current_block_idx;
    current_block = get_block_value_at_index(current_block_idx);
  }

  legacy_embedded_debug_assert(current_block_idx == to_block_idx);
  if constexpr (DO_FLIP) {
    if (to_bit_idx < BLOCK_SIZE - 1) {
      current_block |= details::bits_set_from(to_bit_idx + 1);
    }
  } else {
    current_block &= details::bits_set_to(to_bit_idx);
  }
  apply_to_block(callable, details::block_and_bit_index::to_bit_index(current_block_idx, 0), current_block);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference::bit_reference(
    block_type& block, const block_index_type index) noexcept
    : block_(block), mask_(details::BIT_MASK(index)) {}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference::operator bool() const noexcept {
  return (block_ & mask_) != 0;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline bool bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference::operator~() const noexcept {
  return (block_ & mask_) == 0;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference&
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference::flip() noexcept {
  // flip bit
  block_ ^= mask_;
  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference&
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference::operator=(const bool b) noexcept {
  if (b) {
    // set bit
    block_ |= mask_;
  } else {
    // reset bit
    block_ &= ~mask_;
  }
  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference&
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference::operator=(const bit_reference& b) noexcept {
  return operator=(b.operator bool());  // NOLINT(misc-unconventional-assign-operator)
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference&
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference::operator|=(const bool b) noexcept {
  if (b) {
    // set bit
    block_ |= mask_;
  }
  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference&
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference::operator&=(const bool b) noexcept {
  if (!b) {
    // reset bit
    block_ &= ~mask_;
  }
  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference&
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_reference::operator^=(const bool b) noexcept {
  if (b) {
    flip();
  }
  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
inline constexpr typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::value_type
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::get_block_value(const block_type& block) noexcept {
  if constexpr (PARALLELISM_ENABLED) {
    return block.load();
  } else {
    return block;
  }
}

template <typename DERIVED>
using bitset_crtp_base_t = legacy_embedded_ctl::bitset_crtp_base<legacy_embedded_ctl::details::bitset_types::parallelism_setting{false}, DERIVED>;

template <typename DERIVED>
using bitset_crtp_base_parallel_t =
    legacy_embedded_ctl::bitset_crtp_base<legacy_embedded_ctl::details::bitset_types::parallelism_setting{true}, DERIVED>;

}  // namespace celonis::accelerator::legacy_embedded_ctl