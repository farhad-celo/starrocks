#pragma once

#include <type_traits>

namespace celonis::accelerator::legacy_embedded_ctl::details {

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
class static_array_base;

/**
 * @brief Type tags to control whether the static array shall be used in shared state or not.
 */
struct shared final : public std::bool_constant<true> {};
struct non_shared final : public std::bool_constant<false> {};

}  // namespace celonis::accelerator::legacy_embedded_ctl::details
