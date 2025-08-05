#include "incremental_eventlog.h"

#include <algorithm>
#include <iterator>
#include <numeric>
#include <span>
#include <unordered_map>

#include <tbb/enumerable_thread_specific.h>

#include "legacy_embedded_ctl/bits/half_open_interval.h"
#include "modules/common/for_each_group.h"
#include "modules/common/iterator/index_input_iterator.h"
#include "modules/memory/management/memory_checked_containers.h"
#include "modules/operators/process/variant_view.h"

namespace celonis::accelerator::operators::mo {

namespace {

template <typename COLUMN_POINTER_TYPE>
using hash_type = process::variant_hash<COLUMN_POINTER_TYPE>;
template <typename COLUMN_POINTER_TYPE>
using equal_to_type = process::variant_equal_to<COLUMN_POINTER_TYPE>;
template <typename COLUMN_POINTER_TYPE>
using variant_counter_type = std::unordered_map<process::variant_view, row_id, hash_type<COLUMN_POINTER_TYPE>,
                                                equal_to_type<COLUMN_POINTER_TYPE>>;

template <typename ACTIVITY_ACCESSOR, typename GROUP_ACCESSOR>
auto count_group_occurrences(ACTIVITY_ACCESSOR activity_accessor, GROUP_ACCESSOR group_accessor,
                             const cube::filter_bitset_t& filter, size_t grain_size) {
  using column_pointer_type = typename decltype(activity_accessor)::type;

  process::variant_hash<column_pointer_type> hash{activity_accessor, filter};
  process::variant_equal_to<column_pointer_type> equal_to{activity_accessor, filter};

  variant_counter_type<column_pointer_type> counter{0, hash, equal_to};
  tbb::enumerable_thread_specific<variant_counter_type<column_pointer_type>> counters{0, hash, equal_to};
  common::for_each_group<size_t>(group_accessor, grain_size, [&counters](auto group) { ++counters.local()[group]; });
  counters.combine_each([&counter](auto& c) {
    counter.merge(c);
    std::ranges::for_each(c, [&counter](auto k_v) { counter[k_v.first] += k_v.second; });
  });
  return counter;
}

/**
 * Given two sorted input ranges, find the first (smallest) element of their intersection.
 *
 * You can also think of this as the smallest element that is in both sets.
 * If the intersection is empty (e.g., because either of the inputs is empty), then the past-the-end iterators will be
 * returned.
 *
 * @tparam INPUT_ITERATOR0 the type of iterator into the first set
 * @tparam INPUT_ITERATOR1 the type of iterator into the second set
 * @param lhs_first iterator to the first element of the first set
 * @param lhs_last past-the-end iterator of the first set
 * @param rhs_first iterator to the first element of the second set
 * @param rhs_last past-the-end iterator of the second set
 * @return iterators into the first and the second sets, pointing to the first equivalent elements.
 */
template <typename INPUT_ITERATOR0, typename INPUT_ITERATOR1>
std::pair<INPUT_ITERATOR0, INPUT_ITERATOR1> set_find_first_in_intersection(INPUT_ITERATOR0 lhs_first,
                                                                           INPUT_ITERATOR0 lhs_last,
                                                                           INPUT_ITERATOR1 rhs_first,
                                                                           INPUT_ITERATOR1 rhs_last) {
  while (lhs_first != lhs_last && rhs_first != rhs_last) {
    // look for an element in both sets
    if (*lhs_first < *rhs_first) {
      ++lhs_first;
    } else if (*rhs_first < *lhs_first) {
      ++rhs_first;
    } else {  // *lhs_first and *rhs_first are equivalent
      return {lhs_first, rhs_first};
    }
  }
  // no match
  return {lhs_last, rhs_last};
}

/**
 * Similar to std::set_difference, but assumes that the first input range is also the output range.
 *
 * Both input ranges have to be sorted. The output range is also sorted.
 *
 * @tparam INPUT_OUTPUT_ITERATOR the type of iterators to the first range (input and output)
 * @tparam INPUT_ITERATOR the type of iterators to the second range (only input)
 * @param lhs_first iterator to the first element of the first range
 * @param lhs_last past-the-end iterator of the first range
 * @param rhs_first iterator to the first element of the second range
 * @param rhs_last past-the-end iterator of the second range
 * @return
 */
template <typename INPUT_OUTPUT_ITERATOR, typename INPUT_ITERATOR>
INPUT_OUTPUT_ITERATOR set_difference_inplace(INPUT_OUTPUT_ITERATOR lhs_first, INPUT_OUTPUT_ITERATOR lhs_last,
                                             INPUT_ITERATOR rhs_first, INPUT_ITERATOR rhs_last) {
  if (lhs_first == lhs_last || rhs_first == rhs_last) {
    return lhs_last;
  }
  std::tie(lhs_first, rhs_first) = set_find_first_in_intersection(lhs_first, lhs_last, rhs_first, rhs_last);
  if (lhs_first == lhs_last) {
    return lhs_first;
  }
  // now, we have *lhs_first == *rhs_first, so we start moving elements
  auto result{lhs_first};
  ++lhs_first;
  // we have established *rhs_first < *lhs_first now.
  for (;;) {
    if (lhs_first == lhs_last) {
      return result;
    }
    if (rhs_first == rhs_last) {
      return std::move(lhs_first, lhs_last, result);
    }
    if (*lhs_first < *rhs_first) {  // element not found in rhs, keep it
      *result = std::move(*lhs_first);
      ++result;
      ++lhs_first;
    } else if (*rhs_first < *lhs_first) {  // keep looking for this element in rhs
      ++rhs_first;
    } else {  // *lhs_first == *rhs_first, so we skip this element
      ++lhs_first;
      ++rhs_first;
    }
  }
}

template <typename COLUMN_PTR_TYPE>
struct distance_sort_key {
  distance_sort_key(const std::vector<process::variant_view>& covering, const cube::filter_bitset_t& filter,
                    typename memory::column_ptrs_impl<COLUMN_PTR_TYPE>::const_data_accessor_t activities)
      : filter_{filter},
        activities_{activities},
        buffer_{legacy_embedded_ctl::make_static_array_for_overwrite<COLUMN_PTR_TYPE>(
            std::accumulate(begin(covering), end(covering), size_t{0},
                            [get_size = process::variant_view_size{filter}](auto acc, const auto& view) {
                              return acc + get_size(view);
                            }),
            LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG))},
        reference_points_(covering.size()) {
    std::transform(begin(covering), end(covering), begin(reference_points_),
                   [it = begin(buffer_), &filter, &activities](auto view) mutable {
                     auto* const first{it};
                     for (auto index{begin(view)}, end_index{end(view)}; index < end_index; ++index) {
                       if (!filter.test(index)) {
                         *it = activities[index];
                         ++it;
                       }
                     }
                     std::sort(first, it);
                     return std::span{first, it};
                   });
  }

