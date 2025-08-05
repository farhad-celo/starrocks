#pragma once

#include <concepts>
#include <ranges>

#include "legacy_embedded_ctl/type_traits.h"

namespace celonis::accelerator::legacy_embedded_ctl {

template <typename T>
concept standard_integer = is_standard_integer_v<T>;

template <typename T, typename... U>
concept one_of = (std::same_as<T, U> || ...);

template <typename T>
concept iterable = std::ranges::range<T>;

template <typename T>
concept nested_iterable = iterable<T> && iterable<std::ranges::range_value_t<T>>;

template <typename RANGE>
concept contiguous_sized_range = std::ranges::contiguous_range<RANGE> && std::ranges::sized_range<RANGE>;

template <typename TO, typename FROM>
concept array_convertible = is_array_convertible_v<TO, FROM>;

/** Type must provide a public accessible 'value_type' alias to satisfy this constraint */
template <typename T>
concept value_type_accessible = requires(T t) {
  {std::declval<typename T::value_type>()};
};

/** Container C must be a range with push_back support to satisfy this constraint */
template <typename C, typename V = typename C::value_type>
concept push_backable_range = std::ranges::range<C> && requires(C c, V v) {
  {c.push_back(v)};
  {c.push_back(std::move(v))};
};

/** Container C must be a range with insert support to satisfy this constraint */
template <typename C, typename V = typename C::value_type>
concept insertable_range = std::ranges::range<C> && requires(C c, V v) {
  {c.insert(std::end(c), v)};
  {c.insert(std::end(c), std::move(v))};
};

/** Container C must be a range with either push_back OR insert support to satisfy this constraint */
template <typename C, typename V = typename C::value_type>
concept push_backable_or_insertable_range = push_backable_range<C, V> || insertable_range<C, V>;

/** Container C must be a range with only insert (but no push_back) support to satisfy this constraint */
template <typename C, typename V = typename C::value_type>
concept only_insertable_range = insertable_range<C, V> && !push_backable_range<C, V>;

/** Same as std::regular_invocable but also verifies the return type */
template <typename F, typename R, typename... ARGS>
concept regular_invocable_r = std::regular_invocable<F, ARGS...> && std::is_invocable_r_v<R, F, ARGS...>;

/**
 * @brief Range R must be a map-like type to satisfy this constraint. R is considered to be map-like if:
 * - it has type alias 'key_type'
 * - it has type alias 'mapped_type'
 * - it has an 'operator[]' method which is accessed by values of 'key_type' and returns a value of type 'mapped_type'
 * - it has an 'at' method which is accessed by values of 'key_type' and returns a value of type 'mapped_type'
 */
template <typename R>
// N.B: Just 'R r' was still deduced as a reference in some cases
concept map_or_alike = requires(std::remove_reference_t<R> r) {
  // A map-like type needs a 'key_type' alias
  {std::declval<typename decltype(r)::key_type>()};
  // A map-like type needs a 'mapped_type' alias
  {std::declval<typename decltype(r)::mapped_type>()};
  // A map-like type needs an 'operator[]'-method invokable with a value of 'key_type'
  // N.B: For many map (like) types operator[] can only be invoked on non-const values
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  {const_cast<std::remove_const_t<decltype(r)>&>(r)[std::declval<const typename decltype(r)::key_type&>()]};
  // A map-like type needs an 'at'-method invokable with a value of 'key_type'
  {r.at(std::declval<const typename decltype(r)::key_type&>())};
  // A map-like type needs to return a 'mapped_type' value from 'operator[]'
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  std::same_as<typename decltype(r)::mapped_type, decltype(const_cast<std::remove_const_t<decltype(r)>&>(
                                                      r)[std::declval<const typename decltype(r)::key_type&>()])>;
  // A map-like type needs to return a 'mapped_type' value from 'at'
  std::same_as<typename decltype(r)::mapped_type,
               decltype(r.at(std::declval<const typename decltype(r)::key_type&>()))>;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
