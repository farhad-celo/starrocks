#pragma once

#include <iosfwd>
#include <string>
#include <type_traits>

#include <fmt/format.h>

#include "legacy_embedded_ctl/exception.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief Simple closed interval type representing a begin and end point (where begin must not be larger than end)
 */
template <typename T>
class closed_interval final {
  static_assert(std::is_integral_v<T>, "Currently only integer types are supported.");  // TODO(n.weber): relax later on
 public:
  using value_type = T;
  using size_type = std::make_unsigned_t<value_type>;
  using const_reference = const value_type&;

  constexpr closed_interval() = default;
  /**
   * @brief Constructs a closed interval out of the given begin and end points.
   * @throws invalid_interval if the condition begin <= end does not hold
   */
  constexpr closed_interval(const_reference begin, const_reference end);

  [[nodiscard]] constexpr const_reference begin() const noexcept { return begin_; }
  [[nodiscard]] constexpr const_reference end() const noexcept { return end_; }
  [[nodiscard]] constexpr size_type size() const noexcept { return end() - begin() + 1; }
  [[nodiscard]] std::string to_string() const;

  friend std::ostream& operator<<(std::ostream& out, const closed_interval& interval) {
    return out << interval.to_string();
  }

  [[nodiscard]] friend bool operator==(const closed_interval& lhs, const closed_interval& rhs) noexcept = default;

 private:
  value_type begin_{};
  value_type end_{};
};

template <typename T>
constexpr closed_interval<T>::closed_interval(const_reference begin, const_reference end) : begin_{begin}, end_{end} {
  if (begin > end) {
    throw invalid_interval{"Invalid interval {}: The begin of any interval must not be larger than its end.",
                           to_string()};
  }
}

template <typename T>
inline std::string closed_interval<T>::to_string() const {
  return fmt::format("[{}, {}]", begin(), end());
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