  auto operator()(process::variant_view variant) const noexcept {
    thread_local std::vector<COLUMN_PTR_TYPE> sorted_buffer{};
    sorted_buffer.clear();
    for (auto it{variant.begin()}; it != variant.end(); ++it) {
      if (!filter_.test(it)) {
        sorted_buffer.emplace_back(activities_[it]);
      }
    }
    std::sort(begin(sorted_buffer), end(sorted_buffer));
    return std::accumulate(
        begin(reference_points_), end(reference_points_), std::numeric_limits<size_t>::max(),
        [this](auto min_diff, const auto& ref) { return std::min(min_diff, count_differences(ref, sorted_buffer)); });
  }

 private:
  const cube::filter_bitset_t& filter_;
  typename memory::column_ptrs_impl<COLUMN_PTR_TYPE>::const_data_accessor_t activities_;
  legacy_embedded_ctl::static_array<COLUMN_PTR_TYPE> buffer_;
  std::vector<std::span<COLUMN_PTR_TYPE>> reference_points_{};

  [[nodiscard]] auto count_differences(std::span<COLUMN_PTR_TYPE> ref,
                                       const std::vector<COLUMN_PTR_TYPE>& other) const {
    // TODO(a.swoboda) we only need the size, so we can compute this more efficiently if it is a bottleneck
    thread_local std::vector<COLUMN_PTR_TYPE> result{};
    result.clear();
    std::set_symmetric_difference(ref.begin(), ref.end(), begin(other), end(other), std::back_inserter(result));
    return result.size();
  }
};

template <typename COLUMN_PTR_TYPE>
struct event_subset {
  // The returned subset is not thread-safe!
  static auto create_factory(const cube::filter_bitset_t& filter,
                             typename memory::column_ptrs_impl<COLUMN_PTR_TYPE>::const_data_accessor_t activities) {
    const auto size{filter.size() - filter.count()};
    auto buffer{legacy_embedded_ctl::make_static_array_for_overwrite<COLUMN_PTR_TYPE>(size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))};
    auto* it{buffer.begin()};
    return [&filter, activities, buffer = std::move(buffer), it](process::variant_view v) mutable {
      auto* const old_it{it};
      for (auto index{v.begin()}, last{v.end()}; index != last; ++index) {
        if (!filter.test(index)) {
          *it++ = activities[index];
        }
      }
      std::sort(old_it, it);
      it = std::unique(old_it, it);
      return event_subset{std::span<COLUMN_PTR_TYPE>{old_it, it}};
    };
  }

