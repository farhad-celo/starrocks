#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <span>
#include <type_traits>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/concepts.h"
#include "legacy_embedded_ctl/exception.h"
#include "legacy_embedded_ctl/hash.h"
#include "legacy_embedded_ctl/utility.h"

namespace celonis::accelerator::legacy_embedded_ctl {

template <typename T>
class array_view;

/** Returns whether the given view is a (sub-)view into the given range */
template <typename T, std::ranges::contiguous_range RANGE>
[[nodiscard]] bool is_view_into_range(array_view<T> view, RANGE&& r);

template <typename T>
[[nodiscard]] bool cmp_ref(const array_view<T>& lhs, const array_view<T>& rhs);

struct array_view_hash_ref {
  template <typename T>
  [[nodiscard]] std::size_t operator()(const array_view<T>& array_view) const;
};

namespace details {
template <typename TO, typename FROM>
concept type_compatible = array_convertible<std::remove_reference_t<TO>, std::remove_reference_t<FROM>>;
}  // namespace details

/// Non-owned view into a 1-dim array with size information. Similar to std::span but with more safety features.
template <typename T>
class array_view {
 public:
  /* member types */
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using size_type = std::size_t;
  using ssize_type = std::make_signed_t<size_type>;
  using difference_type = std::ptrdiff_t;
  using index_type = std::size_t;
  using pointer = element_type*;
  using reference = element_type&;
  using iterator = pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;

  /* Ctors */
  constexpr array_view() = default;

  /** Begin iterator + size ctor: view for the range [begin, begin + size) */
  template <std::contiguous_iterator It>
  requires details::type_compatible<T, std::iter_reference_t<It>>  //
  constexpr array_view(It begin, size_type size) : begin_(std::to_address(begin)), size_{size} {}

  /** Begin iterator + end iterator ctor: view for the range [begin, end) */
  template <std::contiguous_iterator It, std::sized_sentinel_for<It> End>
  requires details::type_compatible<T, std::iter_reference_t<It>> &&(!std::is_convertible_v<End, size_type>)  //
      constexpr array_view(It first, End last)
      : begin_{std::to_address(first)}, size_{static_cast<size_type>(std::distance(first, last))} {
    if (const auto end{std::to_address(last)}; begin_ > end) {
      throw out_of_range{"Invalid pointer range. Reason: The begin pointer [{}] comes after the end pointer [{}].",
                         ptr_to_int(begin_), ptr_to_int(end)};
    }
  }

  /** Range ctor: view for the (entire) range [begin of range, end of range) */
  template <contiguous_sized_range RANGE>
  requires(std::ranges::borrowed_range<RANGE> || std::is_const_v<T>) &&   //
      details::type_compatible<T, std::ranges::range_reference_t<RANGE>>  //
      constexpr array_view(RANGE&& range)                                 // NOLINT(google-explicit-constructor)
      : array_view{std::ranges::data(range), std::ranges::size(range)} {}

  /* element access: Note a const view does affect the const'ness of the elements. See comment below for the iterator */
  [[nodiscard]] constexpr reference operator[](index_type index) const;
  [[nodiscard]] constexpr reference at(index_type index) const;
  [[nodiscard]] constexpr pointer data() const;
  [[nodiscard]] constexpr reference front() const;
  [[nodiscard]] constexpr reference back() const;

  /* iterators: The const'ness of the underlying data (T) is independent of this view type's const'ness. A const view
   * does not necessarily indicate a read-only view e.g., when the given T is non-const. Same applies for the element
   * accessors above */
  [[nodiscard]] constexpr iterator begin() const;
  [[nodiscard]] constexpr iterator end() const;
  [[nodiscard]] constexpr reverse_iterator rbegin() const;
  [[nodiscard]] constexpr reverse_iterator rend() const;

  /* capacity */
  [[nodiscard]] constexpr bool empty() const;
  [[nodiscard]] constexpr size_type size() const;
  [[nodiscard]] constexpr ssize_type ssize() const;
  [[nodiscard]] constexpr size_type size_bytes() const;

  /* Utilities */
  /** Checks if the given pointer does point into the range represented by this view */
  [[nodiscard]] constexpr bool points_into_view(pointer ptr_to_check) const;
  /** Obtains a sub view consisting of the first N elements of the sequence (bounds checked) */
  [[nodiscard]] constexpr array_view first(size_type count) const;
  /** Obtains a sub view consisting of the last N elements of the sequence (bounds checked) */
  [[nodiscard]] constexpr array_view last(size_type count) const;
  /** Constructs a bounds checked sub_view with the range [begin + offset, begin + offset + size) */
  [[nodiscard]] constexpr array_view sub_view(size_type offset, size_type size) const;
  /** Constructs a bounds checked sub_view with the range [begin + offset, end) */
  [[nodiscard]] constexpr array_view sub_view(size_type offset) const;

