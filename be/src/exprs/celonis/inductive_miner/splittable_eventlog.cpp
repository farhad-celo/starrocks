#include "splittable_eventlog.h"

#include <algorithm>
#include <memory>
#include <ranges>
#ifdef CELOSTAR
#include <span>
#endif
#include <variant>

#include <tbb/concurrent_unordered_set.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#ifndef CELOSTAR
#include "ctl/array_view.h"
#endif
#include "ctl/assert.h"
#include "ctl/utility.h"
#ifdef CELOSTAR
#include "inductive_miner/parallel_stable_integer_sort_copy.h"
#endif
#include "modules/common/case_aligned_range.h"
#include "modules/common/for_each_group.h"
#ifndef CELOSTAR
#include "modules/cube/filter_bitset.h"
#include "modules/memory/cache/variant_trace_cache.h"
#include "modules/memory/column.h"
#endif

namespace celonis::accelerator::operators::process {

#ifndef CELOSTAR
namespace {

template <typename T>
struct as_type_identity {
  using type = std::type_identity<T>;
};

using variant_of_col_ptr_type_holder = typename ctl::repack_types<
    std::variant,
    typename ctl::transform_types<
        ctl::type_sequence<std::type_identity<memory::col_ptr_8_t>, std::type_identity<memory::col_ptr_16_t>,
                           std::type_identity<memory::col_ptr_32_t>, std::type_identity<memory::col_ptr_64_t>>,
        as_type_identity>::type>::type;

variant_of_col_ptr_type_holder smallest_large_enough_type(size_t size) {
  if (size <= std::numeric_limits<memory::col_ptr_8_t>::max()) {
    return {std::type_identity<memory::col_ptr_8_t>{}};
  }
  if (size <= std::numeric_limits<memory::col_ptr_16_t>::max()) {
    return {std::type_identity<memory::col_ptr_16_t>{}};
  }
  if (size <= std::numeric_limits<memory::col_ptr_32_t>::max()) {
    return {std::type_identity<memory::col_ptr_32_t>{}};
  }
  return {std::type_identity<memory::col_ptr_64_t>{}};
}

template <typename ACTIVITY_ID_T, typename TRACE_ID_T>
using eventlog_extraction_container_t = std::vector<activity_id_and_trace_id_t<ACTIVITY_ID_T, TRACE_ID_T>>;

template <typename ACTIVITY_ID_T, typename TRACE_ID_T>
using parallel_eventlog_extraction_container_t =
    tbb::enumerable_thread_specific<eventlog_extraction_container_t<ACTIVITY_ID_T, TRACE_ID_T>>;

template <typename...>
struct eventlog_container_element;

template <typename ID_T>
struct eventlog_container_element<activity_id_and_trace_id<ID_T>> {
  using type = activity_id_and_trace_id<ID_T>;
};

template <typename T, typename A>
struct eventlog_container_element<std::vector<T, A>> : eventlog_container_element<T> {};

template <typename T>
struct eventlog_container_element<tbb::enumerable_thread_specific<T>> : eventlog_container_element<T> {};

template <typename... Ts>
using eventlog_container_element_t = typename eventlog_container_element<Ts...>::type;

template <ctl::nested_iterable NESTED_ITERABLE>
requires ctl::contiguous_sized_range<std::ranges::range_value_t<NESTED_ITERABLE>>
[[nodiscard]] eventlog_buffer_t transform_to_eventlog_buffer(const NESTED_ITERABLE& eventlog_extraction_containers,
                                                             const common::execution_context& ctx) {
  const auto size{std::accumulate(std::cbegin(eventlog_extraction_containers),
                                  std::cend(eventlog_extraction_containers), size_t{0},
                                  [](const size_t current_size, const auto& inner_container) {
                                    return current_size + std::size(inner_container);
                                  })};

  using eventlog_element_t = eventlog_container_element_t<NESTED_ITERABLE>;

  auto result_buffer{memory::tracking::make_shared_static_array_for_overwrite<eventlog_element_t>(
      size, ALLOC_MSG(ctl::RETURN_VALUE_MSG), ctx)};

  auto result_buffer_output_iter{result_buffer.begin()};
  for (const auto& eventlog_extraction_container : eventlog_extraction_containers) {
    debug_assert((eventlog_extraction_container.empty() || result_buffer_output_iter < result_buffer.end()));
    result_buffer_output_iter = std::ranges::copy(eventlog_extraction_container, result_buffer_output_iter).out;
  }

  debug_assert(result_buffer_output_iter == result_buffer.end());
  return {result_buffer};
}

/**
 * @brief Extracts the activities and their corresponding case and materializes them into an eventlog like structure
 * consisting of an array of activity ID / case ID pairs
 */
[[nodiscard]] eventlog_buffer_t extract_eventlog_buffer(const memory::column_t& activities,
                                                        const memory::column_t& cases,
                                                        const cube::filter_bitset_t& eventlog_selections,
                                                        const common::execution_context& context, size_t grain_size) {
  return memory::cast_execute_column_pointers(
      [&eventlog_selections, &cases, &context, grain_size]<typename TUP>(TUP&& tup) -> eventlog_buffer_t {
        using activity_index_type = typename std::decay_t<std::tuple_element_t<0, TUP>>::value_type;
        using case_index_type = typename std::decay_t<std::tuple_element_t<1, TUP>>::value_type;

        using parallel_container_t = parallel_eventlog_extraction_container_t<activity_index_type, case_index_type>;
        parallel_container_t locals{};
        tbb::parallel_for(common::case_aligned_range{cases, context, grain_size},
                          [&locals, &eventlog_selections, activity_ac = std::get<0>(tup).get_const_accessor(),
                           case_ac = std::get<1>(tup).get_const_accessor()](const auto& range) {
                            auto& local_data{locals.local()};
                            for (auto i{range.begin}; i < range.end; ++i) {
                              if (eventlog_selections.test(i) && activity_ac[i] != 0 && case_ac[i] != 0) {
                                local_data.emplace_back(activity_ac[i], case_ac[i]);
                              }
                            }
                          });
        return transform_to_eventlog_buffer(locals, context);
      },
      activities->get_column_pointers(context), cases->get_column_pointers(context));
}

class thread_safe_max_activity_domain_count {
 public:
  thread_safe_max_activity_domain_count() = default;
  [[nodiscard]] activity_domain_count_t get_with_null() const {  // NB: This call is not thread safe
    const auto it{std::ranges::max_element(thread_local_max_activity_domain_counts_)};
    return activity_domain_count_t{(it != std::cend(thread_local_max_activity_domain_counts_) ? *it : 0) + 1};
  }
  void update(const activity_id_t current_id) noexcept {
    row_id& local{thread_local_max_activity_domain_counts_.local()};
    local = std::max(current_id.get(), local);
  }