  event_subset() = default;

  [[nodiscard]] constexpr auto size() const noexcept { return view_.size(); }
  [[nodiscard]] constexpr auto empty() const noexcept { return view_.empty(); }

  template <typename ITERATOR>
  void subtract_ordered(ITERATOR first, ITERATOR last) {
    // interestingly, std::set_difference does not allow the output range to overlap with any of the input ranges, which
    // is why we have to re-implement it here
    view_ = {view_.begin(), set_difference_inplace(view_.begin(), view_.end(), first, last)};
  }

  template <typename ITERATOR>
  void intersect_ordered(ITERATOR first, ITERATOR last) {
    // interestingly, std::set_intersection does not allow the output range to overlap with any of the input ranges,
    // which is why we have to re-implement it here
    if (view_.empty() || first == last) {
      return;
    }
    // get first element that is not kept (because it is not in span{first, last})
    auto output_it{view_.begin()};
    for (; output_it != view_.end(); ++output_it) {
      first = std::lower_bound(first, last, *output_it);
      if (first == last) {
        view_ = {view_.begin(), output_it};
        return;
      }
      if (*first != *output_it) {
        break;
      }
    }
    if (output_it == view_.end()) {
      // there is nothing to remove
      return;
    }
    // we have established *output_it < *first
    auto input_it{std::next(output_it)};
    while (input_it != view_.end() && first != last) {
      // if *input_it == *first, then copy and increment all iterators
      // else, increment the iterator pointing to the smaller element
      if (*input_it == *first) {
        *output_it++ = *input_it++;
        ++first;
      } else if (*input_it < *first) {
        ++input_it;
      } else {
        ++first;
      }
    }
    // update the view
    view_ = {view_.begin(), output_it};
  }

  [[nodiscard]] constexpr auto begin() noexcept { return view_.begin(); }
  [[nodiscard]] constexpr auto begin() const noexcept { return view_.begin(); }
  [[nodiscard]] constexpr auto end() noexcept { return view_.end(); }
  [[nodiscard]] constexpr auto end() const noexcept { return view_.end(); }

 private:
  explicit event_subset(std::span<COLUMN_PTR_TYPE> view) : view_{view} {}

  std::span<COLUMN_PTR_TYPE> view_{};
};

template <typename P>
constexpr auto begin(event_subset<P>& e) {
  return e.begin();
}
template <typename P>
constexpr auto begin(const event_subset<P>& e) {
  return e.begin();
}
template <typename P>
constexpr auto end(event_subset<P>& e) {
  return e.end();
}
template <typename P>
constexpr auto end(const event_subset<P>& e) {
  return e.end();
}

