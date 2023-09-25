#pragma once

#include <cstring>
#include <type_traits>

#include "modules/common/shared_types_fwd.h"

/**
 * @brief In the following, various comparison operators for our internal types are implemented as callable structs.
 * The implementations provide a suitable overload for the comparison operators such as 'std::equal_to' or 'std::less'
 * for our internal types. Several template specializations are provided to do always 'the right' comparison.
 *
 * E.g., for the input of two 'cel_string_t' we do not use the build-in comparison operations (i.e., ==, <, ..) but use
 * std::strcmp instead to do character-string based comparisons.
 */
namespace celonis::accelerator {

/**
 * Unified comparator for scalar types and strings.
 *
 * Should be zero overhead abstraction, though, one should be careful.
 *
 * @tparam T Type of the value that should be compared. A specialization for T =
 * cel_string_t
 */
template <typename T>
class comparator {
 public:
  constexpr comparator(const T val1, const T val2) : val1(val1), val2(val2) {}

  [[nodiscard]] constexpr bool equals() const { return val1 == val2; }

  [[nodiscard]] constexpr bool smaller() const { return val1 < val2; }

  [[nodiscard]] constexpr bool greater() const { return val1 > val2; }

 private:
  T val1;
  T val2;
};

template <>
class comparator<cel_string_t> {
  static_assert(std::is_same_v<cel_string_t, const char*>);

 public:
  comparator(const char* val1, const char* val2) : cmp_rslt(std::strcmp(val1, val2)) {}

  [[nodiscard]] bool equals() const { return cmp_rslt == 0; }

  [[nodiscard]] bool smaller() const { return cmp_rslt < 0; }

  [[nodiscard]] bool greater() const { return cmp_rslt > 0; }

