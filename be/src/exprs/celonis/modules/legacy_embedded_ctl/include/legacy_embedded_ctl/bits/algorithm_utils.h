#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include "legacy_embedded_ctl/concepts.h"

namespace celonis::accelerator::legacy_embedded_ctl::details {

template <typename ITERATOR_T, typename VALUE_T>
[[nodiscard]] inline bool contains_range_impl(ITERATOR_T first, ITERATOR_T last, const VALUE_T& value) {
  // We could do some validation here whether the value type matches with the iterators etc but we will leave this to
  // std::find for now
  return std::find(first, last, value) != last;
}

/** Tags to resolve overload ambiguity */
template <std::size_t N>
struct overload_priority : overload_priority<N - 1> {};
template <>
struct overload_priority<0> {};
/** Base case with no overload priority */
using no_priority = overload_priority<0>;
using overload_priority_entry = overload_priority<3>;

/** Use container provided 'contains' if it exists */
template <typename CONTAINER_T, typename VALUE_T>
[[nodiscard]] inline auto contains_impl(const CONTAINER_T& container, const VALUE_T& value,
                                        overload_priority<2> /*unused*/) -> decltype(container.contains(value)) {
  return container.contains(value);
}

using std::cbegin;
using std::cend;

/** Use container provided 'find' if it exists */
template <typename CONTAINER_T, typename VALUE_T>
[[nodiscard]] inline auto contains_impl(const CONTAINER_T& container, const VALUE_T& value,
                                        overload_priority<1> /*unused*/)
    -> decltype(container.find(value) != cend(container)) {
  return container.find(value) != cend(container);
}

template <typename CONTAINER_T, typename VALUE_T>
[[nodiscard]] inline bool contains_impl(const CONTAINER_T& container, const VALUE_T& value, no_priority /*unused*/) {
  return contains_range_impl(cbegin(container), cend(container), value);
}

/** If the container has no push_back but insert support */
[[nodiscard]] auto make_inserter(only_insertable_range auto& container) {
  return std::inserter(container, container.end());
}

/** If the container has push_back support, we prefer this overload due to generally better performance */
[[nodiscard]] auto make_inserter(push_backable_range auto& container) { return std::back_inserter(container); }

template <typename R, typename T>
[[nodiscard]] inline auto get_iter_impl(R&& range, const T& value, overload_priority<1> /*unused*/)
    -> decltype(range.find(value)) {
  return range.find(value);
}

template <typename R, typename T>
[[nodiscard]] inline auto get_iter_impl(R&& range, const T& value, no_priority /*unused*/)
    -> decltype(std::ranges::find(std::forward<R>(range), value)) {
  return std::ranges::find(std::forward<R>(range), value);
}

template <typename R, typename T, typename F>
[[nodiscard]] auto get_impl(R&& range, const T& value, F iter_deref_functor) {
  // NOLINTNEXTLINE(readability-qualified-auto) -> we can't use auto* as many iterators are not just pointers
  auto result_iter{get_iter_impl(std::forward<R>(range), value, overload_priority_entry{})};
  static_assert(std::is_reference_v<std::invoke_result_t<F, decltype(result_iter)>>);
  using ref_t = std::reference_wrapper<std::remove_reference_t<std::invoke_result_t<F, decltype(result_iter)>>>;
  return result_iter != std::end(range) ? std::make_optional(ref_t{iter_deref_functor(result_iter)}) : std::nullopt;
}

// N.B: clang-format deactivated as the lambdas are formatted very weirdly and the end-of-namespace comment is injected
// clang-format off
template <map_or_alike R, typename T>
[[nodiscard]] auto get_impl(R&& range, const T& value) {
  using range_t = std::remove_reference_t<R>;
  const auto iterator_dereference_functor{
      // N.B: 'decltype(auto)' as trailing return type doesn't work due to returned pair members being deduced as value
      [](one_of<typename range_t::iterator, typename range_t::const_iterator> auto iter) -> auto& {
          return iter->second;
      }
  };
  return get_impl(std::forward<R>(range), value, iterator_dereference_functor);
}

template <typename R, typename T>
[[nodiscard]] auto get_impl(R&& range, const T& value) {
  using range_t = std::remove_reference_t<R>;
  const auto iterator_dereference_functor{
      [](one_of<typename range_t::iterator, typename range_t::const_iterator> auto iter) -> auto& {
          return *iter;
      }
  };
  return get_impl(std::forward<R>(range), value, iterator_dereference_functor);
}
// clang-format on

}  // namespace celonis::accelerator::legacy_embedded_ctl::details