 private:
  pointer begin_{nullptr};
  size_type size_{0};
};

template <typename T, std::ranges::contiguous_range RANGE>
inline bool is_view_into_range(array_view<T> view, RANGE&& r) {
  using std::begin;
  using std::end;
  using std::to_address;
  static constexpr std::less_equal<> leq;
  return leq(to_address(begin(r)), to_address(begin(view))) && leq(to_address(end(view)), to_address(end(r)));
}

template <typename T>
inline bool cmp_ref(const array_view<T>& lhs, const array_view<T>& rhs) {
  return lhs.begin() == rhs.begin() && lhs.size() == rhs.size();
}

template <typename T>
inline std::size_t array_view_hash_ref::operator()(const array_view<T>& array_view) const {
  std::size_t hash_value{0};
  hash_combine(hash_value, array_view.begin());
  hash_combine(hash_value, array_view.size());
  return hash_value;
}

template <typename ITER, typename END_OR_SIZE>
array_view(ITER, END_OR_SIZE) -> array_view<std::remove_reference_t<std::iter_reference_t<ITER>>>;

template <typename T, size_t N>
array_view(T (&)[N]) -> array_view<T>;

template <typename R>
array_view(R&&) -> array_view<std::remove_reference_t<std::ranges::range_reference_t<R>>>;

template <typename T>
constexpr typename array_view<T>::reference array_view<T>::operator[](const index_type index) const {
  legacy_embedded_debug_assert(index < size());
  return data()[index];
}

template <typename T>
constexpr typename array_view<T>::reference array_view<T>::at(const index_type index) const {
  if (index >= size()) [[unlikely]] {
    throw out_of_range{"Index [{}] is out of bounds for array view of size [{}].", index, size()};
  }
  return data()[index];
}

template <typename T>
constexpr typename array_view<T>::pointer array_view<T>::data() const {
  return begin_;
}

template <typename T>
constexpr typename array_view<T>::reference array_view<T>::front() const {
  legacy_embedded_debug_assert(size() > 0);
  return data()[0];
}

template <typename T>
constexpr typename array_view<T>::reference array_view<T>::back() const {
  legacy_embedded_debug_assert(size() > 0);
  return data()[size() - 1];
}

template <typename T>
constexpr typename array_view<T>::iterator array_view<T>::begin() const {
  return begin_;
}

template <typename T>
constexpr typename array_view<T>::iterator array_view<T>::end() const {
  return std::next(begin(), size());
}

template <typename T>
constexpr typename array_view<T>::reverse_iterator array_view<T>::rbegin() const {
  return std::make_reverse_iterator(end());
}

template <typename T>
constexpr typename array_view<T>::reverse_iterator array_view<T>::rend() const {
  return std::make_reverse_iterator(begin());
}

template <typename T>
constexpr bool array_view<T>::empty() const {
  return size() == 0;
}

template <typename T>
constexpr typename array_view<T>::size_type array_view<T>::size() const {
  return size_;
}

template <typename T>
constexpr typename array_view<T>::ssize_type array_view<T>::ssize() const {
  return static_cast<ssize_type>(size());
}

template <typename T>
constexpr typename array_view<T>::size_type array_view<T>::size_bytes() const {
  return size() * sizeof(element_type);
}

template <typename T>
constexpr bool array_view<T>::points_into_view(pointer ptr_to_check) const {
  return std::less_equal<pointer>{}(begin(), ptr_to_check) && std::less<pointer>{}(ptr_to_check, end());
}

template <typename T>
constexpr array_view<T> array_view<T>::sub_view(const size_type offset, const size_type size) const {
  if (size > this->size() || offset > this->size() - size) {
    throw out_of_range{"Cannot create sub view with offset [{}] and size [{}] for array view of size [{}].", offset,
                       size, this->size()};
  }
  auto* const new_begin{begin_ + offset};
  auto* const new_end{new_begin + size};
  // Only to document the post condition. Always guaranteed by the invariants of internal state.
  legacy_embedded_debug_assert((size == 0 || points_into_view(new_begin)), "'new_begin' must point into view for non-empty sub view.");
  legacy_embedded_debug_assert((size == 0 || points_into_view(new_end - 1)),
               "'new_end-1' must point into view for non-empty sub view.");
  return {new_begin, new_end};
}

template <typename T>
constexpr array_view<T> array_view<T>::sub_view(const size_type offset) const {
  return sub_view(offset, size() - offset);
}

template <typename T>
constexpr array_view<T> array_view<T>::first(size_type count) const {
  if (count > size()) {
    throw out_of_range{"Cannot create a sub view with [{}] first elements for array view of size [{}].", count, size()};
  }
  return {begin_, begin_ + count};
}

template <typename T>
constexpr array_view<T> array_view<T>::last(size_type count) const {
  if (count > size()) {
    throw out_of_range{"Cannot create a sub view with [{}] last elements for array view of size [{}].", count, size()};
  }
  return {std::prev(end(), count), end()};
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