struct exec_compute_counter {
  struct ranking_result {
    legacy_embedded_ctl::shared_static_array<row_id> ranking;
    legacy_embedded_ctl::shared_static_array<row_id> accumulated_counts;
    size_t cover_size{};
  };

  row_id case_domain_count;
  const cube::filter_bitset_t& filter;
  const std::vector<row_id>& activities;
  const common::execution_context& context;

  template <typename TUPLE>
  ranking_result operator()(const TUPLE& ptrs) const {
    const auto activity_accessor{std::get<0>(ptrs).get_const_accessor()};
    const auto case_accessor{std::get<1>(ptrs).get_const_accessor()};
    using column_pointer_type = typename decltype(activity_accessor)::type;
    process::variant_less<column_pointer_type> less{activity_accessor, filter};

    // TODO(j.kruska) CPL-7757 Determine an appropriate grain size here
    constexpr size_t grain_size{2 << 16};
    auto counter{count_group_occurrences(activity_accessor, case_accessor, filter, grain_size)};

    // we ignore empty traces (analogously to the inductive miner), so remove the empty trace from the counter
    counter.erase(process::variant_view{});
    if (counter.empty()) {
      return {legacy_embedded_ctl::shared_static_array<row_id>{}, legacy_embedded_ctl::shared_static_array<row_id>()};
    }

    // now that we have all traces and counts, we compute the weighted set cover
    // first, compute the "bag of activities" (BOA) representation
    auto boa_factory{event_subset<column_pointer_type>::create_factory(filter, activity_accessor)};
    using set_cover_data = std::tuple<process::variant_view, row_id, event_subset<column_pointer_type>>;
    memory::management::checked_vector_t<set_cover_data> boas{
        counter.size(),
        memory::management::checked_allocator<decltype(boas)>(context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))};

    std::vector<column_pointer_type> activity_set(begin(activities), end(activities));
    std::sort(begin(activity_set), end(activity_set));

    std::vector<process::variant_view> covering{};

    std::transform(begin(counter), end(counter), begin(boas), [&](const auto& p) {
      const auto& [variant, count]{p};
      auto boa{boa_factory(variant)};
      boa.intersect_ordered(begin(activity_set), end(activity_set));
      return set_cover_data{variant, count, boa};
    });
    constexpr auto boa_empty{[](const auto& boa) { return std::get<2>(boa).empty(); }};
    constexpr auto weight{[](const auto& boa) { return static_cast<double>(std::get<1>(boa)); }};
    constexpr auto boa_size{[](const auto& boa) { return std::get<2>(boa).size(); }};
    boas.erase(std::remove_if(begin(boas), end(boas), boa_empty), end(boas));
    // Repeat until all activities have been covered, or only activities that are not present are left:
    while (!activity_set.empty() && !boas.empty()) {
      // Greedily compute the variant that maximizes #(newly covered activities) * count, then update the rest
      // 1. Find max
      const auto max_it{std::max_element(begin(boas), end(boas), [&](const auto& lhs, const auto& rhs) {
        return weight(lhs) * static_cast<double>(boa_size(lhs)) < weight(rhs) * static_cast<double>(boa_size(rhs));
      })};
      // 2. Add the max to the initial cover, and remove all of its activities from all other BOAs and the activity set
      covering.emplace_back(std::get<0>(*max_it));
      const auto remove_from{begin(std::get<2>(*max_it))};
      const auto remove_to{end(std::get<2>(*max_it))};
      std::for_each(begin(boas), end(boas),
                    [remove_from, remove_to](auto& tup) { std::get<2>(tup).subtract_ordered(remove_from, remove_to); });
      activity_set.erase(set_difference_inplace(begin(activity_set), end(activity_set), remove_from, remove_to),
                         end(activity_set));
      // 3. Remove all BOAs that are now empty
      boas.erase(std::remove_if(begin(boas), end(boas), boa_empty), end(boas));
    }

