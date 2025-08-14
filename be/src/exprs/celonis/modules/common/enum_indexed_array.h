#pragma once

#include <array>
#include <type_traits>

#include <ctl/exception.h>
#include <ctl/type_traits.h>

#include "modules/common/exceptions.h"

namespace celonis::accelerator::common {

/**
 * @brief A generic data structure to encode a map from an enums values to values of some type T. If we need to map from
 * an enum to some other type we already have an internal integer representation and know the max size at compile time.
 * Thus we can avoid the overhead of hashing (e.g. std::unordered_map) and dynamic allocation (e.g. std::vector). This
 * class also offers easier access methods taking the enum values, instead of first requiring a cast to the integer
 * representation.
 *
 * Note that this type is implemented in terms of an std::array with a compile time fixed size taken from `enum::SIZE`.
 *
 * @tparam IDX The index type of the map, must be an enum with an underlying unsigned integral type
 * @tparam VAL The value stored in the map. Must be default and move constructible.
 */
template <typename IDX, typename VAL>
  requires(std::is_enum_v<IDX> && std::unsigned_integral<std::underlying_type_t<IDX>>)
class enum_indexed_array : private std::array<VAL, static_cast<std::size_t>(IDX::SIZE)> {
 public:
  using underlying_type = std::array<VAL, static_cast<std::size_t>(IDX::SIZE)>;
  // /* member types */
  using value_type = VAL;
  using size_type = std::size_t;
  using index_type = IDX;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reference = value_type&;
  using const_reference = const value_type&;
  using rvalue_reference = value_type&&;

  // /* element access */
  [[nodiscard]] constexpr const_reference at(index_type enumeration) const {
    auto index{ctl::enum_to_underlying_type(enumeration)};
    // We explicitly reimplement the bounds check to throw a custom exception which gives more context if thrown.
    common::runtime_assert(index < size(), "Attempting to access enum_indexed_array of size [{}] at index [{}].",
                           size(), index);
    return underlying_type::operator[](index);
  };

  [[nodiscard]] constexpr reference at(index_type enumeration) {
    // We implement the non const at in terms of the const at to avoid duplication
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<reference>(static_cast<const enum_indexed_array&>(*this).at(enumeration));
  };

  // /* special functions */
  enum_indexed_array()
    requires(std::is_move_constructible_v<VAL>)
  = default;
  template <typename... Args>
    requires(std::is_same_v<Args, VAL> && ... && std::is_move_constructible_v<VAL>)
  explicit enum_indexed_array(Args&&... args) : underlying_type{std::forward<Args&&>(args)...} {}

  // /* iterators */
  using underlying_type::begin;
  using underlying_type::cbegin;
  using underlying_type::cend;
  using underlying_type::crbegin;
  using underlying_type::crend;
  using underlying_type::end;
  using underlying_type::rbegin;
  using underlying_type::rend;

  // /* capacity */
  using underlying_type::empty;
  using underlying_type::max_size;
  using underlying_type::size;

  // /*operations*/
  using underlying_type::fill;
};

}  // namespace celonis::accelerator::common