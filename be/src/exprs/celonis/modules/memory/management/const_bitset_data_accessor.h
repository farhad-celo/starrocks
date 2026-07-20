#pragma once

#include <memory>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/bitset_view.h"
#include "modules/common/int_types.h"
#include "modules/memory/null_flags_fwd.h"

namespace celonis::accelerator::memory::management {

class const_bitset_data_accessor final {
 public:
  const_bitset_data_accessor(legacy_embedded_ctl::bitset_view_t bitset_view,
                             legacy_embedded_ctl::shared_static_array<const uint64_t> data)
      : bitset_view_{bitset_view}, bitset_data_{std::move(data)} {}

  [[nodiscard]] bool operator[](const null_flags_bitset_t::bit_index_type idx) const noexcept {
    return bitset_view_.test(idx);
  }

  [[nodiscard]] size_t size() const noexcept { return bitset_view_.size(); }

  [[nodiscard]] legacy_embedded_ctl::bitset_view_t get() const noexcept { return bitset_view_; }

  [[nodiscard]] bool test(const null_flags_bitset_t::bit_index_type index) const noexcept {
    return bitset_view_.test(index);
  }

  [[nodiscard]] const uint64_t* data() const noexcept { return bitset_view_.data(); }

  [[nodiscard]] bool any() const noexcept { return bitset_view_.any(); }

 private:
  legacy_embedded_ctl::bitset_view_t bitset_view_;
  legacy_embedded_ctl::shared_static_array<const uint64_t> bitset_data_;  // placeholder to keep the data in memory
};

}  // namespace celonis::accelerator::memory::management
