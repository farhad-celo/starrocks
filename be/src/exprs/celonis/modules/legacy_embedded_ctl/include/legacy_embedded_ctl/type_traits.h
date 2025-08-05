#pragma once

#include <string>
#include <tuple>
#include <type_traits>

#include "legacy_embedded_ctl/bits/type_traits_utils.h"

/**
 * @brief a few useful type traits not (yet) in the C++ standard library
 */
namespace celonis::accelerator::legacy_embedded_ctl {

template <typename E>
constexpr auto enum_to_underlying_type(E enumerator) noexcept {
  static_assert(std::is_enum_v<E>, "Only enumeration type allowed");
  return static_cast<std::underlying_type_t<E>>(enumerator);
}

template <typename T>
struct remove_all_cv : std::remove_cv<T> {};

template <typename T>
struct remove_all_cv<T*> {
  using type = typename remove_all_cv<T>::type*;
};

template <typename T>
struct remove_all_cv<T* const> {
  using type = typename remove_all_cv<T>::type*;
};

/**
 * @brief removes all const-volatile qualifiers from a type
 * @note The difference to std::remove_cv_t is that std::remove_cv_t<const T* const> just removes the constness from the
 * pointer but not from the pointed to type.
 * @example std::remove_cv_t<const T* const> -> const T* but legacy_embedded_ctl::remove_all_cv_t<const T* const> -> T*
 */
template <typename T>
using remove_all_cv_t = typename remove_all_cv<T>::type;

template <typename T>
struct remove_all_cvr {
  using type = remove_all_cv_t<std::remove_reference_t<T>>;
};

/**
 * @brief removes all const-volatile and reference qualifiers from a type
 */
template <typename T>
using remove_all_cvr_t = typename remove_all_cvr<T>::type;

template <typename T1, typename T2>
struct is_same_ignore_cvr {
  static constexpr bool value{std::is_same_v<remove_all_cvr<T1>, remove_all_cvr<T2>>};
};

template <typename T1, typename T2>
inline constexpr bool is_same_ignore_cvr_v{is_same_ignore_cvr<T1, T2>::value};

template <class>
[[maybe_unused]] inline constexpr bool always_false_v = false;

/**
 * @brief provides a similar functionality like std::is_base_of but for templated base classes. If
 * DERIVED_TEMPLATE_IMPL is an implementation of BASE_TEMPLATE, provides the member constant value equal to true.
 * Otherwise value is false.
 * @note Limitations of this implementation: 1) Does not work when the template arguments of BASE_TEMPLATE contain
 * non-type template parameters like the size value in std::array. 2) Does not work with multiple inheritance or private
 * inheritance from BASE_TEMPLATE.
 */
template <template <typename...> class BASE_TEMPLATE, typename DERIVED_TEMPLATE_IMPL>
using is_base_of_template =
    decltype(details::is_base_of_template_impl<BASE_TEMPLATE>(std::declval<DERIVED_TEMPLATE_IMPL*>()));

/**
 * @brief Helper variable template for is_base_of_template
 */
template <template <typename...> class BASE_TEMPLATE, typename DERIVED_TEMPLATE_IMPL>
inline constexpr bool is_base_of_template_v = is_base_of_template<BASE_TEMPLATE, DERIVED_TEMPLATE_IMPL>::value;

template <typename T>
struct is_cstring {
  using T_ = std::decay_t<remove_all_cvr_t<T>>;
  static constexpr bool value{std::is_same_v<char*, T_>};
};

template <typename T>
inline constexpr bool is_cstring_v{is_cstring<T>::value};

template <typename T>
struct is_string {
  using T_ = std::decay_t<remove_all_cvr_t<T>>;
  // TODO(n.weber): If there is the need, add additional checks as for char[], std::string_view, etc.
  static constexpr bool value{std::disjunction_v<is_cstring<T>, std::is_same<std::string, T_>>};
};

template <typename T>
inline constexpr bool is_string_v{is_string<T>::value};

template <typename F, typename... T>
using is_one_of = std::disjunction<std::is_same<F, T>...>;

template <typename F, typename... T>
inline constexpr bool is_one_of_v{is_one_of<F, T...>::value};

template <typename T>
using is_signed_integer =
    // NOLINTNEXTLINE(google-runtime-int)
    is_one_of<std::remove_cvref_t<T>, signed char, signed short, signed int, signed long, signed long long>;

template <typename T>
inline constexpr bool is_signed_integer_v{is_signed_integer<T>::value};

template <typename T>
using is_unsigned_integer =
    // NOLINTNEXTLINE(google-runtime-int)
    is_one_of<std::remove_cvref_t<T>, unsigned char, unsigned short, unsigned int, unsigned long, unsigned long long>;

template <typename T>
inline constexpr bool is_unsigned_integer_v{is_unsigned_integer<T>::value};

template <typename T>
using is_standard_integer = std::disjunction<is_signed_integer<T>, is_unsigned_integer<T>>;

template <typename T>
inline constexpr bool is_standard_integer_v{is_standard_integer<T>::value};

template <typename TO, typename FROM>
using is_array_convertible = std::is_convertible<FROM (*)[], TO (*)[]>;

template <typename TO, typename FROM>
inline constexpr bool is_array_convertible_v{is_array_convertible<TO, FROM>::value};

/**
 * If SOURCE is not a reference, return std::remove_reference_t<TARGET>.
 * Else, if SOURCE is an lvalue-reference, we add an lvalue-reference to TARGET.
 * Else, SOURCE is an rvalue-reference, in which case we remove any reference from TARGET and add an rvalue-reference.
 * In other words, we transform TARGET so that it is the same kind of reference as SOURCE (or not a reference)
 * @tparam SOURCE A type with the kind of reference we want to apply to target
 * @tparam TARGET The type that we want to apply the kind of reference to
 */
template <typename SOURCE, typename TARGET>
struct adopt_reference {
  using type = std::remove_reference_t<TARGET>;
};

template <typename SOURCE, typename TARGET>
struct adopt_reference<SOURCE&, TARGET> {
  using type = std::add_lvalue_reference_t<TARGET>;
};

template <typename SOURCE, typename TARGET>
struct adopt_reference<SOURCE&&, TARGET> {
  using type = std::add_rvalue_reference_t<std::remove_reference_t<TARGET>>;
};

template <typename SOURCE, typename TARGET>
using adopt_reference_t = typename adopt_reference<SOURCE, TARGET>::type;

/**
 * Alias a type that is identical to TARGET, except that it has the same const-ness as SOURCE
 * NB: A reference to const (e.g., const int&) is NOT const itself!
 * @tparam SOURCE A type with the kind of const qualification that we want to apply to target
 * @tparam TARGET The type that we want to apply the const qualifier to
 */
template <typename SOURCE, typename TARGET>
struct adopt_const {
  using type = std::remove_const_t<TARGET>;
};

template <typename SOURCE, typename TARGET>
struct adopt_const<const SOURCE, TARGET> {
  using type = std::add_const_t<TARGET>;
};

template <typename SOURCE, typename TARGET>
using adopt_const_t = typename adopt_const<SOURCE, TARGET>::type;

/**
 * Alias a type that is identical to TARGET, except for having identical volatile qualification as SOURCE
 * @tparam SOURCE A type with the kind of volatile qualification that we want to apply to target
 * @tparam TARGET The type that we want to apply the volatile qualifier to
 */
template <typename SOURCE, typename TARGET>
struct adopt_volatile {
  using type = std::remove_volatile_t<TARGET>;
};

template <typename SOURCE, typename TARGET>
struct adopt_volatile<volatile SOURCE, TARGET> {
  using type = std::add_volatile_t<TARGET>;
};

template <typename SOURCE, typename TARGET>
using adopt_volatile_t = typename adopt_volatile<SOURCE, TARGET>::type;

/**
 * Alias a type that is identical to TARGET, except that it has the same cv-qualifiers and reference type as SOURCE.
 * NB: This template will first remove any references from SOURCE and TARGET and compare cv-qualifiers on those types.
 * @tparam SOURCE A type with the kind of cv-qualifiers and reference that we want to apply to target
 * @tparam TARGET The type that we want to apply the cv-qualifiers and reference of SOURCE to
 */
template <typename SOURCE, typename TARGET>
struct adopt_cvr {
  using type = adopt_reference_t<
      SOURCE, adopt_const_t<std::remove_reference_t<SOURCE>,
                            adopt_volatile_t<std::remove_reference_t<SOURCE>, std::remove_reference_t<TARGET>>>>;
};

template <typename SOURCE, typename TARGET>
using adopt_cvr_t = typename adopt_cvr<SOURCE, TARGET>::type;

/**
 * Returns the N-th type of the given template parameter pack.
 */
template <size_t N, typename... TYPES>
using nth_type = typename std::tuple_element<N, std::tuple<TYPES...>>::type;

// No type: no member 'type' alias (failure)
template <typename...>
struct max_integer {};

// One type: T is result type
template <typename T>
requires is_standard_integer_v<T>
struct max_integer<T> : std::type_identity<std::remove_cvref_t<T>> {
};

// Two types: T with larger max integer value is result type
template <typename LHS_T, typename RHS_T>
requires is_standard_integer_v<LHS_T> && is_standard_integer_v<RHS_T>
struct max_integer<LHS_T, RHS_T> : std::conditional<std::numeric_limits<std::remove_reference_t<LHS_T>>::max() >=
                                                        std::numeric_limits<std::remove_reference_t<RHS_T>>::max(),
                                                    std::remove_cvref_t<LHS_T>, std::remove_cvref_t<RHS_T>> {
};

/* Three+ types: recursively calls itself until only two types are left. At this point the recursion stops and always
 * the larger T is returned as result type to the previous call. */
template <typename T, typename... Ts>
requires is_standard_integer_v<T> &&
    (is_standard_integer_v<Ts>&&...) struct max_integer<T, Ts...> : max_integer<T, typename max_integer<Ts...>::type> {
};

/**
 * @brief type trait which returns the 'largest' integer type of the types in the parameter pack given to it
 * @note 'largest' in this context means the integer type which can hold the largest/max (positive) value
 */
template <typename... Ts>
using max_integer_t = typename max_integer<Ts...>::type;

}  // namespace celonis::accelerator::legacy_embedded_ctl
