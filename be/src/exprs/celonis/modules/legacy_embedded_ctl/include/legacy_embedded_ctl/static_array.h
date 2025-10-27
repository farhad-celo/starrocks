#pragma once

#include "legacy_embedded_ctl/bits/static_array_base.h"  // IWYU pragma: export
#include "legacy_embedded_ctl/static_array_allocators.h"
#include "legacy_embedded_ctl/static_array_fwd.h"  // IWYU pragma: export
#include "legacy_embedded_ctl/utils/allocation_messages.h"

/**
 * @brief Implementation of a fixed-size array which does not need to know its size at compile time.
 * The implementation is mostly targeted to replace pairs of {std::unique_ptr<T[]>/std::shared_ptr<T[]>, std::size_t}
 * and usages of std::vector<T> when we want to have a fixed size array but do not know its size at compile time.
 *
 * This implementation should be much more convenient to work with than {std::unique_ptr<T[]>, std::size_t} pairs as it
 * supports common container utilities such as iterators and supports getter functions for its size/emptiness.
 *
 * Please note that while static arrays are taking an allocator as an optional parameter for creation, the
 * allocator is moved to the deleter of the internal data pointer. Thus static_array is only a reference type and not
 * allocator-aware. Allocators will always be propagated from source to target during move or copy, even if allocator
 * propagation is disabled in the allocator.
 *
 * @tparam T the type of the data hold by the static array
 * @tparam SHARED bool flag indicating whether the array is intended for unique or shared ownership
 * @see static_array_base for the implementation details
 */
namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief Factory to create a static array with the given number of elements (value initialized)
 * @note Value-initialization ('T()') for built-in types basically means zero-initialization. If you intend to
 * overwrite all elements anyways, prefer using the 'make_static_array_for_overwrite' factory (for built-in types) as
 * this has no value-initialization overhead. The construction is equivalent to std::vector<T>(size) in regards to
 * initialization.
 */
template <typename T>
[[nodiscard]] static_array<T> make_static_array_value_init(
    std::size_t size, const utils::allocation_reason& reason,
    utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] static_array<T> make_static_array_value_init(std::size_t size, const utils::allocation_reason& reason,
                                                           static_array_allocator_type<T> allocator);

/**
 * @brief Factory to create a static array with the given number of elements (default initialized)
 * @note This factory only makes sense for built-in types if for performance reasons one wants to be explicit whether to
 * do default or value initialization of the underlying storage. For custom (i.e., class) types, value initialization
 * does not have any performance impact (use the regular, less verbose, 'make_static_array' factory in this case).
 */
template <typename T>
[[nodiscard]] static_array<T> make_static_array_for_overwrite(
    std::size_t size, const utils::allocation_reason& reason,
    utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] static_array<T> make_static_array_for_overwrite(std::size_t size, const utils::allocation_reason& reason,
                                                              static_array_allocator_type<T> allocator);

/**
 * @brief Factory to create a static array with the given number of elements
 */
template <typename T>
[[nodiscard]] static_array<T> make_static_array(std::size_t size, const utils::allocation_reason& reason,
                                                utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] static_array<T> make_static_array(std::size_t size, const utils::allocation_reason& reason,
                                                static_array_allocator_type<T> allocator);

/**
 * @brief Factory to create a static array with the given number of elements initialized to the given 'init_value'
 */
template <typename T>
[[nodiscard]] static_array<T> make_static_array(std::size_t size, const T& init_value,
                                                const utils::allocation_reason& reason,
                                                utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] static_array<T> make_static_array(std::size_t size, const T& init_value,
                                                const utils::allocation_reason& reason,
                                                static_array_allocator_type<T> allocator);

/**
 * @brief Factory to create a static array with the given elements in the initializer list
 */
template <typename T>
[[nodiscard]] static_array<T> make_static_array(std::initializer_list<T> init, const utils::allocation_reason& reason,
                                                utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] static_array<T> make_static_array(std::initializer_list<T> init, const utils::allocation_reason& reason,
                                                static_array_allocator_type<T> allocator);

/**
 * @brief Factory to create a static array with the given sized range
 */
template <typename T, std::ranges::sized_range RANGE>
  requires(!std::same_as<RANGE, std::initializer_list<T>>)