 private:
  tbb::enumerable_thread_specific<row_id> thread_local_max_activity_domain_counts_{};
};

}  // namespace
#endif

splittable_eventlog splittable_eventlog::extract(const splittable_eventlog_config_t& extraction_config,
                                                 const common::execution_context& context) {
  return std::visit([&context](const auto& config) { return extract(config, context); }, extraction_config);
}

#ifndef CELOSTAR
splittable_eventlog splittable_eventlog::canonicalize_if_necessary(splittable_eventlog&& other, size_t extra_space,
                                                                   const common::execution_context& context) {
  const auto max_representable{std::visit(
      []<typename VIEW>([[maybe_unused]] const VIEW& v) -> size_t {
        using id_type = typename eventlog_view_element_t<VIEW>::value_type;
        return std::numeric_limits<id_type>::max();
      },
      other.current_view_)};

  if (other.trace_domain_count() + extra_space > max_representable) {
    // we need to compress the ids, and maybe copy to a new buffer with a bigger case type
    other.canonicalize_case_ids();
    if (other.trace_domain_count() + extra_space > max_representable) {
      // we can't fit everything, so we need a bigger copy
      return std::visit([&context, &other]<typename T>(std::type_identity<T> /**/) { return other.copy<T>(context); },
                        smallest_large_enough_type(other.trace_domain_count() + extra_space));
    }
  }
  return std::move(other);
}
#endif

std::vector<splittable_eventlog> splittable_eventlog::split(const split_mapping_t& mapping) {
  return std::visit(
      [&mapping, this]<typename VIEW>(VIEW& view) {
        // rearrange so that we are grouped according to the mapping.
        const auto mapping_less{[&mapping](const auto& lhs, const auto& rhs) {
          return mapping[lhs.activity_id()] < mapping[rhs.activity_id()];
        }};
        using value_type = std::decay_t<decltype(view.front())>;
#ifdef CELOSTAR
        auto temp = std::vector<value_type>(view.size());
#else
        auto temp{ctl::make_static_array_for_overwrite<value_type>(view.size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};
#endif
        std::ranges::copy(view, temp.begin());
        parallel_stable_integer_sort_copy(begin(temp), end(temp), begin(view), 1 << 16,
                                          ctl::cast<row_id>(std::ranges::max(mapping)),
                                          [&mapping](const auto& p) { return mapping[p.activity_id()]; });
        temp.clear();
        // extract the groups
        std::vector<splittable_eventlog> result{};
        while (!view.empty()) {
          const auto partition_predicate{std::not_fn(std::bind_front(mapping_less, view.front()))};
          VIEW sub_view{view.begin(), std::ranges::partition_point(view, partition_predicate)};
          // result.emplace_back(activity_domain_count_, eventlog_, sub_eventlog_t{sub_view}) triggers a GCC bug
          result.emplace_back(
              splittable_eventlog{activity_domain_count(), trace_domain_count(), eventlog_, {sub_view}});
          debug_assert(eventlog_ == result.back().eventlog_);
#ifdef CELOSTAR
          view = view.subspan(sub_view.size());
#else
          view = view.sub_view(sub_view.size());
#endif
        }
        return result;
      },
      current_view_);
}

#ifndef CELOSTAR
void splittable_eventlog::canonicalize_case_ids() {
  std::visit(
      [this]<typename VIEW>(const VIEW& v) {
        if (v.empty()) {
          return;
        }
        using case_id_type = typename eventlog_view_element_t<VIEW>::trace_id_raw_type;
        case_id_type current_id{};
        // TODO(a.swoboda) consider parallelizing this
        common::for_each_group(element<PICK_CASE_ID>(v), [v, &current_id](auto interval) {
          const auto from{std::next(v.begin(), interval.begin())};
          const auto to{std::next(from, interval.size())};
          std::for_each(from, to, [current_id](auto& p) { p.second = current_id; });
          ++current_id;
        });
        trace_domain_count_ = trace_domain_count_t{ctl::cast<row_id>(current_id)};
      },
      current_view_);
}

// TODO(n.weber): Investigate whether an upsizing is really necessary. I think we can ignore 1B/2B IDs entirely and by
//  default always use 4B (for small eventlogs the memory overhead is also small). Also, we might want to investigate
//  whether to even use 8B by default. This would certainly increase the memory overhead of eventlogs of size < 2bn
//  but would simplify the logic at several places significantly and might improve performance due to less need for
//  upsizing/copying the EL buffer.
template <
    ctl::one_of<memory::col_ptr_8_t, memory::col_ptr_16_t, memory::col_ptr_32_t, memory::col_ptr_64_t> CASE_ID_TYPE>
splittable_eventlog splittable_eventlog::copy(const common::execution_context& context) const {
  auto buffer{std::visit(
      [&context](auto view) -> eventlog_buffer_t {
        using pair_type = activity_id_and_trace_id<CASE_ID_TYPE>;
        auto result_buffer{memory::tracking::make_static_array_for_overwrite<pair_type>(
            view.size(), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG), context)};
        std::ranges::transform(view, result_buffer.begin(), [](auto p) {
          return pair_type{p.activity_id(), p.trace_id()};
        });
        return {std::move(result_buffer)};
      },
      current_view_)};
  return {activity_domain_count(), trace_domain_count(), {std::move(buffer)}};
}
#endif

splittable_eventlog::splittable_eventlog(const activity_domain_count_t activity_domain_count_value,
                                         const trace_domain_count_t case_domain_count, eventlog_buffer_t eventlog,
                                         std::optional<eventlog_view_t> optional_current_view)
    : activity_domain_count_{activity_domain_count_value},
      trace_domain_count_{case_domain_count},
      eventlog_{std::move(eventlog)},
      current_view_{optional_current_view.has_value()
                        ? *optional_current_view
#ifdef CELOSTAR
                        : std::visit([](auto&& e) -> eventlog_view_t { return std::span{e.buffer.get(), e.size}; }, eventlog_)} {}

#else
                        : std::visit([](auto&& e) -> eventlog_view_t { return e; }, eventlog_)} {}

