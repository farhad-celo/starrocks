#include "legacy_embedded_ctl/bitset_base.h"

#include <algorithm>
#include <bit>
#include <type_traits>

#include <oneapi/tbb/parallel_for.h>

#include "legacy_embedded_ctl/bitset_view.h"
#include "legacy_embedded_ctl/conversion.h"
#include "legacy_embedded_ctl/dynamic_bitset.h"
#include "legacy_embedded_ctl/static_array.h"
#include "legacy_embedded_ctl/utils/allocation_messages.h"
#include "legacy_embedded_ctl/utils/allocation_priority.h"
#include "legacy_embedded_ctl/utils/allocation_reason.h"
#include "modules/common/aligned_blocked_range.h"

namespace celonis::accelerator::legacy_embedded_ctl {

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
DERIVED& bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::set() noexcept {
  // maybe parallelize based on heuristic for very large bitsets?
  auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
  std::fill(mutable_span.begin(), mutable_span.end(), details::MASK_ALL_SET);
  zero_unused_bits();
  return static_cast<DERIVED&>(*this);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
DERIVED& bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::set_range(const bit_index_type idx_from,
                                                                   bit_index_type idx_to) noexcept {
  if (idx_to > size()) {
    idx_to = size();
  }
  if (idx_to <= idx_from) {
    return static_cast<DERIVED&>(*this);
  }
  legacy_embedded_debug_assert(idx_to >= 1);

  const auto [from_block_idx, from_bit_idx] = details::block_and_bit_index::get(idx_from);
  const auto [to_block_idx, to_bit_idx] = details::block_and_bit_index::get(idx_to - 1);
  auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};

  if (from_block_idx == to_block_idx) {
    mutable_span[from_block_idx] |= details::bits_set_in_range(from_bit_idx, to_bit_idx);
    return static_cast<DERIVED&>(*this);
  }

  mutable_span[from_block_idx] |= details::bits_set_from(from_bit_idx);
  mutable_span[to_block_idx] |= details::bits_set_to(to_bit_idx);
  for (auto i = from_block_idx + 1; i < to_block_idx; ++i) {
    mutable_span[i] |= details::MASK_ALL_SET;
  }

  return static_cast<DERIVED&>(*this);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
DERIVED& bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::reset() noexcept {
  // maybe parallelize based on heuristic for very large bitsets?
  auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
  std::fill(mutable_span.begin(), mutable_span.end(), details::MASK_ALL_UNSET);
  return static_cast<DERIVED&>(*this);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
DERIVED& bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::flip() noexcept {
  auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};
  for (auto& block : mutable_span) {
    if constexpr (PARALLELISM_ENABLED) {
      value_type oldValue = block.load();
      while (!block.compare_exchange_weak(oldValue, ~oldValue)) {
      }
    } else {
      block = ~block;
    }
  }
  zero_unused_bits();
  return static_cast<DERIVED&>(*this);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
bool bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::all() const noexcept {
  if (empty()) {
    return true;
  }

  const auto [block_index, bit_index] = details::block_and_bit_index::get(size());
  auto view{static_cast<const DERIVED*>(this)->to_block_span()};

  if (bit_index == 0) {
    // all blocks of the bitset are entirely used: can just check if all blocks are completely set
    const block_index_type end_index = num_blocks();
    for (block_index_type idx = 0; idx < end_index; ++idx) {
      if (view[idx] != details::MASK_ALL_SET) {
        return false;
      }
    }
  } else {
    // the last block of the bitset is only partially used: check if all blocks until the last one are completely set
    const block_index_type end_index = num_blocks() - 1;
    for (block_index_type idx = 0; idx < end_index; ++idx) {
      if (view[idx] != details::MASK_ALL_SET) {
        return false;
      }
    }
    // check the remaining bits in the partially used last block
    const value_type mask = details::BIT_MASK(bit_index) - 1;
    const value_type highest_block = get_block_value_at_index(block_index);
    if (highest_block != mask) {
      return false;
    }
  }
  return true;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
bool bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::any() const noexcept {
  auto view{static_cast<const DERIVED*>(this)->to_block_span()};
  return std::ranges::any_of(view.begin(), view.end(),
                             [](const auto& block) { return block != details::MASK_ALL_UNSET; });
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
bool bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::none() const noexcept {
  return !any();
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::block_index_type
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::num_blocks() const noexcept {
  auto view{static_cast<const DERIVED*>(this)->to_block_span()};
  return static_cast<block_index_type>(view.size());
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
bool bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::empty() const noexcept {
  return size() == 0;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::size_type
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::count(const bit_index_type idx_from,
                                                      bit_index_type idx_to) const noexcept {
  if (idx_to > size()) {
    idx_to = size();
  }
  if (idx_to <= idx_from) {
    return 0;
  }
  const auto [from_block_idx, from_bit_idx] = details::block_and_bit_index::get(idx_from);
  const auto [to_block_idx, to_bit_idx] = details::block_and_bit_index::get(idx_to - 1);
  if (from_block_idx == to_block_idx) {  // both index are in the same block
    // clean unwanted bits in block..
    // create mask with all bits in the range [from_bit_idx, to_bit_idx] set and use it to unset other bits in the block
    return static_cast<size_type>(
        std::popcount(get_block_value_at_index(from_block_idx) & details::bits_set_in_range(from_bit_idx, to_bit_idx)));
  }
  // count bits in cleaned from block
  size_type number_of_set_bits{static_cast<size_type>(
      std::popcount(get_block_value_at_index(from_block_idx) & details::bits_set_from(from_bit_idx)))};
  // count bits in cleaned to block
  number_of_set_bits +=
      static_cast<size_type>(std::popcount(get_block_value_at_index(to_block_idx) & details::bits_set_to(to_bit_idx)));
  // count bits in entire blocks in between
  for (block_index_type curr_block_idx = from_block_idx + 1; curr_block_idx < to_block_idx; ++curr_block_idx) {
    number_of_set_bits += static_cast<size_type>(std::popcount(get_block_value_at_index(curr_block_idx)));
  }
  return number_of_set_bits;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_index_type
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::find_first(const bit_index_type idx_from,
                                                           bit_index_type idx_to) const noexcept {
  if (idx_to > size()) {
    idx_to = size();
  }
  if (idx_from >= idx_to) {
    return npos;
  }
  legacy_embedded_debug_assert(idx_from < idx_to);

  const auto [from_block_idx, from_bit_idx] = details::block_and_bit_index::get(idx_from);

  // shift bits upto one immediately after current
  value_type current_block = get_block_value_at_index(from_block_idx) >> from_bit_idx;

  if (current_block != 0) {
    const auto bit_index{idx_from + static_cast<details::bs_bit_index_in_block_type>(find_first_set(current_block))};
    return bit_index < idx_to ? bit_index : npos;
  }
  const auto [to_block_idx, to_bit_idx] = details::block_and_bit_index::get(idx_to - 1);
  if (from_block_idx == to_block_idx) {
    return npos;
  }
  if (const bit_index_type set_bit_idx = find_from(from_block_idx + 1, to_block_idx); set_bit_idx != npos) {
    return set_bit_idx;
  }
  /**
   * Clear bits after to_bit_idx within the last valid block.
   * If idx_to == size(), we would not need to mask here because all bits af size() are zero anyways.
   * However, the additional branch would probably be more expensive than just always masking
   */
  current_block = get_block_value_at_index(to_block_idx) & details::bits_set_to(to_bit_idx);
  return current_block != 0
             ? details::block_and_bit_index::to_bit_index(
                   to_block_idx, static_cast<details::bs_bit_index_in_block_type>(find_first_set(current_block)))
             : npos;
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_index_type
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::find_next(const bit_index_type idx_from,
                                                          const bit_index_type idx_to) const noexcept {
  return find_first(idx_from + 1, idx_to);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::bit_index_type
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::find_from(
    block_index_type block_index, const block_index_type upper_bound_block_index) const noexcept {
  legacy_embedded_debug_assert(upper_bound_block_index <= num_blocks());
  auto view{static_cast<const DERIVED*>(this)->to_block_span()};

  // skip zero blocks
  while (block_index < upper_bound_block_index && view[block_index] == 0) {
    ++block_index;
  }

  if (block_index >= upper_bound_block_index) {
    return npos;  // not found
  }

  const auto block_local_bit_idx{
      static_cast<details::bs_bit_index_in_block_type>(find_first_set(get_block_value_at_index(block_index)))};
  return details::block_and_bit_index::to_bit_index(block_index, block_local_bit_idx);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
typename bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::value_type
bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::get_block_value_at_index(
    const block_index_type block_index) const noexcept {
  legacy_embedded_debug_assert(block_index < num_blocks());
  auto view{static_cast<const DERIVED*>(this)->to_block_span()};
  return get_block_value(view[block_index]);
}

template <parallelism_settings_t PARALLELISM_SETTING, typename DERIVED>
void bitset_crtp_base<PARALLELISM_SETTING, DERIVED>::zero_unused_bits() noexcept {
  legacy_embedded_debug_assert(num_blocks() == details::calc_number_of_bitset_blocks(size()));

  const auto [block_index, bit_index] = details::block_and_bit_index::get(size());
  auto mutable_span{static_cast<DERIVED*>(this)->to_mutable_block_span()};

  // if != 0 this is the number of bits used in the last block
  if (bit_index != 0) {
    mutable_span[block_index] &= details::BIT_MASK(bit_index) - 1;
  }
}

// bitset_view_t does not implement to_mutable_block_span and thus cannot be defined directly
// like the other child types: dynamic_bitset_t, dynamic_bitset_parallel_t, bitset_mutable_view_t
template typename bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                                   legacy_embedded_ctl::bitset_view_t>::value_type
    bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                     legacy_embedded_ctl::bitset_view_t>::get_block_value_at_index(const block_index_type block_index)
        const noexcept;

template bool bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                               legacy_embedded_ctl::bitset_view_t>::all() const noexcept;

template bool bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                               legacy_embedded_ctl::bitset_view_t>::any() const noexcept;

template bool bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                               legacy_embedded_ctl::bitset_view_t>::none() const noexcept;

template typename bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                                   legacy_embedded_ctl::bitset_view_t>::block_index_type
    bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                     legacy_embedded_ctl::bitset_view_t>::num_blocks() const noexcept;

template bool bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                               legacy_embedded_ctl::bitset_view_t>::empty() const noexcept;

template typename bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                                   legacy_embedded_ctl::bitset_view_t>::size_type
    bitset_crtp_base<details::bitset_types::parallelism_setting{false}, legacy_embedded_ctl::bitset_view_t>::count(
        const bit_index_type idx_from, bit_index_type idx_to) const noexcept;

template typename bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                                   legacy_embedded_ctl::bitset_view_t>::bit_index_type
    bitset_crtp_base<details::bitset_types::parallelism_setting{false}, legacy_embedded_ctl::bitset_view_t>::find_first(
        const bit_index_type idx_from, bit_index_type idx_to) const noexcept;

template typename bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                                   legacy_embedded_ctl::bitset_view_t>::bit_index_type
    bitset_crtp_base<details::bitset_types::parallelism_setting{false}, legacy_embedded_ctl::bitset_view_t>::find_next(
        const bit_index_type idx_from, const bit_index_type idx_to) const noexcept;

template typename bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                                   legacy_embedded_ctl::bitset_view_t>::bit_index_type
    bitset_crtp_base<details::bitset_types::parallelism_setting{false}, legacy_embedded_ctl::bitset_view_t>::find_from(
        block_index_type block_index, const block_index_type upper_bound_block_index) const noexcept;

template class bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                                legacy_embedded_ctl::dynamic_bitset_t>;
template class bitset_crtp_base<details::bitset_types::parallelism_setting{true},
                                legacy_embedded_ctl::dynamic_bitset_parallel_t>;
template class bitset_crtp_base<details::bitset_types::parallelism_setting{false},
                                legacy_embedded_ctl::bitset_mutable_view_t>;

}  // namespace celonis::accelerator::legacy_embedded_ctl