[[nodiscard]] static_array<T> make_static_array(RANGE&& range, const utils::allocation_reason& reason,
                                                utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T, std::ranges::sized_range RANGE>
  requires(!std::same_as<RANGE, std::initializer_list<T>>)
[[nodiscard]] static_array<T> make_static_array(RANGE&& range, const utils::allocation_reason& reason,
                                                static_array_allocator_type<T> allocator);

template <typename T>
void swap(static_array<T>& lhs, static_array<T>& rhs) noexcept;

/**
 * @brief What follows are the equivalent declarations as above but for arrays of shared ownership
 */
template <typename T>
[[nodiscard]] shared_static_array<T> make_shared_static_array_value_init(
    std::size_t size, const utils::allocation_reason& reason,
    utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] shared_static_array<T> make_shared_static_array_value_init(std::size_t size,
                                                                         const utils::allocation_reason& reason,
                                                                         static_array_allocator_type<T> allocator);

template <typename T>
[[nodiscard]] shared_static_array<T> make_shared_static_array_for_overwrite(
    std::size_t size, const utils::allocation_reason& reason,
    utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] shared_static_array<T> make_shared_static_array_for_overwrite(std::size_t size,
                                                                            const utils::allocation_reason& reason,
                                                                            static_array_allocator_type<T> allocator);

template <typename T>
[[nodiscard]] shared_static_array<T> make_shared_static_array(
    std::size_t size, const utils::allocation_reason& reason,
    utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] shared_static_array<T> make_shared_static_array(std::size_t size, const utils::allocation_reason& reason,
                                                              static_array_allocator_type<T> allocator);

template <typename T>
[[nodiscard]] shared_static_array<T> make_shared_static_array(
    std::size_t size, const T& init_value, const utils::allocation_reason& reason,
    utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] shared_static_array<T> make_shared_static_array(std::size_t size, const T& init_value,
                                                              const utils::allocation_reason& reason,
                                                              static_array_allocator_type<T> allocator);

template <typename T>
[[nodiscard]] shared_static_array<T> make_shared_static_array(
    std::initializer_list<T> init, const utils::allocation_reason& reason,
    utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T>
[[nodiscard]] shared_static_array<T> make_shared_static_array(std::initializer_list<T> init,
                                                              const utils::allocation_reason& reason,
                                                              static_array_allocator_type<T> allocator);

template <typename T, std::ranges::sized_range RANGE>
  requires(!std::same_as<RANGE, std::initializer_list<T>>)
[[nodiscard]] shared_static_array<T> make_shared_static_array(
    RANGE&& range, const utils::allocation_reason& reason,
    utils::allocation_priority priority = utils::allocation_priority::LOW);

template <typename T, std::ranges::sized_range RANGE>
  requires(!std::same_as<RANGE, std::initializer_list<T>>)
[[nodiscard]] shared_static_array<T> make_shared_static_array(RANGE&& range, const utils::allocation_reason& reason,
                                                              static_array_allocator_type<T> allocator);

template <typename T>
void swap(shared_static_array<T>& lhs, shared_static_array<T>& rhs) noexcept;

/*
 ***********************************************************************************************************************
 *** Implementation section of above declarations
 ***********************************************************************************************************************
 */

template <typename T>
inline static_array<T> make_static_array_value_init(std::size_t size, const utils::allocation_reason& reason,
                                                    const utils::allocation_priority priority) {
  return details::make_static_array_value_init<T, details::non_shared>(
      size, reason, make_default_tracking_allocator<T>(reason, priority, true));
}

template <typename T>
inline static_array<T> make_static_array_value_init(std::size_t size, const utils::allocation_reason& reason,
                                                    static_array_allocator_type<T> allocator) {
  return details::make_static_array_value_init<T, details::non_shared>(size, reason, std::move(allocator));
}

template <typename T>
inline static_array<T> make_static_array_for_overwrite(std::size_t size, const utils::allocation_reason& reason,
                                                       const utils::allocation_priority priority) {
  return details::make_static_array_for_overwrite<T, details::non_shared>(
      size, reason, make_default_tracking_allocator<T>(reason, priority, false));
}

template <typename T>
inline static_array<T> make_static_array_for_overwrite(std::size_t size, const utils::allocation_reason& reason,
                                                       static_array_allocator_type<T> allocator) {
  return details::make_static_array_for_overwrite<T, details::non_shared>(size, reason, std::move(allocator));
}

template <typename T>
inline static_array<T> make_static_array(const std::size_t size, const utils::allocation_reason& reason,
                                         const utils::allocation_priority priority) {
  return details::make_static_array<T, details::non_shared>(size, reason,
                                                            make_default_tracking_allocator<T>(reason, priority, true));
}

template <typename T>
inline static_array<T> make_static_array(const std::size_t size, const utils::allocation_reason& reason,
                                         static_array_allocator_type<T> allocator) {
  return details::make_static_array<T, details::non_shared>(size, reason, std::move(allocator));
}

template <typename T>
inline static_array<T> make_static_array(const std::size_t size, const T& init_value,
                                         const utils::allocation_reason& reason,
                                         const utils::allocation_priority priority) {
  return details::make_static_array<T, details::non_shared>(size, init_value, reason,
                                                            make_default_tracking_allocator<T>(reason, priority, true));
}

template <typename T>
inline static_array<T> make_static_array(const std::size_t size, const T& init_value,
                                         const utils::allocation_reason& reason,
                                         static_array_allocator_type<T> allocator) {
  return details::make_static_array<T, details::non_shared>(size, init_value, reason, std::move(allocator));
}

template <typename T>
inline static_array<T> make_static_array(std::initializer_list<T> init, const utils::allocation_reason& reason,
                                         const utils::allocation_priority priority) {
  return details::make_static_array<T, details::non_shared>(std::move(init), reason,
                                                            make_default_tracking_allocator<T>(reason, priority, true));
}

template <typename T>
inline static_array<T> make_static_array(std::initializer_list<T> init, const utils::allocation_reason& reason,
                                         static_array_allocator_type<T> allocator) {
  return details::make_static_array<T, details::non_shared>(std::move(init), reason, std::move(allocator));
}

template <typename T, std::ranges::sized_range RANGE>
  requires(!std::same_as<RANGE, std::initializer_list<T>>)
inline static_array<T> make_static_array(RANGE&& range, const utils::allocation_reason& reason,
                                         const utils::allocation_priority priority) {
  return details::make_static_array<T, details::non_shared>(std::forward<RANGE>(range), reason,
                                                            make_default_tracking_allocator<T>(reason, priority, true));
}

template <typename T, std::ranges::sized_range RANGE>
  requires(!std::same_as<RANGE, std::initializer_list<T>>)
inline static_array<T> make_static_array(RANGE&& range, const utils::allocation_reason& reason,
                                         static_array_allocator_type<T> allocator) {
  return details::make_static_array<T, details::non_shared>(std::forward<RANGE>(range), reason, std::move(allocator));
}

template <typename T>
inline void swap(static_array<T>& lhs, static_array<T>& rhs) noexcept {
  details::swap(lhs, rhs);
}

template <typename T>
inline shared_static_array<T> make_shared_static_array_value_init(std::size_t size,
                                                                  const utils::allocation_reason& reason,
                                                                  const utils::allocation_priority priority) {
  return details::make_static_array_value_init<T, details::shared>(
      size, reason, make_default_tracking_allocator<T>(reason, priority, true));
}

template <typename T>
inline shared_static_array<T> make_shared_static_array_value_init(std::size_t size,
                                                                  const utils::allocation_reason& reason,
                                                                  static_array_allocator_type<T> allocator) {
  return details::make_static_array_value_init<T, details::shared>(size, reason, std::move(allocator));
}

template <typename T>
inline shared_static_array<T> make_shared_static_array_for_overwrite(std::size_t size,
                                                                     const utils::allocation_reason& reason,
                                                                     const utils::allocation_priority priority) {
  return details::make_static_array_for_overwrite<T, details::shared>(
      size, reason, make_default_tracking_allocator<T>(reason, priority, false));
}

template <typename T>
inline shared_static_array<T> make_shared_static_array_for_overwrite(std::size_t size,
                                                                     const utils::allocation_reason& reason,
                                                                     static_array_allocator_type<T> allocator) {
  return details::make_static_array_for_overwrite<T, details::shared>(size, reason, std::move(allocator));
}

template <typename T>
inline shared_static_array<T> make_shared_static_array(const std::size_t size, const utils::allocation_reason& reason,
                                                       const utils::allocation_priority priority) {
  return details::make_static_array<T, details::shared>(size, reason,
                                                        make_default_tracking_allocator<T>(reason, priority, true));
}

template <typename T>
inline shared_static_array<T> make_shared_static_array(const std::size_t size, const utils::allocation_reason& reason,
                                                       static_array_allocator_type<T> allocator) {
  return details::make_static_array<T, details::shared>(size, reason, std::move(allocator));
}

template <typename T>
inline shared_static_array<T> make_shared_static_array(const std::size_t size, const T& init_value,
                                                       const utils::allocation_reason& reason,
                                                       const utils::allocation_priority priority) {
  return details::make_static_array<T, details::shared>(size, init_value, reason,
                                                        make_default_tracking_allocator<T>(reason, priority, true));
}

template <typename T>
inline shared_static_array<T> make_shared_static_array(const std::size_t size, const T& init_value,
                                                       const utils::allocation_reason& reason,
                                                       static_array_allocator_type<T> allocator) {
  return details::make_static_array<T, details::shared>(size, init_value, reason, std::move(allocator));
}

template <typename T>
inline shared_static_array<T> make_shared_static_array(std::initializer_list<T> init,
                                                       const utils::allocation_reason& reason,
                                                       const utils::allocation_priority priority) {
  return details::make_static_array<T, details::shared>(std::move(init), reason,
                                                        make_default_tracking_allocator<T>(reason, priority, true));
}

template <typename T>
inline shared_static_array<T> make_shared_static_array(std::initializer_list<T> init,
                                                       const utils::allocation_reason& reason,
                                                       static_array_allocator_type<T> allocator) {
  return details::make_static_array<T, details::shared>(std::move(init), reason, std::move(allocator));
}

template <typename T, std::ranges::sized_range RANGE>
  requires(!std::same_as<RANGE, std::initializer_list<T>>)
inline shared_static_array<T> make_shared_static_array(RANGE&& range, const utils::allocation_reason& reason,
                                                       const utils::allocation_priority priority) {
  return details::make_static_array<T, details::shared>(std::forward<RANGE>(range), reason,
                                                        make_default_tracking_allocator<T>(reason, priority, true));
}

template <typename T, std::ranges::sized_range RANGE>
  requires(!std::same_as<RANGE, std::initializer_list<T>>)
inline shared_static_array<T> make_shared_static_array(RANGE&& range, const utils::allocation_reason& reason,
                                                       static_array_allocator_type<T> allocator) {
  return details::make_static_array<T, details::shared>(std::forward<RANGE>(range), reason, std::move(allocator));
}

template <typename T>
inline void swap(shared_static_array<T>& lhs, shared_static_array<T>& rhs) noexcept {
  details::swap(lhs, rhs);
}

/**
 * Short reminder on default initialization vs. value initialization for scalar types (e.g., built-in types or enums)
 *
 * Default initialization (simplified):
 * - Is performed when a variable is declared without an initializer
 * - E.g., 'int i;', 'std::string s;' or 'T t;'
 * - For non-class variables (e.g., built-in types or enums) default initialization leaves the variable in an
 * indeterminate state and should never been read before a value has been assigned explicitly
 * - E.g., after the following initialization 'int i;' 'i' is in an indeterminate state and could contain any value
 *
 * Value initialization (simplified):
 * - Is performed when a variable is declared with empty pairs of '()' or '{}' as the initializer
 * - E.g., 'int i{};', 'std::string s{};' or 'T t{};'
 * - For non-class variables (e.g., built-in types or enums) value initialization performs zero-initialization and
 * leaves the variable in a deterministic state state
 * - E.g., after the following initialization 'int i{};' 'i' is in a deterministic state and contains the value '0'
 * - Note: value initialization can have a small but measurable performance impact to zero-out the memory
 *
 * Class types (highly simplified):
 * - For class types the difference is not as relevant as usually for both default and value initialization simply the
 * default constructor is called
 */

}  // namespace celonis::accelerator::legacy_embedded_ctl
