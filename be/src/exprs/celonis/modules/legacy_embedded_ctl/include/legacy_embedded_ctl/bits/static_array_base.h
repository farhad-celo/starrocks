#pragma once

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <ranges>
#include <type_traits>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/bits/memory_utils.h"
#include "legacy_embedded_ctl/bits/static_array_base_fwd.h"
#include "legacy_embedded_ctl/exception.h"
#include "legacy_embedded_ctl/memory/deregistering_deleter.h"

namespace celonis::accelerator::legacy_embedded_ctl::details {

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
[[nodiscard]] static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array_value_init(
    std::size_t size, const utils::allocation_reason& reason, ALLOCATOR_TYPE allocator);

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
[[nodiscard]] static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array_for_overwrite(
    std::size_t size, const utils::allocation_reason& reason, ALLOCATOR_TYPE allocator);

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
[[nodiscard]] static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array(std::size_t size,
                                                                               const utils::allocation_reason& reason,
                                                                               ALLOCATOR_TYPE allocator);

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
[[nodiscard]] static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array(std::size_t size, const T& init_value,
                                                                               const utils::allocation_reason& reason,
                                                                               ALLOCATOR_TYPE allocator);

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
[[nodiscard]] static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array(std::initializer_list<T> init,
                                                                               const utils::allocation_reason& reason,
                                                                               ALLOCATOR_TYPE allocator);

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
[[nodiscard]] static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array(std::ranges::sized_range auto&& range,
                                                                               const utils::allocation_reason& reason,
                                                                               ALLOCATOR_TYPE allocator);

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
void swap(static_array_base<T, SHARED_T, ALLOCATOR_TYPE>& lhs,
          static_array_base<T, SHARED_T, ALLOCATOR_TYPE>& rhs) noexcept;

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
class static_array_base final {
  friend class static_array_base<T, shared, ALLOCATOR_TYPE>;
  friend class static_array_base<const T, shared, ALLOCATOR_TYPE>;
  friend class static_array_base<T, non_shared, ALLOCATOR_TYPE>;
  friend class static_array_base<const T, non_shared, ALLOCATOR_TYPE>;

 public:
  /* member types */
  using value_type = T;
  using size_type = std::size_t;
  using ssize_type = std::make_signed_t<size_type>;
  using index_type = std::size_t;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reference = value_type&;
  using const_reference = const value_type&;
  using allocator_type = ALLOCATOR_TYPE;
  static constexpr bool IS_SHARED{SHARED_T::value};
  using underlying_buffer_type = std::conditional_t<IS_SHARED, std::shared_ptr<value_type[]>,
                                                    deregistering_unique_ptr<value_type[], ALLOCATOR_TYPE>>;

  /* constructors */
  static_array_base() noexcept = default;

  // NOLINTNEXTLINE(bugprone-unhandled-self-assignment)
  [[nodiscard]] static_array_base copy() const;

  template <typename U>
  // NOLINTNEXTLINE(google-explicit-constructor)
  static_array_base(const static_array_base<U, SHARED_T, ALLOCATOR_TYPE>& other) noexcept(IS_SHARED);
  template <typename U>
  // NOLINTNEXTLINE(bugprone-unhandled-self-assignment)
  static_array_base& operator=(const static_array_base<U, SHARED_T, ALLOCATOR_TYPE>& other) noexcept(IS_SHARED);

  template <typename U, typename SHARED_T_>
  // NOLINTNEXTLINE(google-explicit-constructor)
  static_array_base(static_array_base<U, SHARED_T_, ALLOCATOR_TYPE>&& other) noexcept(IS_SHARED == SHARED_T_::value);
  template <typename U, typename SHARED_T_>
  static_array_base& operator=(static_array_base<U, SHARED_T_, ALLOCATOR_TYPE>&& other) noexcept(
      std::is_nothrow_constructible_v<static_array_base, static_array_base<T, SHARED_T_, ALLOCATOR_TYPE>&&>);

  // TODO(l.karnowski) Can we make this private somehow?
  static_array_base(size_type size, underlying_buffer_type data) : size_{size}, data_{std::move(data)} {}

  template <typename U, typename SHARED_T_, typename ALLOCATOR_TYPE_>
  [[nodiscard]] bool operator==(const static_array_base<U, SHARED_T_, ALLOCATOR_TYPE_>& rhs) const
      noexcept(noexcept(std::declval<const T&>() == std::declval<const U&>()));

