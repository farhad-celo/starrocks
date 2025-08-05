#pragma once

// Include missing in named type library:
// https://github.com/joboccara/NamedType/commit/ce05a9418be21516044c6e122829856be518a24b
// TODO(l.karnowski) Remove this with newer version of NamedType
#include <tuple>

#include "NamedType/named_type.hpp"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * Allows to create a zero-overhead strong type for an underlying type.
 *
 * Example:
 * void foo(bool); // What is the boolean parameter for?
 *
 * using zero_init_t = legacy_embedded_ctl::named_type<bool, struct zero_init_tag>;
 * void foo(zero_init_t); // Now it is clear and type-safe what the parameter is used for.
 */
template <typename T, typename TAG, template <typename> class... SKILLS>
using named_type = ::fluent::NamedType<T, TAG, SKILLS...>;

// If needed, more aliases can be added from here:
// https://github.com/joboccara/NamedType/blob/master/include/NamedType/underlying_functionalities.hpp

template <typename DESTINATION>
using implicitly_convertible_to = ::fluent::ImplicitlyConvertibleTo<DESTINATION>;

template <typename T>
using hashable = ::fluent::Hashable<T>;

template <typename T>
using comparable = ::fluent::Comparable<T>;

template <typename T>
using printable = ::fluent::Printable<T>;

template <typename NAMED_TYPE>
struct sequencable : fluent::crtp<NAMED_TYPE, sequencable> {
  template <typename... ARGS>
  constexpr auto emplace_back(ARGS&&... args) {
    return this->underlying().get().emplace_back(std::forward<ARGS>(args)...);
  }
  [[nodiscard]] constexpr auto front() const { return this->underlying().get().front(); };
  [[nodiscard]] constexpr auto back() const { return this->underlying().get().back(); };
};

template <typename NAMED_TYPE>
struct clearable : fluent::crtp<NAMED_TYPE, clearable> {
  constexpr void clear() { return this->underlying().get().clear(); }
};

/**
 * The underlying strong type fulfills the conditions of https://en.cppreference.com/w/cpp/named_req/Container
 */
template <typename NAMED_TYPE>
struct containerable : fluent::crtp<NAMED_TYPE, containerable> {
  [[nodiscard]] constexpr auto begin() { return this->underlying().get().begin(); }
  [[nodiscard]] constexpr auto end() { return this->underlying().get().end(); }
  [[nodiscard]] constexpr auto begin() const { return this->underlying().get().begin(); }
  [[nodiscard]] constexpr auto end() const { return this->underlying().get().end(); }
  [[nodiscard]] constexpr auto cbegin() const { return this->underlying().get().cbegin(); }
  [[nodiscard]] constexpr auto cend() const { return this->underlying().get().cend(); }
  constexpr bool operator==(const containerable<NAMED_TYPE>& other) const {
    return this->underlying().get() == other.underlying().get();
  }
  constexpr bool operator!=(const containerable<NAMED_TYPE>& other) const {
    return this->underlying().get() != other.underlying().get();
  }
  constexpr void swap(NAMED_TYPE& other) const { return this->underlying().get().swap(other); }
  [[nodiscard]] constexpr auto size() const { return this->underlying().get().size(); }
  [[nodiscard]] constexpr auto max_size() const { return this->underlying().get().max_size(); }
  [[nodiscard]] constexpr bool empty() const { return this->underlying().get().empty(); }
};

/**
 * A skill describing the features of a strong type wrapping an std::vector.
 * Does not define the complete interface of std::vector only what was required for usages, feel free to add more parts
 * of the std::vector interface to it as needed
 */
template <typename NAMED_TYPE>
struct strongly_typed_vector : containerable<NAMED_TYPE>, sequencable<NAMED_TYPE>, clearable<NAMED_TYPE> {};

}  // namespace celonis::accelerator::legacy_embedded_ctl
