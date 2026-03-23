#pragma once

#include <numeric>
#include <ranges>
#include <vector>

#include <tbb/blocked_range.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/conversion.h"
#include "legacy_embedded_ctl/interval.h"
#include "modules/common/exceptions.h"
#include "modules/common/int_types.h"
#include "modules/memory/column.h"
#include "modules/memory/const_abstract_column_ptrs_accessor.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::common {

struct case_aligned_range {
  memory::column_t case_column;
  const common::execution_context& context;
  size_t grainsize;
  size_t begin;  // [begin1,end1) is 1st sequence to be merged
  size_t end;    // [begin2,end2) is 2nd sequence to be merged

  [[nodiscard]] bool empty() const { return end - begin == 0; }

  [[nodiscard]] bool is_divisible() const {
    const auto& col_ptrs{case_column->get_column_pointers(context)};
    if (begin == end ||
        col_ptrs.get_ptr_slow(static_cast<row_id>(begin)) == col_ptrs.get_ptr_slow(static_cast<row_id>(end) - 1)) {
      return false;
    }
    return end - begin > grainsize;
  }

  // The use of virtual column pointers access should not hurt here due to the large grainsize and usually small cases
  case_aligned_range(case_aligned_range& r, tbb::split /*ignore*/) : case_column{r.case_column}, context{r.context} {
    size_t middle = r.end + r.begin;
    middle = middle / 2;
    const auto& col_ptrs{case_column->get_column_pointers(context)};
    const memory::const_abstract_column_ptrs_accessor col_ptrs_ac{col_ptrs};
    row_id middle_case = col_ptrs.get_ptr_slow(static_cast<row_id>(middle));
    // find end of case, if middle already equals to end, find start of case
    if (col_ptrs_ac[static_cast<row_id>(middle)] == col_ptrs_ac[static_cast<row_id>(r.end) - 1]) {
      while (middle > r.begin) {
        middle--;
        if (col_ptrs_ac[static_cast<row_id>(middle)] != middle_case) {
          ++middle;
          break;
        }
      }
    } else {
      while (middle < r.end && col_ptrs_ac[static_cast<row_id>(middle)] == middle_case) {
        middle++;
      }
    }
    end = r.end;
    begin = middle;
    r.end = middle;
    grainsize = r.grainsize;
  }

  explicit case_aligned_range(memory::column_t case_id_column, const common::execution_context& context,
                              const size_t grain_size)
      : case_column(std::move(case_id_column)),
        context(context),
        grainsize(grain_size),
        begin(0),
        end(static_cast<size_t>(this->case_column->get_row_count())) {}
};

template <legacy_embedded_ctl::parallelism_settings_t PARALLELISM_SETTING>
struct set_bit_aligned_range {
  const legacy_embedded_ctl::dynamic_bitset<PARALLELISM_SETTING>& bitset;
  size_t begin;
  size_t end;
  size_t grain_size;
  explicit set_bit_aligned_range(const legacy_embedded_ctl::dynamic_bitset<PARALLELISM_SETTING>& bitset, size_t begin,
                                 size_t end, size_t grain_size = 1) noexcept
      : bitset{bitset}, begin{begin}, end{end}, grain_size{grain_size} {}
  set_bit_aligned_range(set_bit_aligned_range& other, tbb::split /**/) noexcept
      : bitset{other.bitset}, begin{other.midpoint()}, end{other.end}, grain_size{other.grain_size} {
    other.end = begin;
  }
  // TODO(a.swoboda) we could also add a proportional split constructor
  [[nodiscard]] constexpr bool empty() const noexcept { return begin == end; }
  [[nodiscard]] constexpr bool is_divisible() const noexcept { return end >= begin && end - begin > grain_size; }
  [[nodiscard]] size_t midpoint() const noexcept {
    const auto middle{std::midpoint(begin, end)};
    auto result{bitset.find_next(middle, end)};
    if (result != bitset.npos) {
      return result;
    }
    // else, we need to move backwards
    result = middle;
    while (result > begin && !bitset.test(result)) {
      --result;
    }
    return result;
  }
};

template <std::bidirectional_iterator ITERATOR, typename PROJECTION>
struct group_aligned_range {
  ITERATOR begin;
  ITERATOR end;
  size_t grain_size;
  [[no_unique_address]] PROJECTION projection;
  [[nodiscard]] bool empty() const { return std::distance(begin, end) == 0; }
  [[nodiscard]] bool is_divisible() const {
    return legacy_embedded_ctl::cast<size_t>(std::distance(begin, end)) > grain_size &&
           std::ranges::mismatch(std::next(begin), end, begin, end, std::ranges::equal_to{}, projection, projection)
                   .in1 != end;
  }
  group_aligned_range(ITERATOR begin, ITERATOR end, size_t grain_size, PROJECTION projection)
      : begin{begin}, end{end}, grain_size{grain_size}, projection{std::move(projection)} {}
  group_aligned_range(group_aligned_range& other, tbb::split /**/)
      : begin{std::next(other.begin, std::distance(other.begin, other.end) / 2)},
        end{other.end},
        grain_size{other.grain_size},
        projection{other.projection} {
    // adjust begin
    auto next =
        std::ranges::mismatch(std::next(begin), end, begin, end, std::ranges::equal_to{}, projection, projection).in1;
    if (next == end) {  // we need to go in the other direction
      next = begin;
      while (next != other.begin) {
        --next;
        if (*next != *begin) {
          break;
        }
      }
      // next is pointing to the mismatching activity. The range is left inclusive, right exclusive. Therefore we need
      // to move the pointer one element ahead
      common::runtime_assert(next < end, "The first mismatch cannot be the end of the interval");
      ++next;
    }
    begin = next;
    other.end = begin;
  }
};

template <class CASE_PTR_ACCESSOR>
std::vector<legacy_embedded_ctl::half_open_interval<row_id>> generate_case_aligned_blocks(
    const CASE_PTR_ACCESSOR& case_ptr_ac, const row_id row_count, const size_t grainsize) {
  std::vector<legacy_embedded_ctl::half_open_interval<row_id>> ret;
  int64_t laststart = 0;
  int64_t nextsplit = 0;
  // Must have same type for std::min()
  auto grain_size_row_id = static_cast<row_id>(grainsize);
  while (nextsplit < row_count) {
    nextsplit = std::min(nextsplit + grain_size_row_id, static_cast<int64_t>(row_count) - 1);
    auto cur_case = case_ptr_ac[static_cast<row_id>(nextsplit)];
    nextsplit++;
    while (nextsplit < row_count) {
      auto next_case = case_ptr_ac[static_cast<row_id>(nextsplit)];
      if (next_case != cur_case) {
        break;
      }
      nextsplit++;
    }
    ret.emplace_back(laststart, nextsplit);
    laststart = nextsplit;
    legacy_embedded_debug_assert(nextsplit == row_count || case_ptr_ac[static_cast<row_id>(nextsplit) - 1] !=
                                                               case_ptr_ac[static_cast<row_id>(nextsplit)]);
  }
  return ret;
}

}  // namespace celonis::accelerator::common