  /* element access */
  [[nodiscard]] const_reference operator[](index_type index) const noexcept;
  [[nodiscard]] reference operator[](index_type index) noexcept;
  [[nodiscard]] const_reference at(index_type index) const;
  [[nodiscard]] reference at(index_type index);
  [[nodiscard]] const_reference front() const noexcept;
  [[nodiscard]] reference front() noexcept;
  [[nodiscard]] const_reference back() const noexcept;
  [[nodiscard]] reference back() noexcept;
  [[nodiscard]] const_pointer data() const noexcept;
  [[nodiscard]] pointer data() noexcept;
  [[nodiscard]] const_pointer get() const noexcept;
  [[nodiscard]] pointer get() noexcept;

  /* iterators */
  [[nodiscard]] const_iterator cbegin() const noexcept;
  [[nodiscard]] const_iterator begin() const noexcept;
  [[nodiscard]] iterator begin() noexcept;
  [[nodiscard]] const_iterator cend() const noexcept;
  [[nodiscard]] const_iterator end() const noexcept;
  [[nodiscard]] iterator end() noexcept;
  // reverse iterators not implemented

  /* capacity */
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] size_type size() const noexcept;
  [[nodiscard]] ssize_type ssize() const;

  [[nodiscard]] size_type byte_size() const noexcept;
  [[nodiscard]] ssize_type byte_ssize() const;

  /**
   * @brief Releases the allocated memory of this array and sets its size to zero
   */
  void reset() noexcept;
  /** Same as 'reset' but with the naming known from std::vector */
  void clear() noexcept;

  /**
   * @brief releases (i.e., extracts) the raw data array from the static array. By this, the ownership of the resource
   * is transferred to the caller (resource stealing).
   * @return the ownership to the resource held by the static array
   */
  [[nodiscard]] underlying_buffer_type&& release_data() && noexcept;

  /**
   * @brief Same as above 'release_data' but return the owned resource as shared array instead.
   * @return The ownership to the (now shared) resource held by the static array
   */
  [[nodiscard]] std::shared_ptr<value_type[]> release_data_shared() &&;

  [[nodiscard]] size_type use_count() const;

  [[nodiscard]] ALLOCATOR_TYPE get_allocator() const;

  void shrink(size_type size);

  [[nodiscard]] static_array_base sub_array(size_type offset, size_type size) const;

 private:
  static underlying_buffer_type make_default_array_internal() noexcept;
  static underlying_buffer_type make_array_internal(bool do_value_init, size_type size,
                                                    const utils::allocation_reason& reason, ALLOCATOR_TYPE allocator);

  template <typename U>
  static underlying_buffer_type make_array_copy_internal(
      const static_array_base<U, SHARED_T, ALLOCATOR_TYPE>& other) noexcept(IS_SHARED);

  /** internal ctor controlling default and value init */
  static_array_base(size_type s, const utils::allocation_reason& reason, bool do_value_init, ALLOCATOR_TYPE allocator);

  /** data members */
  size_type size_{0};
  underlying_buffer_type data_{make_default_array_internal()};