    const auto sort_vector{get_sort_vector(activity_accessor, covering, counter)};

    ranking_result result{
        legacy_embedded_ctl::make_shared_static_array_for_overwrite<row_id>(case_domain_count, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG)),
        legacy_embedded_ctl::make_shared_static_array_for_overwrite<row_id>(counter.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG)),
        covering.size()};
    std::transform(begin(sort_vector), end(sort_vector), begin(result.accumulated_counts),
                   [&counter](const auto& p) { return counter[p]; });
    std::partial_sum(begin(result.accumulated_counts), end(result.accumulated_counts),
                     begin(result.accumulated_counts));
    // we can re-use the counter for the ranking map
    auto ranking_map = std::move(counter);
    ranking_map.insert_or_assign({}, std::numeric_limits<row_id>::max());
    for (row_id i{0}, num_variants{legacy_embedded_ctl::cast<row_id>(sort_vector.size())}; i != num_variants; ++i) {
      ranking_map[sort_vector[i]] = i;
    }
    common::for_each_group<size_t>(case_accessor, grain_size, [&ranking_map, &result, &case_accessor](auto group) {
      result.ranking[case_accessor[group.begin()]] = ranking_map.at(group);
    });
    return result;
  }

  // Bring the variants in the counter into the correct order for the incremental_eventlog: First the covering,
  // ascending in the variant count, then all the other variants in the counter, ordered by similarity to the covering
  // and variant count
  template <typename ACCESSOR>
  auto get_sort_vector(ACCESSOR&& activity_accessor, const std::vector<process::variant_view>& covering,
                       variant_counter_type<typename std::decay_t<ACCESSOR>::type>& counter) const {
    using column_pointer_type = typename std::decay_t<ACCESSOR>::type;
    // Now that we have (the approximation of) our set covering, order all variants
    // As ordering criterion, we use a notion of "similarity" that is basically the editing distance of the BOAs.
    const distance_sort_key<column_pointer_type> get_sort_key{covering, filter, activity_accessor};
    struct sort_key_type {
      size_t distance{};
      row_id count{};
      constexpr bool operator<(const sort_key_type& rhs) const noexcept {
        return std::tuple(distance, -count) < std::tuple(rhs.distance, -rhs.count);
      }
    };
    std::vector<std::pair<process::variant_view, sort_key_type>> sort_vector(counter.size());

    // Put the covering in the front of the sorting vector (highest count first)
    const auto [_, non_cover_it]{std::ranges::transform(covering, begin(sort_vector), [&counter](const auto& v) {
      return std::pair{v, sort_key_type{{}, counter[v]}};
    })};
    // Append all other variants that are not in the cover. Since the counter contains all variants, we temporarily
    // remove the covering from the counter, and then iterate over the counter
    std::vector<typename variant_counter_type<column_pointer_type>::node_type> cover_nodes(covering.size());
    std::ranges::transform(covering, begin(cover_nodes), [&counter](const auto& v) { return counter.extract(v); });
    std::ranges::transform(counter, non_cover_it, [&get_sort_key](const auto& p) {
      return std::pair{p.first, sort_key_type{get_sort_key(p.first), p.second}};
    });
    // Now that we have extracted all variants, add the cover back to the counter
    std::ranges::for_each(cover_nodes, [&counter](auto& node) { counter.insert(std::move(node)); });
    // Finally, we can sort all variants, to get their position in the incremental eventlog
    // Note that we expressly keep the covering in the front!
    constexpr auto sort_vector_less{[](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; }};
    std::sort(begin(sort_vector), non_cover_it, sort_vector_less);
    std::sort(non_cover_it, end(sort_vector), sort_vector_less);

    std::vector<process::variant_view> result(sort_vector.size());
    std::ranges::transform(sort_vector, begin(result), [](const auto& p) { return p.first; });

    return result;
  }
};

