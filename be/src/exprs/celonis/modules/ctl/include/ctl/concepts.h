#pragma once

#include <concepts>
#include <ranges>

#include "ctl/type_traits.h"

namespace celonis::accelerator::ctl {

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

}  // namespace celonis::accelerator::ctl
