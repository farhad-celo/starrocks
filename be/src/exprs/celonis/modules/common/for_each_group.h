#pragma once

#include <algorithm>

#include <tbb/parallel_for_each.h>

#include "legacy_embedded_ctl/conversion.h"
#include "legacy_embedded_ctl/interval.h"
#include "modules/common/case_aligned_range.h"
#include "modules/common/iterator/index_input_iterator.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::common {

/**
 * Given a data accessor for grouping and from and to row indices, repeatedly calls the provided function with the
 * indices of all groups (sub-ranges of equal values) within the range defined by from and to. In the typical case of
 * grouping over the whole accessor the other overloads should be preferred
 *
 * @tparam INDEX the template parameter to use for the indices of the group
 * @tparam ACCESSOR the grouper's accessor type. Must have operator[] overloaded to work with INDEX arguments
 * @tparam F the type of the function to call. Must be callable with rvalues of type
 * legacy_embedded_ctl::half_open_interval<INDEX>
 * @param from start of the range
 * @param to end of the range
 * @param accessor the accessor for the grouper
 * @param f the function to call for each group
 */
template <typename INDEX = row_id, typename ACCESSOR, typename F>
void for_each_group(INDEX from, INDEX to, ACCESSOR accessor, F f) {
  using index_iterator = common::iterator::index_input_iterator<INDEX>;
  for (index_iterator first{from}, last{first}, past_end{to}; first != past_end; first = last) {
    last = std::mismatch(std::next(first), past_end, first, [&](auto i, auto j) {
             return accessor[i] == accessor[j];
           }).first;
    f(legacy_embedded_ctl::half_open_interval<INDEX>{*first, *last});
  }
}

/**
 * Given a data accessor for grouping and from and to row indices, repeatedly calls the provided function with the
 * indices of all groups (sub-ranges of equal values) within the range defined by from and to, until that function
 * returns false. In the typical case of grouping over the whole accessor the other overloads should be preferred
 *
 * @tparam INDEX the template parameter to use for the indices of the group
 * @tparam ACCESSOR the grouper's accessor type. Must have operator[] overloaded to work with INDEX arguments
 * @tparam F the type of the function to call. Must be callable with rvalues of type
 * legacy_embedded_ctl::half_open_interval<INDEX>
 * @param from start of the range
 * @param to end of the range
 * @param accessor the accessor for the grouper
 * @param f the function to call for each group. Return true to continue, and false to stop iterating.
 */
template <typename INDEX = row_id, typename ACCESSOR, typename F>
void for_each_group_stopping(INDEX from, INDEX to, ACCESSOR accessor, F f) {
  using index_iterator = common::iterator::index_input_iterator<INDEX>;
  for (index_iterator first{from}, last{first}, past_end{to}; first != past_end; first = last) {
    last = std::mismatch(std::next(first), past_end, first, [&](auto i, auto j) {
             return accessor[i] == accessor[j];
           }).first;
    if (!f(legacy_embedded_ctl::half_open_interval<INDEX>{*first, *last})) {
      return;
    }
  }
}

/**
 * Given a data accessor for grouping, repeatedly calls the provided function with the indices of all groups (sub-ranges
 * of equal values)
 *
 * @tparam INDEX the template parameter to use for the indices of the group
 * @tparam ACCESSOR the grouper's accessor type. Must have operator[] overloaded to work with INDEX arguments
 * @tparam F the type of the function to call. Must be callable with rvalues of type
 * legacy_embedded_ctl::half_open_interval<INDEX>
 * @param accessor the accessor for the grouper
 * @param f the function to call for each group
 */
template <typename INDEX, typename ACCESSOR, typename F>
void for_each_group(ACCESSOR accessor, F f) {
  for_each_group<INDEX, ACCESSOR, F>(0, accessor.size(), accessor, f);
}

/**
 * Given a data accessor for grouping, repeatedly calls the provided function with the indices of all groups (sub-ranges
 * of equal values), until the function returns false.
 *
 * @tparam INDEX the template parameter to use for the indices of the group
 * @tparam ACCESSOR the grouper's accessor type. Must have operator[] overloaded to work with INDEX arguments
 * @tparam F the type of the function to call. Must be callable with rvalues of type
 * legacy_embedded_ctl::half_open_interval<INDEX>
 * @param accessor the accessor for the grouper
 * @param f the function to call for each group
 */
template <typename INDEX, typename ACCESSOR, typename F>
void for_each_group_stopping(ACCESSOR accessor, F f) {
  for_each_group_stopping<INDEX, ACCESSOR, F>(0, accessor.size(), accessor, f);
}

/**
 * Given a data accessor for grouping, repeatedly calls the provided function with the indices of all groups (sub-ranges
 * of equal values)
 * <br/>
 * Attention this is parallelized, <b>f</b> needs to be thread safe
 *
 * @tparam INDEX the template parameter to use for the indices of the group
 * @tparam ACCESSOR the grouper's accessor type. Must have operator[] overloaded to work with INDEX arguments
 * @tparam F the type of the function to call. Must be callable with rvalues of type
 * legacy_embedded_ctl::half_open_interval<INDEX>
 * @param accessor the accessor for the grouper
 * @param grain_size grain size for parallelization
 * @param f the function to call for each group
 */
template <typename INDEX = row_id, typename ACCESSOR, typename F>
void for_each_group(ACCESSOR accessor, size_t grain_size, F f) {
  const auto group_aligned_range{
      common::generate_case_aligned_blocks(accessor, legacy_embedded_ctl::cast<row_id>(accessor.size()), grain_size)};
  tbb::parallel_for_each(group_aligned_range.begin(), group_aligned_range.end(), [&f, &accessor](auto range) {
    for_each_group<INDEX, ACCESSOR, F>(legacy_embedded_ctl::cast<INDEX>(range.begin()),
                                       legacy_embedded_ctl::cast<INDEX>(range.end()), accessor, f);
  });
}

template <typename INDEX = row_id, typename ACCESSOR, typename F, typename LOCALS>
requires requires(LOCALS&& locals) { locals.local(); }
void for_each_group(ACCESSOR accessor, size_t grain_size, F f, LOCALS&& locals) {
  const auto group_aligned_range{
      common::generate_case_aligned_blocks(accessor, legacy_embedded_ctl::cast<row_id>(accessor.size()), grain_size)};
  tbb::parallel_for_each(group_aligned_range.begin(), group_aligned_range.end(), [&f, &accessor, &locals](auto range) {
    auto& local{locals.local()};
    for_each_group<INDEX>(legacy_embedded_ctl::cast<INDEX>(range.begin()),
                          legacy_embedded_ctl::cast<INDEX>(range.end()), accessor,
                          [f, &local](auto interval) mutable { return f(interval, local); });
  });
}

template <typename ACCESSOR, typename F>
void for_each_group(ACCESSOR accessor, F f) {
  return for_each_group<decltype(accessor.size())>(std::move(accessor), std::move(f));
}

template <typename ACCESSOR, typename F>
void for_each_group_stopping(ACCESSOR accessor, F f) {
  return for_each_group_stopping<decltype(accessor.size())>(std::move(accessor), std::move(f));
}

}  // namespace celonis::accelerator::common
