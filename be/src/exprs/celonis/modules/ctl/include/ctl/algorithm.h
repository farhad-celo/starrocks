#pragma once

#include <concepts>
#include <string_view>

#include "ctl/bits/algorithm_utils.h"

namespace celonis::accelerator::ctl {

/**
 * @brief Returns whether the given iterator range contains the given value
 */
template <typename ITERATOR_T, typename VALUE_T>
[[nodiscard]] inline bool contains(ITERATOR_T first, ITERATOR_T last, const VALUE_T& value) {
  return details::contains_range_impl(first, last, value);
}

/**
 * @brief Returns whether the given container contains the given value
 */
template <typename CONTAINER_T, typename VALUE_T>
[[nodiscard]] inline bool contains(const CONTAINER_T& container, const VALUE_T& value) {
  return details::contains_impl(container, value, details::overload_priority_entry{});
}

/** Contains overload for string types */
// TODO(n.weber): Replace with C++23 std::string_view::contains
[[nodiscard]] inline bool str_contains_sub(const std::string_view string, const std::string_view sub_string) {
  return string.find(sub_string) != std::string_view::npos;
}

}  // namespace celonis::accelerator::ctl
