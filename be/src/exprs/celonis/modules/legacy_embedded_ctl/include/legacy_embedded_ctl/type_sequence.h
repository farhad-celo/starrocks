#pragma once

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * Empty type, holding a (possibly empty) sequence of types in its parameter list
 * @tparam TYPES types in the type sequence
 */
template <typename... TYPES>
struct type_sequence {};

/**
 * The coproduct of type sequences, or "concatenation" of type sequences
 * @tparam an arbitrary number of templates with type parameters only. The individual template's types are concatenated
 */
template <typename...>
struct types_coproduct;

template <>
struct types_coproduct<> {
  using type = type_sequence<>;
};

template <template <typename...> typename HOLDER, typename... TYPES>
struct types_coproduct<HOLDER<TYPES...>> {
  using type = type_sequence<TYPES...>;
};

template <template <typename...> typename HOLDER1, typename... TYPES1, template <typename...> typename HOLDER2,
          typename... TYPES2>
struct types_coproduct<HOLDER1<TYPES1...>, HOLDER2<TYPES2...>> {
  using type = type_sequence<TYPES1..., TYPES2...>;
};

template <typename FIRST, typename SECOND, typename THIRD, typename... REST>
struct types_coproduct<FIRST, SECOND, THIRD, REST...> {
  using type = typename types_coproduct<FIRST, typename types_coproduct<SECOND, THIRD, REST...>::type>::type;
};

/**
 * Unpack a variadic template (second argument), and pack its type parameters into a new template (first argument)
 */
template <template <typename...> typename, typename>
struct repack_types;

template <template <typename...> typename TEMPLATE, template <typename...> typename HOLDER, typename... TYPES>
struct repack_types<TEMPLATE, HOLDER<TYPES...>> {
  using type = TEMPLATE<TYPES...>;
};

template <template <typename...> typename TEMPLATE, typename T>
using repack_types_t = typename repack_types<TEMPLATE, T>::type;

/**
 * Given a template with type parameters only, apply a transformation (repack the type) to each type parameter
 */
template <typename, template <typename...> typename>
struct transform_types;

template <template <typename...> typename HOLDER, typename... TYPES, template <typename...> typename TRANSFORM>
struct transform_types<HOLDER<TYPES...>, TRANSFORM> {
  using type = type_sequence<typename repack_types_t<TRANSFORM, TYPES>::type...>;
};

/**
 * Partially apply a type parameter. Has inner template classes that prepend this type parameter to a sequence of types
 * @tparam T the type to partially apply
 */
template <typename T>
struct curried_type {
  /**
   * Apply the remaining types and wrap it in a sequence of types
   * @tparam REST
   */
  template <typename... REST>
  struct pack {
    using type = typename types_coproduct<type_sequence<T>, type_sequence<REST...>>::type;
  };
  /**
   * Apply the types in the type parameter and wrap it in a sequence of types
   * @tparam HOLDER
   */
  template <typename HOLDER>
  struct packed {
    using type = typename types_coproduct<type_sequence<T>, HOLDER>::type;
  };
};

/**
 * The product of type sequences. Given two or more sequences of types, this computes the cartesian product
 *
 * The cartesian product in this case is a sequence of type sequences, enumerating all combinations of input types
 * <b>
 * For example, given the two type sequences <int, double> and <float, std::string>, the cartesian product is
 * <b>
 * < <int, float>, <int, std::string>, <double, float>, <double, std::string> >
 * @tparam ... parameter pack of type sequences
 */
template <typename...>
struct types_product;

template <template <typename...> typename HOLDER, typename... TYPES>
struct types_product<HOLDER<TYPES...>> {
  using type = type_sequence<type_sequence<TYPES>...>;
};

template <template <typename...> typename HOLDER, typename... TYPES, typename... REST>
struct types_product<HOLDER<TYPES...>, REST...> {
  using type = typename types_coproduct<typename transform_types<typename types_product<REST...>::type,
                                                                 curried_type<TYPES>::template pack>::type...>::type;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