#endif
#ifdef CELOSTAR
splittable_eventlog splittable_eventlog::extract(const splittable_eventlog_config_for_using_variant_map& config,
                                                 const common::execution_context& context) {
  const auto& [variant_map, grain_size]{config};
  const auto variant_count{static_cast<trace_domain_count_t>(variant_map.size())};
  size_t size = 0;
  for (const auto& [variant, count] : variant_map) {
    size += variant.data.size();
  }

  // TODO(j.kim): Check if we need to use memory management.
  shared_static_array_of_activity_id_and_trace_id<row_id> result;
  result.buffer = std::shared_ptr<activity_id_and_trace_id<row_id>[]>(new activity_id_and_trace_id<row_id>[size]);
  result.size = size;

  // Stores the largest encountered activity ID
  row_id max_activity_domain_count = 0;

  // Fetch all variants, materialize its elements (i.e., activity IDs) together with the variant ID.
  int index = 0;
  int variant_index = 0;
  for (const auto& [variant, count] : variant_map) {
    for (const auto& activity_id : variant.data) {
      max_activity_domain_count = std::max(max_activity_domain_count, activity_id);
      result.buffer[index++] = {activity_id, variant_index};
    }
    variant_index++;
  }
  max_activity_domain_count++;  // One more for empty activity. TODO. Confirm.

  return splittable_eventlog{activity_domain_count_t{max_activity_domain_count}, variant_count, result};
}
#else
splittable_eventlog splittable_eventlog::extract(const splittable_eventlog_config_for_using_entire_eventlog& config,
                                                 const common::execution_context& context) {
  const auto& [activities, cases, grain_size, selections]{config};
  return splittable_eventlog{activity_domain_count_t{activities->get_domain_count(context)},
                             trace_domain_count_t{cases->get_domain_count(context)},
                             extract_eventlog_buffer(activities, cases, selections, context, grain_size)};
}

