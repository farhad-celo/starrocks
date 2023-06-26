#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string_view>

#include <boost/container_hash/hash.hpp>  // for boost::hash_detail::hash_combine_impl

#include "ctl/bit.h"

namespace celonis::accelerator::ctl {

/**
 * @brief Utility to compute a single hash value over multiple data values
 * @note This behaves exactly like boost::hash_combine but optionally supports to use a given custom hash function
 */
template <typename T, typename HASHER_T = std::hash<T>>
inline void hash_combine(std::size_t& seed, const T& value) {
  const HASHER_T hasher{};
  // call boost internal implementation detail over the alternative of duplicating (and maintaining) the boost code
  seed = boost::hash_detail::hash_combine_impl<bit_width_for_type<std::size_t>()>::fn(seed, hasher(value));
}

/**
 * @brief Utility to compute a single hash value over a range of values represented by the given iterator range
 * @note This behaves exactly like boost::hash_range but optionally supports to use a given custom hash function
 */
template <typename ITERATOR_T, typename HASHER_T = std::hash<typename std::iterator_traits<ITERATOR_T>::value_type>>
[[nodiscard]] inline std::size_t hash_range(const ITERATOR_T begin, const ITERATOR_T end) {
  using value_type = typename std::iterator_traits<ITERATOR_T>::value_type;
  std::size_t seed{0};
  std::for_each(begin, end, [&seed](const value_type& value) { hash_combine<value_type, HASHER_T>(seed, value); });
  return seed;
}

/** @brief Same as 'hash_range' taking a pair of iterators but for convenience accepts the container only */
template <typename CONTAINER_T, typename HASHER_T = std::hash<
                                    typename std::iterator_traits<typename CONTAINER_T::const_iterator>::value_type>>
[[nodiscard]] inline std::size_t hash_range(const CONTAINER_T& container) {
  using iterator_type = typename CONTAINER_T::const_iterator;
  using std::cbegin;
  using std::cend;
  return hash_range<iterator_type, HASHER_T>(cbegin(container), cend(container));
}

[[nodiscard]] inline constexpr std::uint64_t hash_fnv1a64(const char* const key) noexcept {
  // FNV-1a 64bit hashing algorithm.
  std::uint64_t hash = 0xcbf29ce484222325;
  constexpr std::uint64_t prime = 0x100000001b3;
  for (std::size_t idx = 0; key[idx] != '\0'; ++idx) {
    const char current_char = key[idx];
    hash ^= current_char;
    hash *= prime;
  }
  return hash;
}

// TODO(n.weber): can be made constexpr
[[nodiscard]] inline std::uint64_t hash_murmur_64a(std::string_view input) noexcept {
  const char* key{input.data()};
  const std::size_t len{input.size()};

  const std::uint64_t seed = 0xc70f6907UL;
  const std::uint64_t m = 0xc6a4a7935bd1e995;
  const int r = 47;

  std::uint64_t h = seed ^ (len * m);

  const std::uint64_t* data = reinterpret_cast<const std::uint64_t*>(key);
  static_assert(sizeof(decltype(data)) == sizeof(decltype(key)), "Data type sizes do not match");
  const std::uint64_t* end = data + (len / 8);

  while (data != end) {
    std::uint64_t k = 0;
    // required to satisfy the UB sanitizer (unaligned memory access).
    // Do not replace by 'std::uint64_t k = *data++;' (see CPL-2605).
    // TODO(n.weber): Can probably be fixed with 'std::bitcast' in C++20
    std::memcpy(&k, data, sizeof(k));
    ++data;

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;
  }

  const unsigned char* data2 = reinterpret_cast<const unsigned char*>(data);

  switch (len & 7) {
    case 7:
      h ^= std::uint64_t(data2[6]) << 48;
      [[fallthrough]];
    case 6:
      h ^= std::uint64_t(data2[5]) << 40;
      [[fallthrough]];
    case 5:
      h ^= std::uint64_t(data2[4]) << 32;
      [[fallthrough]];
    case 4:
      h ^= std::uint64_t(data2[3]) << 24;
      [[fallthrough]];
    case 3:
      h ^= std::uint64_t(data2[2]) << 16;
      [[fallthrough]];
    case 2:
      h ^= std::uint64_t(data2[1]) << 8;
      [[fallthrough]];
    case 1:
      h ^= std::uint64_t(data2[0]);
      h *= m;
  };

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}

}  // namespace celonis::accelerator::ctl