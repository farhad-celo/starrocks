#pragma once

#include <ranges>
#include <string_view>

#include "legacy_embedded_ctl/bits/algorithm_utils.h"
#include "legacy_embedded_ctl/concepts.h"
#include "legacy_embedded_ctl/exception.h"

namespace celonis::accelerator::legacy_embedded_ctl {

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

/**
 * @brief Similar to std::ranges::transform but returns a 'transformed to' output range
 * @tparam C the output range to transform to
 * @tparam R the input range to transform
 * @tparam F the transform functor
 * @tparam P an optional projection (projects values from the input range to an input value for the transform functor)
 * @return the allocated and transformed to output range
 */
template <typename C, std::ranges::sized_range R, typename F, typename P = std::identity>
// As known already, clang-format has issues with formatting requirements clauses
// clang-format off
/* Constraints */
requires(
    /* Projection P must be invocable with elements of range R */
    std::regular_invocable<P, typename std::remove_reference_t<R>::value_type>
    // TODO(n.weber): In theory, this next constraint is more restrictive than necessary. All 'transform_to' should be
    //  concerned about is that it can pass its transformed values to the output range (via push_back/insert as checked
    //  in the next constraint). The output range C is principally allowed to change the type of its input values
    //  internally and have a mismatch between its 'value_type' alias and its value type for inputs. However, because
    //  containers doing such internal transformations should be rare (and might even indicate a flaw in their design),
    //  we better be more restrictive here until a relaxation is really needed.
    /* Function F must transform the projected elements of range R to elements of the output range C */
    && regular_invocable_r<F, typename C::value_type, std::invoke_result_t<P, typename std::remove_reference_t<R>::value_type>>
    // TODO(n.weber): We could also relax the next constraint and not require the output to be a std::range. This would
    //  make it easier to write simple proxy types (not needing iterators) one can add items to (see also in the tests).
    /* C must be an insertable range which takes the values as returned by the transform function F as input */
    && push_backable_or_insertable_range<C, std::invoke_result_t<F, std::invoke_result_t<P, typename std::remove_reference_t<R>::value_type>>>
)
/* Actual signature */
[[nodiscard]] inline C transform_to(R&& source_range, F transform_functor, P projection = {}) {
  // clang-format on
  C result{};
  // Pre-allocate memory if possible
  if constexpr (requires { result.reserve(std::declval<std::size_t>()); }) {
    result.reserve(source_range.size());
  }
  std::ranges::transform(std::forward<R>(source_range), details::make_inserter(result), std::move(transform_functor),
                         std::move(projection));
  return result;
}

/**
 * @brief Searches and returns a given 'value' in a given 'range'.
 * @tparam R type of the input range
 * @tparam T type of the value to search
 * @param range the range where to find the value
 * @param value the value to find
 * @return an optional containing either a reference (wrapper) to the found value or nullopt
 */
template <typename R, typename T>
[[nodiscard]] inline auto get(R&& range, const T& value) {
  return details::get_impl(std::forward<R>(range), value);
}

/** @brief Same as above but throws if the value was not found. Also returns a 'true' reference (i.e., not a wrapper) */
template <typename R, typename T>
[[nodiscard]] inline decltype(auto) get_or_throw(R&& range, const T& value) {
  if (const auto get_result{get(std::forward<R>(range), value)}; get_result.has_value()) {
    return get_result.value().get();
  }
  throw out_of_range{"Value not found."};
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
