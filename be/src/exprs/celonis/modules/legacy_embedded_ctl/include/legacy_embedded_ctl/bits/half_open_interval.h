#pragma once

#include <algorithm>
#include <iosfwd>
#include <string>
#include <type_traits>

#include <fmt/format.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/exception.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief Simple half-open interval type representing a begin and end point (where begin must not be larger than end)
 */
template <typename T>
class half_open_interval final {
  static_assert(std::is_integral_v<T>, "Currently only integer types are supported.");  // TODO(n.weber): relax later on
 public:
  using value_type = T;
  using const_reference = const value_type&;

  /**
   * @brief Constructs an empty half-open interval.
   */
  constexpr half_open_interval() = default;
  /**
   * @brief Constructs a half-open interval out of the given begin and end points.
   * @throws invalid_interval if the condition begin <= end does not hold
   */
  constexpr half_open_interval(const_reference begin, const_reference end);

  [[nodiscard]] constexpr const_reference begin() const noexcept { return begin_; }
  [[nodiscard]] constexpr const_reference end() const noexcept { return end_; }
  [[nodiscard]] constexpr bool empty() const noexcept(noexcept(std::declval<T>() >= std::declval<T>()));
  [[nodiscard]] constexpr auto size() const noexcept(noexcept(std::declval<T>() - std::declval<T>()));
  [[nodiscard]] std::string to_string() const;
  /**
   * @brief Constructs a new sub interval with new_begin = begin + min(size(), offset)
   */
  [[nodiscard]] constexpr half_open_interval<value_type> move_begin(const_reference offset) const noexcept;
  /**
   * @brief Constructs a new sub interval with new_end = end - min(size(), offset)
   */
  [[nodiscard]] constexpr half_open_interval<value_type> move_end(const_reference offset) const noexcept;
  /**
   * @brief Constructs a new sub interval [min{begin(), upper_bound}, min{end(), upper_bound}).
   */
  [[nodiscard]] constexpr half_open_interval trim_to_upper_bound(T upper_bound) const noexcept;

  [[nodiscard]] friend constexpr const_reference begin(const half_open_interval& i) { return i.begin(); }
  [[nodiscard]] friend constexpr const_reference begin(half_open_interval& i) { return i.begin(); }
  [[nodiscard]] friend constexpr const_reference end(const half_open_interval& i) { return i.end(); }
  [[nodiscard]] friend constexpr const_reference end(half_open_interval& i) { return i.end(); }

  [[nodiscard]] friend bool operator==(const half_open_interval& lhs, const half_open_interval& rhs) noexcept = default;

  friend std::ostream& operator<<(std::ostream& out, const half_open_interval& interval) {
    return out << interval.to_string();
  }

 private:
  struct no_bounds_check {};

  /// Private constructor which doesn't perform bounds-checking
  constexpr half_open_interval(no_bounds_check no_check, const_reference begin, const_reference end) noexcept;

  value_type begin_{};
  value_type end_{};
};

/**
 * @brief Factory to create an empty half-open interval
 */
template <typename T>
constexpr half_open_interval<T> make_empty_half_open_interval() {
  return half_open_interval<T>{T{}, T{}};
}

template <typename T>
constexpr half_open_interval<T>::half_open_interval(const_reference begin, const_reference end)
    : begin_{begin}, end_{end} {
  if (begin > end) {
    throw invalid_interval{"Invalid interval {}: The begin of any interval must not be larger than its end.",
                           to_string()};
  }
}

template <typename T>
constexpr bool half_open_interval<T>::empty() const noexcept(noexcept(std::declval<T>() >= std::declval<T>())) {
  legacy_embedded_debug_assert(begin() <= end());
  return begin() >= end();
}

template <typename T>
constexpr auto half_open_interval<T>::size() const noexcept(noexcept(std::declval<T>() - std::declval<T>())) {
  legacy_embedded_debug_assert(begin() <= end());
  return end() - begin();
}

template <typename T>
inline std::string half_open_interval<T>::to_string() const {
  return fmt::format("[{}, {})", begin(), end());
}

template <typename T>
constexpr half_open_interval<T> half_open_interval<T>::move_begin(const_reference offset) const noexcept {
  return {no_bounds_check{}, begin() + std::min(offset, size()), end()};
}

template <typename T>
constexpr half_open_interval<T> half_open_interval<T>::move_end(const_reference offset) const noexcept {
  return {no_bounds_check{}, begin(), end() - std::min(offset, size())};
}

template <typename T>
constexpr half_open_interval<T> half_open_interval<T>::trim_to_upper_bound(T upper_bound) const noexcept {
  return {no_bounds_check{}, std::min(begin(), upper_bound), std::min(end(), upper_bound)};
}

template <typename T>
constexpr half_open_interval<T>::half_open_interval(half_open_interval<T>::no_bounds_check /*no_check*/,
                                                    const_reference begin, const_reference end) noexcept
    : begin_{begin}, end_{end} {
  legacy_embedded_debug_assert(
      begin <= end, "Invalid interval {}: The begin of any interval must not be larger than its end.", to_string());
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
