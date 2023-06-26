#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

namespace celonis::accelerator::ctl::details {

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

}  // namespace celonis::accelerator::ctl::details