/**
 * This is a helper class that we use to compute the ranking of an element, i.e., given a map M, an object K of M's
 * key_type, and a random-access container C of ordered value_types of M, computes the index of K in C
 *
 * NB We originally had a lambda for this, but clang-tidy raised a false-positive warning, and this is arguably nicer
 * than silencing the warning
 * @tparam COUNTER The type of map used as a lookup for the key type
 * @tparam RANKING A container of COUNTER's value_type (a pair of const key_type and mapped_type), sorted according to
 * the ranking (so probably sorted according to the value of the COUNTER map's value_type)
 * @tparam CMP The comparison used for the RANKING's order
 */
template <typename COUNTER, typename RANKING, typename CMP>
struct counter_ranking_of {
  const COUNTER& counter;
  const RANKING& ranking;
  const CMP& cmp;
  auto operator()(process::variant_view span) const {
    const auto it{counter.find(span)};
    return it == end(counter)
               ? std::numeric_limits<row_id>::max()
               : static_cast<row_id>(std::lower_bound(begin(ranking), end(ranking), *it, cmp) - begin(ranking));
  }
};
template <typename COUNTER, typename RANKING, typename CMP>
counter_ranking_of(const COUNTER&, const RANKING&, const CMP&) -> counter_ranking_of<COUNTER, RANKING, CMP>;

struct exec_compute_ranking {
  struct ranking_result {
    legacy_embedded_ctl::shared_static_array<row_id> ranking;
    legacy_embedded_ctl::shared_static_array<row_id> accumulated_counts;
  };

  row_id case_domain_count;
  const cube::filter_bitset_t& filter;

  template <typename TUPLE>
  ranking_result operator()(const TUPLE& ptrs) const {
    const auto activity_accessor{std::get<0>(ptrs).get_const_accessor()};
    const auto case_accessor{std::get<1>(ptrs).get_const_accessor()};
    using column_pointer_type = typename decltype(activity_accessor)::type;
    process::variant_less<column_pointer_type> less{activity_accessor, filter};
    // TODO(j.kruska) CPL-7757 Determine an appropriate grain size here
    constexpr size_t grain_size{2 << 16};
    auto counter = count_group_occurrences(activity_accessor, case_accessor, filter, grain_size);
    // we ignore empty traces (analogously to the inductive miner), so remove the empty trace from the counter
    counter.erase(process::variant_view{});
    if (counter.empty()) {
      return {legacy_embedded_ctl::shared_static_array<row_id>{}, legacy_embedded_ctl::shared_static_array<row_id>()};
    }
    // create ranking of variants
    auto variant_ranking{legacy_embedded_ctl::make_static_array_for_overwrite<std::pair<process::variant_view, row_id>>(
        counter.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))};
    std::copy(begin(counter), end(counter), begin(variant_ranking));
    const auto count_greater{[&](const auto& lhs, const auto& rhs) {
      return lhs.second > rhs.second || (lhs.second == rhs.second && less(lhs.first, rhs.first));
    }};
    std::sort(begin(variant_ranking), end(variant_ranking), count_greater);
    const counter_ranking_of ranking_of{counter, variant_ranking, count_greater};
    // create ranking of cases
    auto ranking{
        legacy_embedded_ctl::make_shared_static_array_for_overwrite<row_id>(case_domain_count, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG))};
    common::for_each_group<size_t>(case_accessor, grain_size,
                                   [&](auto group) { ranking[case_accessor[group.begin()]] = ranking_of(group); });
    // create the accumulated counts
    auto accumulated_variant_counts{
        legacy_embedded_ctl::make_shared_static_array_for_overwrite<row_id>(variant_ranking.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG))};
    std::transform(begin(variant_ranking), end(variant_ranking), begin(accumulated_variant_counts),
                   [](const auto& p) { return p.second; });
    std::partial_sum(begin(accumulated_variant_counts), end(accumulated_variant_counts),
                     begin(accumulated_variant_counts));
    return {ranking, accumulated_variant_counts};
  }
};

