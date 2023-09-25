#pragma once

#include <algorithm>
#include <type_traits>

#include <tbb/blocked_range.h>

#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::common {

template <class TYPE = size_t>
struct safe_aligned_blocked_range {
  static_assert(std::is_same_v<size_t, TYPE> || std::is_same_v<row_id, TYPE>, "unsupported type");

  TYPE begin;  // [begin1,end1) is 1st sequence to be merged
  TYPE end;    // [begin2,end2) is 2nd sequence to be merged
  const size_t grain_size;

  [[nodiscard]] bool empty() const { return end - begin == 0; }

  [[nodiscard]] TYPE size() const { return end - begin; }

  [[nodiscard]] bool is_divisible() const { return static_cast<size_t>(end - begin) > grain_size; }

  safe_aligned_blocked_range(safe_aligned_blocked_range<TYPE>& r, tbb::split /*flag*/) : grain_size(r.grain_size) {
    // find the middle aligned to 64
    // we convert to size_t as unsigned bitwise operations are less magic and the sum of r.end and r.begin cannot
    // overflow, as long as the original range size type fulfills sizeof(type) <= sizeof(size_t)
    size_t middle = (static_cast<size_t>(r.end) + static_cast<size_t>(r.begin)) / 2;
    // bitmask to zero the 6 least significant bits
    middle &= ~((static_cast<size_t>(1) << 6) - 1);
    end = static_cast<TYPE>(r.end);
    begin = static_cast<TYPE>(middle);
    r.end = static_cast<TYPE>(middle);
  }

  safe_aligned_blocked_range(TYPE begin, TYPE end, const size_t grain_size = (1 << 15))
      : begin(begin), end(end), grain_size(std::max(static_cast<size_t>(128), grain_size)) {}
};

using aligned_blocked_range = safe_aligned_blocked_range<>;

}  // namespace celonis::accelerator::common
