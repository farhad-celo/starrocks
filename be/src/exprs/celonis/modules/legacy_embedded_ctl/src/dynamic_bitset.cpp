#include "legacy_embedded_ctl/dynamic_bitset.h"

#include <algorithm>
#include <bit>
#include <oneapi/tbb/parallel_for.h>
#include <type_traits>

#include "legacy_embedded_ctl/conversion.h"
#include "legacy_embedded_ctl/static_array.h"
#include "legacy_embedded_ctl/utils/allocation_messages.h"
#include "legacy_embedded_ctl/utils/allocation_priority.h"
#include "legacy_embedded_ctl/utils/allocation_reason.h"
#include "modules/common/aligned_blocked_range.h"

namespace celonis::accelerator::legacy_embedded_ctl {

namespace {
constexpr size_t GRAIN_SIZE = 4096;
}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING>::dynamic_bitset(const size_type size, const bool default_value)
    : dynamic_bitset{size, default_value,
                     make_default_tracking_allocator<block_type>(LEGACY_EMBEDDED_ALLOC_MSG(MEMBER_INIT_MSG),
                                                                 utils::allocation_priority::LOW, default_value)} {}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING>::dynamic_bitset(const size_type size, const bool default_value,
                                                    allocator_type allocator)
    : dynamic_bitset{size, allocator} {
  std::fill(bitset_data_.begin(), bitset_data_.end(), default_value ? details::MASK_ALL_SET : details::MASK_ALL_UNSET);
  // Special case: if 'default_value' is true, all bits in each block have been set above.
  // However, if 'size() % BLOCK_SIZE != 0' (e.g., size: 1, 63, 65, ...), the last block contains set bits after
  // position 'size() - 1'. This causes issues in algorithms which expect all bits in the range [size(), infinity)
  // to be unset/zero
  if (default_value && (this->size() % base_type::BLOCK_SIZE != 0)) {
    // clear bits after position 'bit_idx' in block 'blk_idx'
    this->zero_unused_bits();
  }
}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING>::dynamic_bitset(const dynamic_bitset& other)
    : dynamic_bitset{other.size(), other.bitset_data_.get_allocator()} {
  std::transform(other.bitset_data_.cbegin(), other.bitset_data_.cend(), bitset_data_.begin(),
                 [this](const block_type& block) { return this->get_block_value(block); });
}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING>::dynamic_bitset(const size_type size, allocator_type allocator)
    : size_{size},
      bitset_data_{make_static_array_for_overwrite<block_type>(details::calc_number_of_bitset_blocks(size_),
                                                               LEGACY_EMBEDDED_ALLOC_MSG(MEMBER_INIT_MSG), std::move(allocator))} {}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING>& dynamic_bitset<PARALLELISM_SETTING>::operator=(const dynamic_bitset& other) {
  if (other.num_blocks() != num_blocks()) {
    dynamic_bitset cpy{other};
    swap(*this, cpy);
  } else {
    // if the other bitset fits into the already allocated memory, do not reallocate but reuse existing allocated memory
    size_ = other.size();
    std::transform(other.bitset_data_.cbegin(), other.bitset_data_.cend(), bitset_data_.begin(),
                   [this](const block_type& block) { return this->get_block_value(block); });
  }
  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING>& dynamic_bitset<PARALLELISM_SETTING>::operator&=(
    const dynamic_bitset& rhs) noexcept {
  legacy_embedded_debug_assert(size() == rhs.size());

  common::safe_aligned_blocked_range<size_t> range{0, num_blocks(), GRAIN_SIZE};
  tbb::parallel_for(range, [this, &rhs](const common::aligned_blocked_range& section) {
    for (size_t idx{section.begin}; idx < section.end; ++idx) {
      bitset_data_[idx] &= rhs.bitset_data_[idx];
    }
  });

  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING> dynamic_bitset<PARALLELISM_SETTING>::operator&(const dynamic_bitset& rhs) const {
  legacy_embedded_debug_assert(size() == rhs.size());
  dynamic_bitset result{size_, bitset_data_.get_allocator()};

  common::safe_aligned_blocked_range<size_t> range{0, num_blocks(), GRAIN_SIZE};
  tbb::parallel_for(range, [this, &rhs, &result](const common::aligned_blocked_range& section) {
    for (size_t idx{section.begin}; idx < section.end; ++idx) {
      result.bitset_data_[idx] = bitset_data_[idx] & rhs.bitset_data_[idx];
    }
  });

  return result;
}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING>& dynamic_bitset<PARALLELISM_SETTING>::operator|=(
    const dynamic_bitset& rhs) noexcept {
  legacy_embedded_debug_assert(size() == rhs.size());

  common::safe_aligned_blocked_range<size_t> range{0, num_blocks(), GRAIN_SIZE};
  tbb::parallel_for(range, [this, &rhs](const common::aligned_blocked_range& section) {
    for (size_t idx{section.begin}; idx < section.end; ++idx) {
      bitset_data_[idx] |= rhs.bitset_data_[idx];
    }
  });

  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING> dynamic_bitset<PARALLELISM_SETTING>::operator|(const dynamic_bitset& rhs) const {
  legacy_embedded_debug_assert(size() == rhs.size());
  dynamic_bitset result{size_, bitset_data_.get_allocator()};

  common::safe_aligned_blocked_range<size_t> range{0, num_blocks(), GRAIN_SIZE};
  tbb::parallel_for(range, [this, &rhs, &result](const common::aligned_blocked_range& section) {
    for (size_t idx{section.begin}; idx < section.end; ++idx) {
      result.bitset_data_[idx] = bitset_data_[idx] | rhs.bitset_data_[idx];
    }
  });

  return result;
}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING>& dynamic_bitset<PARALLELISM_SETTING>::operator^=(
    const dynamic_bitset& rhs) noexcept {
  legacy_embedded_debug_assert(size() == rhs.size());

  common::safe_aligned_blocked_range<size_t> range{0, num_blocks(), GRAIN_SIZE};
  tbb::parallel_for(range, [this, &rhs](const common::aligned_blocked_range& section) {
    for (size_t idx{section.begin}; idx < section.end; ++idx) {
      bitset_data_[idx] ^= rhs.bitset_data_[idx];
    }
  });

  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING>
dynamic_bitset<PARALLELISM_SETTING> dynamic_bitset<PARALLELISM_SETTING>::operator^(const dynamic_bitset& rhs) const {
  legacy_embedded_debug_assert(size() == rhs.size());
  dynamic_bitset result{size_, bitset_data_.get_allocator()};

  common::safe_aligned_blocked_range<size_t> range{0, num_blocks(), GRAIN_SIZE};
  tbb::parallel_for(range, [this, &rhs, &result](const common::aligned_blocked_range& section) {
    for (size_t idx{section.begin}; idx < section.end; ++idx) {
      result.bitset_data_[idx] = bitset_data_[idx] ^ rhs.bitset_data_[idx];
    }
  });

  return result;
}

template <parallelism_settings_t PARALLELISM_SETTING>
typename dynamic_bitset<PARALLELISM_SETTING>::block_index_type dynamic_bitset<PARALLELISM_SETTING>::num_blocks()
    const noexcept {
  return static_cast<block_index_type>(bitset_data_.size());
}

template <parallelism_settings_t PARALLELISM_SETTING>
bool dynamic_bitset<PARALLELISM_SETTING>::empty() const noexcept {
  return size() == 0;
}

template <parallelism_settings_t U>
void swap(dynamic_bitset<U>& lhs, dynamic_bitset<U>& rhs) noexcept {
  using std::swap;
  swap(lhs.size_, rhs.size_);
  swap(lhs.bitset_data_, rhs.bitset_data_);
}

template class dynamic_bitset<details::bitset_types::parallelism_setting{false}>;
template class dynamic_bitset<details::bitset_types::parallelism_setting{true}>;

}  // namespace celonis::accelerator::legacy_embedded_ctl