void validate_sizes(const memory::column_t& activity_column, const memory::column_t& case_column,
                    const cube::filter_bitset_t& filter, const common::execution_context& context) {
  if (std::cmp_not_equal(activity_column->get_row_count(context), filter.size()) ||
      std::cmp_not_equal(case_column->get_row_count(context), filter.size())) {
    throw common::internal_exception{"Filter, activity column, and case column, don't match in size"};
  }
}

}  // namespace

incremental_eventlog incremental_eventlog::frequency_decreasing(const memory::column_t& activity_column,
                                                                memory::column_t case_column,
                                                                cube::filter_bitset_t filter,
                                                                const common::execution_context& context) {
  const auto case_domain_count{case_column->get_domain_count(context)};
  const auto sub_ctx{context.create_sub_context("frequency_decreasing", {{"case_domain_count", case_domain_count}})};
  validate_sizes(activity_column, case_column, filter, sub_ctx);
  // combine filter with the activity null bitset
  activity_column->project_null_flags(filter, sub_ctx);
  auto [ranking, counts]{memory::cast_execute_column_pointers(exec_compute_ranking{case_domain_count, filter},
                                                              activity_column->get_column_pointers(sub_ctx),
                                                              case_column->get_column_pointers(sub_ctx))};
  return {std::move(case_column), std::move(filter), std::move(ranking), std::move(counts)};
}

incremental_eventlog::retained_objects incremental_eventlog::retain_most_frequent(
    row_id num_variants, const common::execution_context& context) const {
  const auto sub_ctx{context.create_sub_context("retain_most_frequent", {{"number_of_variants", num_variants}})};
  if (num_variants <= 0 || variant_count() == 0) {
    return retained_objects{cube::filter_bitset_t(initial_filter_.size(), true), 0};
  }
  // from here on, we may assume that we retain at least one object
  num_variants = std::min(variant_count(), num_variants);
  return memory::cast_execute_column_pointers(
      [this, num_variants](auto tup) {
        const auto case_accessor{std::get<0>(tup).get_const_accessor()};
        auto result_filter{initial_filter_};
        for (size_t i{0}; i != result_filter.size(); ++i) {
          if (num_variants <= ranking_[case_accessor[i]] && !initial_filter_.test(i)) {
            result_filter.set(i);
          }
        }
        return retained_objects{result_filter, accumulated_variant_counts_[num_variants - 1]};
      },
      case_column_->get_column_pointers(sub_ctx));
}

std::pair<incremental_eventlog, size_t> incremental_eventlog::activity_cover(const std::vector<row_id>& activity_ids,
                                                                             const memory::column_t& activity_column,
                                                                             memory::column_t case_column,
                                                                             cube::filter_bitset_t filter,
                                                                             const common::execution_context& context) {
  validate_sizes(activity_column, case_column, filter, context);
  // combine filter with the activity null bitset
  activity_column->project_null_flags(filter, context);
  auto [ranking, counts, cover_size]{memory::cast_execute_column_pointers(
      exec_compute_counter{case_column->get_domain_count(context), filter, activity_ids, context},
      activity_column->get_column_pointers(context), case_column->get_column_pointers(context))};
  return {{std::move(case_column), std::move(filter), std::move(ranking), std::move(counts)}, cover_size};
}

row_id incremental_eventlog::retain_percentage_of_objects(double ratio) const {
  if (ratio <= 0) {
    return legacy_embedded_ctl::cast<row_id>(1);
  }
  if (ratio >= 1) {
    return legacy_embedded_ctl::cast<row_id>(variant_count());
  }

  const auto target_count = static_cast<double>(object_count()) * ratio;
  // lower_bound always returns a value within the range because target count is < 1 because of the initial check
  // This is why the +1 is safe and we do not have to handle lower bound returning the past-the-end iterator
  const auto num_retained_variants{legacy_embedded_ctl::cast<row_id>(std::distance(
      begin(accumulated_variant_counts_), std::ranges::lower_bound(accumulated_variant_counts_, target_count) + 1))};

  return std::max(legacy_embedded_ctl::cast<row_id>(1), num_retained_variants);
}

}  // namespace celonis::accelerator::operators::mo