splittable_eventlog splittable_eventlog::extract(const splittable_eventlog_config_for_using_variants& config,
                                                 const common::execution_context& context) {
  const auto& [variant_trace_cache_ptr, grain_size]{config};
  const auto& variant_trace_cache{*variant_trace_cache_ptr};
  /* All the meta data and accessors from the variant trace cache */
  const auto variant_count{trace_domain_count_t{variant_trace_cache.get_num_traces()}};
  const auto variants_accessor{variant_trace_cache.get_traces(context)};
  const auto variant_lengths_accessor{variant_trace_cache.get_trace_lengths(context)};

  // View over the entire variant buffer. Used for out-of-bounds check verification.
  const ctl::array_view<const trace_element_type> trace_buffer_view{variants_accessor.buffer_begin(),
                                                                    variants_accessor.buffer_end()};

  // Small utility factory to create a view on a variant referenced by the given index
  const auto make_variant_view{[&](const row_id variant_idx) {
    ctl::array_view<const trace_element_type> variant_view{variants_accessor.at(variant_idx),
                                                           variant_lengths_accessor.at(variant_idx)};
    if (ctl::is_view_into_range(variant_view, trace_buffer_view)) {
      return variant_view;
    }
    throw common::internal_exception::with_context(
        {{"variant_ptr", ctl::ptr_to_int(variant_view.data())},
         {"variant_length", variant_view.size()},
         {"end_of_variant_buffer", ctl::ptr_to_int(trace_buffer_view.end())}},
        "Out of bounds access to variant elements at [{}].", ctl::source_location{});
  }};

  static_assert(std::numeric_limits<row_id>::max() >= std::numeric_limits<trace_element_type>::max(),
                "The activity IDs must be representable by row_id");

  using parallel_container_t =
      parallel_eventlog_extraction_container_t<activity_id_t::UnderlyingType, trace_element_type>;
  parallel_container_t thread_local_activity_variant_containers{};
  using eventlog_element_t = eventlog_container_element_t<parallel_container_t>;
  // Stores the largest encountered activity ID
  thread_safe_max_activity_domain_count max_activity_domain_count{};

  // Fetch all variants, materialize its elements (i.e., activity IDs) together with the variant ID.
  tbb::parallel_for(
      tbb::blocked_range<row_id>{0, variant_count, grain_size},
      [&thread_local_activity_variant_containers, &max_activity_domain_count,
       &make_variant_view = std::as_const(make_variant_view)](const auto& range) {
        auto& thread_local_activity_variant_container{thread_local_activity_variant_containers.local()};
        for (auto local_variant_idx{range.begin()}; local_variant_idx < range.end(); ++local_variant_idx) {
          const auto variant_view{make_variant_view(local_variant_idx)};
          std::ranges::transform(variant_view, std::back_inserter(thread_local_activity_variant_container),
                                 [&max_activity_domain_count, local_variant_idx](const auto variant_element) {
                                   const activity_id_t activity_id{variant_element};
                                   max_activity_domain_count.update(activity_id);
                                   return eventlog_element_t{activity_id, trace_id_t{local_variant_idx}};
                                 });
        }
      });

  auto materialized_eventlog{transform_to_eventlog_buffer(thread_local_activity_variant_containers, context)};

  return splittable_eventlog{max_activity_domain_count.get_with_null(), variant_count,
                             std::move(materialized_eventlog)};
}

template splittable_eventlog splittable_eventlog::copy<memory::col_ptr_8_t>(const common::execution_context&) const;
template splittable_eventlog splittable_eventlog::copy<memory::col_ptr_16_t>(const common::execution_context&) const;
template splittable_eventlog splittable_eventlog::copy<memory::col_ptr_32_t>(const common::execution_context&) const;
template splittable_eventlog splittable_eventlog::copy<memory::col_ptr_64_t>(const common::execution_context&) const;
#endif

}  // namespace celonis::accelerator::operators::process