 private:
  int cmp_rslt;
};

template <typename LHS_T, typename RHS_T = LHS_T>
struct equal_to final {
  constexpr bool operator()(const LHS_T& lhs, const RHS_T& rhs) const
      noexcept(noexcept(std::declval<LHS_T>() == std::declval<RHS_T>())) {
    static_assert(std::is_same_v<LHS_T, RHS_T>, "The given types are not allowed to be used in the comparison.");
    static_assert(is_supported_type<LHS_T>(), "The type of the left-hand side argument is not supported.");
    static_assert(is_supported_type<RHS_T>(), "The type of the right-hand side argument is not supported.");
    return lhs == rhs;
  }
};

template <>
struct equal_to<cel_string_t, cel_string_t> {
  static_assert(std::is_same_v<cel_string_t, const char*>);
  // on gcc std::strcmp is noexcept although not required by the standard
  bool operator()(const char* lhs, const char* rhs) const
      noexcept(noexcept(std::strcmp(std::declval<const char*>(), std::declval<const char*>()))) {
    return std::strcmp(lhs, rhs) == 0;
  }
};

template <>
struct equal_to<cel_int_t, cel_float_t> final {
  constexpr bool operator()(const cel_int_t lhs, const cel_float_t rhs) const noexcept {
    return static_cast<cel_float_t>(lhs) == rhs;
  }
};

template <>
struct equal_to<cel_float_t, cel_int_t> final {
  constexpr bool operator()(const cel_float_t lhs, const cel_int_t rhs) const noexcept {
    return lhs == static_cast<cel_float_t>(rhs);
  }
};

template <typename LHS_T, typename RHS_T = LHS_T>
struct not_equal_to final {
  constexpr bool operator()(const LHS_T& lhs, const RHS_T& rhs) const
      noexcept(noexcept(std::declval<LHS_T>() != std::declval<RHS_T>())) {
    static_assert(std::is_same_v<LHS_T, RHS_T>, "The given types are not allowed to be used in the comparison.");
    static_assert(is_supported_type<LHS_T>(), "The type of the left-hand side argument is not supported.");
    static_assert(is_supported_type<RHS_T>(), "The type of the right-hand side argument is not supported.");
    return lhs != rhs;
  }
};

template <>
struct not_equal_to<cel_string_t, cel_string_t> {
  static_assert(std::is_same_v<cel_string_t, const char*>);
  // on gcc std::strcmp is noexcept although not required by the standard
  bool operator()(const char* lhs, const char* rhs) const
      noexcept(noexcept(std::strcmp(std::declval<const char*>(), std::declval<const char*>()))) {
    return std::strcmp(lhs, rhs) != 0;
  }
};

template <>
struct not_equal_to<cel_int_t, cel_float_t> final {
  constexpr bool operator()(const cel_int_t lhs, const cel_float_t rhs) const noexcept {
    return static_cast<cel_float_t>(lhs) != rhs;
  }
};

template <>
struct not_equal_to<cel_float_t, cel_int_t> final {
  constexpr bool operator()(const cel_float_t lhs, const cel_int_t rhs) const noexcept {
    return lhs != static_cast<cel_float_t>(rhs);
  }
};

template <typename LHS_T, typename RHS_T = LHS_T>
struct less final {
  constexpr bool operator()(const LHS_T& lhs, const RHS_T& rhs) const
      noexcept(noexcept(std::declval<LHS_T>() < std::declval<RHS_T>())) {
    static_assert(std::is_same_v<LHS_T, RHS_T>, "The given types are not allowed to be used in the comparison.");
    static_assert(is_supported_type<LHS_T>(), "The type of the left-hand side argument is not supported.");
    static_assert(is_supported_type<RHS_T>(), "The type of the right-hand side argument is not supported.");
    return lhs < rhs;
  }
};

template <>
struct less<cel_string_t, cel_string_t> {
  static_assert(std::is_same_v<cel_string_t, const char*>);
  // on gcc std::strcmp is noexcept although not required by the standard
  bool operator()(const char* lhs, const char* rhs) const
      noexcept(noexcept(std::strcmp(std::declval<const char*>(), std::declval<const char*>()))) {
    return std::strcmp(lhs, rhs) < 0;
  }
};

template <>
struct less<cel_int_t, cel_float_t> final {
  constexpr bool operator()(const cel_int_t lhs, const cel_float_t rhs) const noexcept {
    return static_cast<cel_float_t>(lhs) < rhs;
  }
};

template <>
struct less<cel_float_t, cel_int_t> final {
  constexpr bool operator()(const cel_float_t lhs, const cel_int_t rhs) const noexcept {
    return lhs < static_cast<cel_float_t>(rhs);
  }
};

template <typename LHS_T, typename RHS_T = LHS_T>
struct less_equal final {
  constexpr bool operator()(const LHS_T& lhs, const RHS_T& rhs) const
      noexcept(noexcept(std::declval<LHS_T>() <= std::declval<RHS_T>())) {
    static_assert(std::is_same_v<LHS_T, RHS_T>, "The given types are not allowed to be used in the comparison.");
    static_assert(is_supported_type<LHS_T>(), "The type of the left-hand side argument is not supported.");
    static_assert(is_supported_type<RHS_T>(), "The type of the right-hand side argument is not supported.");
    return lhs <= rhs;
  }
};

template <>
struct less_equal<cel_string_t, cel_string_t> {
  static_assert(std::is_same_v<cel_string_t, const char*>);
  // on gcc std::strcmp is noexcept although not required by the standard
  bool operator()(const char* lhs, const char* rhs) const
      noexcept(noexcept(std::strcmp(std::declval<const char*>(), std::declval<const char*>()))) {
    return std::strcmp(lhs, rhs) <= 0;
  }
};

template <>
struct less_equal<cel_int_t, cel_float_t> final {
  constexpr bool operator()(const cel_int_t lhs, const cel_float_t rhs) const noexcept {
    return static_cast<cel_float_t>(lhs) <= rhs;
  }
};

template <>
struct less_equal<cel_float_t, cel_int_t> final {
  constexpr bool operator()(const cel_float_t lhs, const cel_int_t rhs) const noexcept {
    return lhs <= static_cast<cel_float_t>(rhs);
  }
};

template <typename LHS_T, typename RHS_T = LHS_T>
struct greater final {
  constexpr bool operator()(const LHS_T& lhs, const RHS_T& rhs) const
      noexcept(noexcept(std::declval<LHS_T>() > std::declval<RHS_T>())) {
    static_assert(std::is_same_v<LHS_T, RHS_T>, "The given types are not allowed to be used in the comparison.");
    static_assert(is_supported_type<LHS_T>(), "The type of the left-hand side argument is not supported.");
    static_assert(is_supported_type<RHS_T>(), "The type of the right-hand side argument is not supported.");
    return lhs > rhs;
  }
};

template <>
struct greater<cel_string_t, cel_string_t> {
  static_assert(std::is_same_v<cel_string_t, const char*>);
  // on gcc std::strcmp is noexcept although not required by the standard
  bool operator()(const char* lhs, const char* rhs) const
      noexcept(noexcept(std::strcmp(std::declval<const char*>(), std::declval<const char*>()))) {
    return std::strcmp(lhs, rhs) > 0;
  }
};

template <>
struct greater<cel_int_t, cel_float_t> final {
  constexpr bool operator()(const cel_int_t lhs, const cel_float_t rhs) const noexcept {
    return static_cast<cel_float_t>(lhs) > rhs;
  }
};

template <>
struct greater<cel_float_t, cel_int_t> final {
  constexpr bool operator()(const cel_float_t lhs, const cel_int_t rhs) const noexcept {
    return lhs > static_cast<cel_float_t>(rhs);
  }
};

template <typename LHS_T, typename RHS_T = LHS_T>
struct greater_equal final {
  constexpr bool operator()(const LHS_T& lhs, const RHS_T& rhs) const
      noexcept(noexcept(std::declval<LHS_T>() >= std::declval<RHS_T>())) {
    static_assert(std::is_same_v<LHS_T, RHS_T>, "The given types are not allowed to be used in the comparison.");
    static_assert(is_supported_type<LHS_T>(), "The type of the left-hand side argument is not supported.");
    static_assert(is_supported_type<RHS_T>(), "The type of the right-hand side argument is not supported.");
    return lhs >= rhs;
  }
};

template <>
struct greater_equal<cel_string_t, cel_string_t> {
  static_assert(std::is_same_v<cel_string_t, const char*>);
  // on gcc std::strcmp is noexcept although not required by the standard
  bool operator()(const char* lhs, const char* rhs) const
      noexcept(noexcept(std::strcmp(std::declval<const char*>(), std::declval<const char*>()))) {
    return std::strcmp(lhs, rhs) >= 0;
  }
};

template <>
struct greater_equal<cel_int_t, cel_float_t> final {
  constexpr bool operator()(const cel_int_t lhs, const cel_float_t rhs) const noexcept {
    return static_cast<cel_float_t>(lhs) >= rhs;
  }
};

template <>
struct greater_equal<cel_float_t, cel_int_t> final {
  constexpr bool operator()(const cel_float_t lhs, const cel_int_t rhs) const noexcept {
    return lhs >= static_cast<cel_float_t>(rhs);
  }
};

template <typename LHS_T, typename MID_T = LHS_T, typename RHS_T = LHS_T>
struct between final {
  constexpr bool operator()(const LHS_T& lhs, const MID_T& mid, const RHS_T& rhs) const
      noexcept(noexcept(std::declval<LHS_T>() >= std::declval<MID_T>()) && noexcept(std::declval<LHS_T>() <=
                                                                                    std::declval<RHS_T>())) {
    static_assert(std::is_same_v<LHS_T, MID_T> && std::is_same_v<MID_T, RHS_T>,
                  "The given types are not allowed to be used in the comparison.");
    static_assert(is_supported_type<LHS_T>(), "The type of the left-hand side argument is not supported.");
    static_assert(is_supported_type<MID_T>(), "The type of the middle argument is not supported.");
    static_assert(is_supported_type<RHS_T>(), "The type of the right-hand side argument is not supported.");
    return (lhs >= mid) && (lhs <= rhs);
  }
};

template <>
struct between<cel_string_t, cel_string_t, cel_string_t> final {
  static_assert(std::is_same_v<cel_string_t, const char*>);
  bool operator()(const char* lhs, const char* mid, const char* rhs) const
      noexcept(noexcept(std::strcmp(std::declval<const char*>(), std::declval<const char*>()))) {
    return (std::strcmp(lhs, mid) >= 0) && (std::strcmp(lhs, rhs) <= 0);
  }
};

template <>
struct between<cel_int_t, cel_int_t, cel_float_t> final {
  constexpr bool operator()(const cel_int_t lhs, const cel_int_t mid, const cel_float_t rhs) const noexcept {
    const cel_float_t lhs_as_float = static_cast<cel_float_t>(lhs);
    const cel_float_t mid_as_float = static_cast<cel_float_t>(mid);
    return (lhs_as_float >= mid_as_float) && (lhs_as_float <= rhs);
  }
};

template <>
struct between<cel_int_t, cel_float_t, cel_int_t> final {
  constexpr bool operator()(const cel_int_t lhs, const cel_float_t mid, const cel_int_t rhs) const noexcept {
    const cel_float_t lhs_as_float = static_cast<cel_float_t>(lhs);
    const cel_float_t rhs_as_float = static_cast<cel_float_t>(rhs);
    return (lhs_as_float >= mid) && (lhs_as_float <= rhs_as_float);
  }
};

template <>
struct between<cel_int_t, cel_float_t, cel_float_t> final {
  constexpr bool operator()(const cel_int_t lhs, const cel_float_t mid, const cel_float_t rhs) const noexcept {
    const cel_float_t lhs_as_float = static_cast<cel_float_t>(lhs);
    return (lhs_as_float >= mid) && (lhs_as_float <= rhs);
  }
};

template <>
struct between<cel_float_t, cel_int_t, cel_int_t> final {
  constexpr bool operator()(const cel_float_t lhs, const cel_int_t mid, const cel_int_t rhs) const noexcept {
    const cel_float_t mid_as_float = static_cast<cel_float_t>(mid);
    const cel_float_t rhs_as_float = static_cast<cel_float_t>(rhs);
    return (lhs >= mid_as_float) && (lhs <= rhs_as_float);
  }
};

template <>
struct between<cel_float_t, cel_int_t, cel_float_t> final {
  constexpr bool operator()(const cel_float_t lhs, const cel_int_t mid, const cel_float_t rhs) const noexcept {
    const cel_float_t mid_as_float = static_cast<cel_float_t>(mid);
    return (lhs >= mid_as_float) && (lhs <= rhs);
  }
};

template <>
struct between<cel_float_t, cel_float_t, cel_int_t> final {
  constexpr bool operator()(const cel_float_t lhs, const cel_float_t mid, const cel_int_t rhs) const noexcept {
    const cel_float_t rhs_as_float = static_cast<cel_float_t>(rhs);
    return (lhs >= mid) && (lhs <= rhs_as_float);
  }
};

}  // namespace celonis::accelerator
