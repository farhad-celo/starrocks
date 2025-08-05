#pragma once

#include <climits>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/bits/dynamic_bitset_types.h"

namespace celonis::accelerator::legacy_embedded_ctl::details {

using bs_value_type = bitset_types::value_type;
using bs_bit_index_in_block_type = bitset_types::bit_index_in_block_type;

static constexpr bs_value_type MASK_ALL_UNSET = bs_value_type{0};
static constexpr bs_value_type MASK_ALL_SET = ~MASK_ALL_UNSET;

/**
 * @brief creates a bit mask where only one bit is set at the given bit index (for the commonly used pattern '1 << idx')
 * @param bit_index the bit index within the bit mask where to set the bit
 * @return the mask corresponding to '1 << bit_index'
 */
inline static constexpr bs_value_type BIT_MASK(const bs_bit_index_in_block_type bit_index) noexcept {
  legacy_embedded_debug_assert(bit_index < bitset_types::BLOCK_SIZE());
  return bs_value_type{1} << bit_index;
}

using bs_bit_index_type = bitset_types::bit_index_type;
using bs_size_type = bitset_types::size_type;
using bs_block_index_type = bitset_types::block_index_type;
static constexpr bs_size_type BS_BLOCK_SIZE = bitset_types::BLOCK_SIZE();

/**
 * @brief helper class enabling easy access to the block index and bit index within that block for a given bit index
 */
class block_and_bit_index final {
 public:
  bs_block_index_type block_index_;
  bs_bit_index_in_block_type bit_index_;

  /**
   * @return the block index of this instance
   */
  [[nodiscard]] constexpr bs_block_index_type block_index() const noexcept { return block_index_; }
  /**
   * @return the bit index (within the respective block) of this instance
   */
  [[nodiscard]] constexpr bs_bit_index_in_block_type bit_index() const noexcept { return bit_index_; }

  /**
   * @brief factory function to 'get' the block and bit index (within that block) for the given (total) bit index
   * @param index the (total) bit index within the entire bitset
   * @return instance of this class with correctly set block index and bit index (within that block)
   */
  [[nodiscard]] static constexpr block_and_bit_index get(const bs_bit_index_type index) noexcept {
    return block_and_bit_index{static_cast<bs_block_index_type>(index / BS_BLOCK_SIZE),
                               static_cast<bs_bit_index_in_block_type>(index % BS_BLOCK_SIZE)};
  }

  /**
   * @brief reverse of the above factory function: returns a bit index for a given block index and its local bit index
   * @param block_index the index of the block
   * @param bit_index the block local bit index
   * @return index the (total) bit index within the entire bitset
   */
  [[nodiscard]] static constexpr bs_bit_index_type to_bit_index(const bs_block_index_type block_index,
                                                                const bs_bit_index_in_block_type bit_index) noexcept {
    return block_index * BS_BLOCK_SIZE + bit_index;
  }
};

/**
 * @brief utility function to create bit mask where all bits in the range...
 * - [0, from) are unset (0) and
 * - [from, BLOCK_SIZE] are set (1)
 * @param from_bit_idx the bit index (inclusive) from where on all bits shall be set
 * @return the bit mask with bits in range [from_bit_idx, BLOCK_SIZE] are set
 */
inline static constexpr bs_value_type bits_set_from(const bs_bit_index_in_block_type from_bit_idx) noexcept {
  legacy_embedded_debug_assert(from_bit_idx < BS_BLOCK_SIZE);
  return MASK_ALL_SET << from_bit_idx;
}

/**
 * @brief utility function to create bit mask where all bits in the range...
 * - [0, to] are set (1) and
 * - (to, BLOCK_SIZE] are unset (0)
 * @param to_bit_idx the bit index (inclusive) up to which all bits shall be set
 * @return the bit mask with bits in range [0, to_bit_idx] are set
 */
inline static constexpr bs_value_type bits_set_to(const bs_bit_index_in_block_type to_bit_idx) noexcept {
  legacy_embedded_debug_assert(to_bit_idx < BS_BLOCK_SIZE);
  return MASK_ALL_SET >> (BS_BLOCK_SIZE - to_bit_idx - 1);
}

/**
 * @brief utility function to create bit mask where all bits in the range...
 * - [0, from) are unset (0),
 * - [from, to] are set (1) and
 * - (to, BLOCK_SIZE] are unset (0)
 * @param from_bit_idx the bit index (inclusive) from where on bits shall be set
 * @param to_bit_idx the bit index (inclusive) up to which bits shall be set
 * @return the bit mask with bits in range [from_bit_idx, to_bit_idx] are set
 */
inline static constexpr bs_value_type bits_set_in_range(const bs_bit_index_in_block_type from_bit_idx,
                                                        const bs_bit_index_in_block_type to_bit_idx) noexcept {
  legacy_embedded_debug_assert(from_bit_idx <= to_bit_idx);
  return bits_set_from(from_bit_idx) & bits_set_to(to_bit_idx);
}

/**
 * @brief utility function to calculate the block number based on the size of bits
 */
inline static constexpr bs_block_index_type calc_number_of_bitset_blocks(size_t size) noexcept {
  if (size == 0) {
    return 0;
  }
  const bs_block_index_type block_index{details::block_and_bit_index::get(size - 1).block_index()};
  return block_index + 1;
}

}  // namespace celonis::accelerator::legacy_embedded_ctl::details
