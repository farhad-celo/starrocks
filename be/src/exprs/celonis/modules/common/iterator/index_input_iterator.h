#pragma once

namespace celonis::accelerator::common::iterator {

/**
 * Thin *LegacyInputIterator* wrapper for an integer
 * @tparam INTEGER the integer type to wrap
 */
template <typename INTEGER>
class index_input_iterator final {
  static_assert(std::is_integral_v<INTEGER>,
                "index_input_iterator was designed for integers. Your mileage may vary with other types.");

 public:
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = std::make_signed_t<INTEGER>;
  using value_type = INTEGER;
  using pointer = INTEGER*;
  using reference = const INTEGER&;

  INTEGER i{};

  constexpr index_input_iterator& operator++() noexcept {
    ++i;
    return *this;
  }
  constexpr index_input_iterator operator++(int) noexcept {
    const auto result{*this};
    operator++();
    return result;
  }
  constexpr index_input_iterator& operator--() noexcept {
    --i;
    return *this;
  }
  constexpr index_input_iterator operator--(int) noexcept {
    const auto result{*this};
    operator--();
    return result;
  }
  constexpr index_input_iterator& operator+=(difference_type offset) noexcept {
    i += offset;
    return *this;
  }
  constexpr index_input_iterator& operator-=(difference_type offset) noexcept {
    i -= offset;
    return *this;
  }
  constexpr index_input_iterator operator+(difference_type offset) const noexcept {
    auto result{*this};
    return result += offset;
  }
  constexpr index_input_iterator operator-(difference_type offset) const noexcept {
    auto result{*this};
    return result -= offset;
  }
  constexpr difference_type operator-(index_input_iterator rhs) const noexcept { return i - rhs.i; }
  friend constexpr index_input_iterator operator+(difference_type offset, index_input_iterator it) noexcept {
    return it + offset;
  }
  friend constexpr index_input_iterator operator-(difference_type offset, index_input_iterator it) noexcept {
    return it - offset;
  }
  constexpr INTEGER operator[](difference_type offset) const noexcept { return static_cast<INTEGER>(i + offset); }
  constexpr bool operator<(index_input_iterator rhs) const noexcept { return i < rhs.i; }
  constexpr bool operator>(index_input_iterator rhs) const noexcept { return i > rhs.i; }
  constexpr bool operator<=(index_input_iterator rhs) const noexcept { return i <= rhs.i; }
  constexpr bool operator>=(index_input_iterator rhs) const noexcept { return i >= rhs.i; }
  constexpr bool operator==(index_input_iterator rhs) const noexcept { return i == rhs.i; }
  constexpr bool operator!=(index_input_iterator rhs) const noexcept { return i != rhs.i; }
  constexpr INTEGER operator*() const noexcept { return i; }
  constexpr const INTEGER* operator->() const noexcept { return &i; }
};

// TODO(a.swoboda) with C++20, this user-defined deduction guide should become obsolete
template <typename INTEGER>
explicit index_input_iterator(INTEGER) -> index_input_iterator<INTEGER>;

}  // namespace celonis::accelerator::common::iterator
