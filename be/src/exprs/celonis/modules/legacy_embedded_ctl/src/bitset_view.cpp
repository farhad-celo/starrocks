#include "legacy_embedded_ctl/bitset_view.h"

#include <tbb/parallel_for.h>

#include "modules/common/aligned_blocked_range.h"

namespace celonis::accelerator::legacy_embedded_ctl {

namespace {
constexpr size_t GRAIN_SIZE = 4096;
}

template <parallelism_settings_t PARALLELISM_SETTING>
template <typename BITSET_VIEW>
bitset_mutable_view<PARALLELISM_SETTING>& bitset_mutable_view<PARALLELISM_SETTING>::operator&=(
    const BITSET_VIEW& rhs) noexcept {
  legacy_embedded_debug_assert(size() == rhs.size());

  common::safe_aligned_blocked_range<size_t> range{0, view_.size(), GRAIN_SIZE};
  tbb::parallel_for(range, [this, &rhs](const common::aligned_blocked_range& section) {
    for (size_t idx{section.begin}; idx < section.end; ++idx) {
      view_[idx] &= rhs.view_[idx];
    }
  });

  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING>
template <typename BITSET_VIEW>
bitset_mutable_view<PARALLELISM_SETTING>& bitset_mutable_view<PARALLELISM_SETTING>::operator|=(
    const BITSET_VIEW& rhs) noexcept {
  legacy_embedded_debug_assert(size() == rhs.size());

  common::safe_aligned_blocked_range<size_t> range{0, view_.size(), GRAIN_SIZE};
  tbb::parallel_for(range, [this, &rhs](const common::aligned_blocked_range& section) {
    for (size_t idx{section.begin}; idx < section.end; ++idx) {
      view_[idx] |= rhs.view_[idx];
    }
  });

  return *this;
}

template <parallelism_settings_t PARALLELISM_SETTING>
template <typename BITSET_VIEW>
bitset_mutable_view<PARALLELISM_SETTING>& bitset_mutable_view<PARALLELISM_SETTING>::operator^=(
    const BITSET_VIEW& rhs) noexcept {
  legacy_embedded_debug_assert(size() == rhs.size());

  common::safe_aligned_blocked_range<size_t> range{0, view_.size(), GRAIN_SIZE};
  tbb::parallel_for(range, [this, &rhs](const common::aligned_blocked_range& section) {
    for (size_t idx{section.begin}; idx < section.end; ++idx) {
      view_[idx] ^= rhs.view_[idx];
    }
  });

  return *this;
}

template bitset_mutable_view_t& bitset_mutable_view_t::operator&=(const bitset_view_t& rhs) noexcept;
template bitset_mutable_view_t& bitset_mutable_view_t::operator&=(const bitset_mutable_view_t& rhs) noexcept;
template bitset_mutable_view_t& bitset_mutable_view_t::operator|=(const bitset_view_t& rhs) noexcept;
template bitset_mutable_view_t& bitset_mutable_view_t::operator|=(const bitset_mutable_view_t& rhs) noexcept;
template bitset_mutable_view_t& bitset_mutable_view_t::operator^=(const bitset_view_t& rhs) noexcept;
template bitset_mutable_view_t& bitset_mutable_view_t::operator^=(const bitset_mutable_view_t& rhs) noexcept;

template bitset_mutable_view_parallel_t& bitset_mutable_view_parallel_t::operator&=(
    const bitset_view_parallel_t& rhs) noexcept;
template bitset_mutable_view_parallel_t& bitset_mutable_view_parallel_t::operator&=(
    const bitset_mutable_view_parallel_t& rhs) noexcept;
template bitset_mutable_view_parallel_t& bitset_mutable_view_parallel_t::operator|=(
    const bitset_view_parallel_t& rhs) noexcept;
template bitset_mutable_view_parallel_t& bitset_mutable_view_parallel_t::operator|=(
    const bitset_mutable_view_parallel_t& rhs) noexcept;
template bitset_mutable_view_parallel_t& bitset_mutable_view_parallel_t::operator^=(
    const bitset_view_parallel_t& rhs) noexcept;
template bitset_mutable_view_parallel_t& bitset_mutable_view_parallel_t::operator^=(
    const bitset_mutable_view_parallel_t& rhs) noexcept;

}  // namespace celonis::accelerator::legacy_embedded_ctl