  /** friend functions */
  friend static_array_base make_static_array_value_init<T, SHARED_T>(std::size_t size,
                                                                     const utils::allocation_reason& reason,
                                                                     ALLOCATOR_TYPE allocator);
  friend static_array_base make_static_array_for_overwrite<T, SHARED_T>(std::size_t size,
                                                                        const utils::allocation_reason& reason,
                                                                        ALLOCATOR_TYPE allocator);
  friend void swap<T, SHARED_T>(static_array_base& lhs, static_array_base& rhs) noexcept;
};

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array_value_init(
    std::size_t size, const utils::allocation_reason& reason, ALLOCATOR_TYPE allocator) {
  return static_array_base<T, SHARED_T, ALLOCATOR_TYPE>{size, reason, true, std::move(allocator)};
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array_for_overwrite(
    std::size_t size, const utils::allocation_reason& reason, ALLOCATOR_TYPE allocator) {
  return static_array_base<T, SHARED_T, ALLOCATOR_TYPE>{size, reason, false, std::move(allocator)};
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array(const std::size_t size,
                                                                        const utils::allocation_reason& reason,
                                                                        ALLOCATOR_TYPE allocator) {
  static_assert(!std::is_scalar_v<T>,
                "For scalar types use the more explicit factory 'make_static_array_value_init' or "
                "'make_static_array_for_overwrite'.");
  return make_static_array_value_init<T, SHARED_T>(size, reason, std::move(allocator));
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array(const std::size_t size, const T& init_value,
                                                                        const utils::allocation_reason& reason,
                                                                        ALLOCATOR_TYPE allocator) {
  auto array{make_static_array_for_overwrite<T, SHARED_T>(size, reason, std::move(allocator))};
  std::fill(array.begin(), array.end(), init_value);
  return array;
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array(std::initializer_list<T> init,
                                                                        const utils::allocation_reason& reason,
                                                                        ALLOCATOR_TYPE allocator) {
  auto array{make_static_array_for_overwrite<T, SHARED_T>(init.size(), reason, std::move(allocator))};
  std::copy(init.begin(), init.end(), array.begin());
  return array;
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE> make_static_array(std::ranges::sized_range auto&& range,
                                                                        const utils::allocation_reason& reason,
                                                                        ALLOCATOR_TYPE allocator) {
  auto array{make_static_array_for_overwrite<T, SHARED_T>(range.size(), reason, std::move(allocator))};
  std::ranges::copy(range, array.begin());
  return array;
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline void swap(static_array_base<T, SHARED_T, ALLOCATOR_TYPE>& lhs,
                 static_array_base<T, SHARED_T, ALLOCATOR_TYPE>& rhs) noexcept {
  using std::swap;
  swap(lhs.size_, rhs.size_);
  swap(lhs.data_, rhs.data_);
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::static_array_base(const size_type s,
                                                                         const utils::allocation_reason& reason,
                                                                         const bool do_value_init,
                                                                         ALLOCATOR_TYPE allocator)
    : size_{s}, data_{make_array_internal(do_value_init, size_, reason, std::move(allocator))} {}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
template <typename U>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::static_array_base(
    const static_array_base<U, SHARED_T, ALLOCATOR_TYPE>& other) noexcept(IS_SHARED)
    : size_{other.size()}, data_{make_array_copy_internal<U>(other)} {}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
template <typename U>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE>& static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::operator=(
    const static_array_base<U, SHARED_T, ALLOCATOR_TYPE>& other) noexcept(IS_SHARED) {
  static_array_base cpy{other};
  swap(*this, cpy);
  return *this;
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE> static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::copy() const {
  return static_array_base<T, SHARED_T, ALLOCATOR_TYPE>{size(), make_array_copy_internal<T>(*this)};
}

// TODO(a.swoboda) On GCC/compiler upgrade, check if this false-positive persists
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
template <typename U, typename SHARED_T_>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::static_array_base(
    static_array_base<U, SHARED_T_, ALLOCATOR_TYPE>&& other) noexcept(IS_SHARED == SHARED_T_::value)
    : size_{other.size()} /*, data_{std::move(other).release_data()}*/ {
#pragma GCC diagnostic pop
  static_assert(IS_SHARED || !SHARED_T_::value, "Can not convert a shared static array to a regular static array.");
  // done here instead of the init list to first pass the static assert. This leads to a more descriptive error message
  data_ = std::move(other).release_data();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
template <typename U, typename SHARED_T_>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE>& static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::
operator=(static_array_base<U, SHARED_T_, ALLOCATOR_TYPE>&& other) noexcept(
    std::is_nothrow_constructible_v<static_array_base, static_array_base<T, SHARED_T_, ALLOCATOR_TYPE>&&>) {
  static_array_base<T, SHARED_T, ALLOCATOR_TYPE> cpy{std::move(other)};
  swap(*this, cpy);
  return *this;
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
template <typename U, typename SHARED_T_, typename ALLOCATOR_TYPE_>
inline bool static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::operator==(
    const static_array_base<U, SHARED_T_, ALLOCATOR_TYPE_>& rhs) const
    noexcept(noexcept(std::declval<const T&>() == std::declval<const U&>())) {
  if (size() != rhs.size()) {
    return false;
  }
  return std::equal(cbegin(), cend(), rhs.cbegin());
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::const_reference
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::operator[](const index_type index) const noexcept {
  legacy_embedded_debug_assert(index < size());
  return data_[index];
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::reference
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::operator[](const index_type index) noexcept {
  legacy_embedded_debug_assert(index < size());
  return data_[index];
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::const_reference
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::at(const index_type index) const {
  if (index >= size()) [[unlikely]] {
    throw legacy_embedded_ctl::out_of_range{"Index [{}] is out of bounds for static array of size [{}].", index, size()};
  }
  return data_[index];
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::reference
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::at(const index_type index) {
  if (index >= size()) [[unlikely]] {
    throw legacy_embedded_ctl::out_of_range{"Index [{}] is out of bounds for static array of size [{}].", index, size()};
  }
  return data_[index];
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::const_reference
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::front() const noexcept {
  legacy_embedded_debug_assert(!empty());
  return *cbegin();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::reference
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::front() noexcept {
  legacy_embedded_debug_assert(!empty());
  return *begin();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::const_reference
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::back() const noexcept {
  legacy_embedded_debug_assert(!empty());
  return *std::prev(cend());
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::reference
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::back() noexcept {
  legacy_embedded_debug_assert(!empty());
  return *std::prev(end());
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::const_pointer
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::data() const noexcept {
  return data_.get();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::pointer
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::data() noexcept {
  return data_.get();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::const_pointer
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::get() const noexcept {
  return data_.get();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::pointer
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::get() noexcept {
  return data_.get();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::const_iterator
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::cbegin() const noexcept {
  return data();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::const_iterator
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::begin() const noexcept {
  return cbegin();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::iterator
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::begin() noexcept {
  return data_.get();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::const_iterator
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::cend() const noexcept {
  return std::next(cbegin(), size());
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::const_iterator
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::end() const noexcept {
  return cend();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::iterator
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::end() noexcept {
  return std::next(begin(), size());
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline bool static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::empty() const noexcept {
  return size() == 0;
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::size_type
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::size() const noexcept {
// TODO(m.hueneberg) False positive, check if it persists with new gcc version
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
  return size_;
#pragma GCC diagnostic pop
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::ssize_type
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::ssize() const {
  // Cannot overflow because only 48 bits are used for addressing
  return static_cast<ssize_type>(size());
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::size_type
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::byte_size() const noexcept {
// TODO(m.hueneberg) False positive, check if it persists with new gcc version
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
  return size_ * sizeof(T);
#pragma GCC diagnostic pop
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::ssize_type
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::byte_ssize() const {
  return static_cast<ssize_type>(byte_size());
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline void static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::reset() noexcept {
  size_ = 0;
  data_.reset();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline void static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::clear() noexcept {
  reset();
}

// clang-format off
// clang format seems to misinterpret the && and falsely indent the line (clang-format version 9.0.1-12)

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::underlying_buffer_type&&
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::release_data() && noexcept {
  return std::move(data_);
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline std::shared_ptr<typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::value_type[]>
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::release_data_shared() && {
  return std::move(data_);
}

// clang-format on

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::size_type
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::use_count() const {
  static_assert(IS_SHARED, "use_count is only implemented for shared array");
  return data_.use_count();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline ALLOCATOR_TYPE static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::get_allocator() const {
  return data_.get_deleter().get_allocator();
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline void static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::shrink(const size_t new_size) {
  static_assert(!IS_SHARED, "shrink re-allocates and replaces the buffer which only works with non shared arrays");
  if (new_size > size_) {
    throw out_of_range{"Cannot shrink a static array of size [{}] to a bigger size [{}]", size_, new_size};
  }

  if (new_size < size()) {
    auto new_data{
        make_array_internal(false, new_size, LEGACY_EMBEDDED_ALLOC_MSG("Allocation to shrink a static array"), get_allocator())};
    std::copy_n(data_.get(), new_size, new_data.get());
    data_ = std::move(new_data);
    size_ = new_size;
  }
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline static_array_base<T, SHARED_T, ALLOCATOR_TYPE> static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::sub_array(
    size_type offset, size_type size) const {
  if (size > size_ || offset > size_ - size) {
    throw out_of_range{"Cannot create sub array with offset [{}] and size [{}] for static array of size [{}]", offset,
                       size, size_};
  }
  return static_array_base{size, underlying_buffer_type{data_, data_.get() + offset}};
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::underlying_buffer_type
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::make_default_array_internal() noexcept {
  if constexpr (IS_SHARED) {
    return nullptr;
  } else {
    return underlying_buffer_type{nullptr, deregistering_deleter<value_type[], ALLOCATOR_TYPE>{0, ALLOCATOR_TYPE{}}};
  }
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::underlying_buffer_type
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::make_array_internal(const bool do_value_init, const size_type size,
                                                                    const utils::allocation_reason& reason,
                                                                    ALLOCATOR_TYPE allocator) {
  return do_value_init ? details::tracked_allocation<T, ALLOCATOR_TYPE, true>(size, reason, std::move(allocator))
                       : details::tracked_allocation<T, ALLOCATOR_TYPE, false>(size, reason, std::move(allocator));
}

template <typename T, typename SHARED_T, typename ALLOCATOR_TYPE>
template <typename U>
inline typename static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::underlying_buffer_type
static_array_base<T, SHARED_T, ALLOCATOR_TYPE>::make_array_copy_internal(
    const static_array_base<U, SHARED_T, ALLOCATOR_TYPE>& other) noexcept(IS_SHARED) {
  if constexpr (IS_SHARED) {
    // in the shared scenario, we simply copy the underlying shared ptr
    return other.data_;
  } else {
    // in the non-shared scenario, we allocate new memory and copy the data into it
    // We always propagate the allocator from the source array
    auto data_cpy{details::tracked_allocation<T, ALLOCATOR_TYPE, false /*DO_VALUE_INIT*/>(
        other.size(), LEGACY_EMBEDDED_ALLOC_MSG("Allocation for copy construction of a static array."),
        other.data_.get_deleter().get_allocator())};
    std::copy(other.cbegin(), other.cend(), data_cpy.get());
    return data_cpy;
  }
}

}  // namespace celonis::accelerator::legacy_embedded_ctl::details
