#pragma once

#include <memory>

#include "ctl/assert.h"
#include "modules/common/int_types.h"
#include "modules/memory/null_flags.h"

namespace celonis::accelerator::memory::management {

class const_bitset_data_accessor final {
 public:
  const_bitset_data_accessor(std::shared_ptr<const null_flags_bitset_t> bitset_data, const size_t bitset_size)
      : bitset_data_{std::move(bitset_data)}, bitset_size_{bitset_size} {}

  [[nodiscard]] bool operator[](const null_flags_bitset_t::bit_index_type idx) const noexcept {
    debug_assert(idx < bitset_size_);
    return (*bitset_data_)[idx];  // NOLINT(clang-analyzer-core.uninitialized.UndefReturn)
  }

  [[nodiscard]] size_t size() const noexcept { return bitset_size_; }

  [[nodiscard]] const null_flags_bitset_t* get() const noexcept { return bitset_data_.get(); }

  [[nodiscard]] const null_flags_bitset_t& operator*() const noexcept { return bitset_data_.operator*(); }

  [[nodiscard]] const null_flags_bitset_t* operator->() const noexcept { return bitset_data_.operator->(); }

 private:
  std::shared_ptr<const null_flags_bitset_t> bitset_data_;
  size_t bitset_size_;
};

}  // namespace celonis::accelerator::memory::management
