#pragma once

#include <array>
#include <concepts>
#include <iterator>
#include <memory>
#include <optional>
#include <tuple>
#include <variant>

#include <boost/dynamic_bitset.hpp>
#include <boost/fusion/functional/invocation/invoke.hpp>
#include <boost/iterator/counting_iterator.hpp>
#include <boost/iterator/zip_iterator.hpp>
#include <tbb/parallel_for.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/bitset_view.h"
#include "legacy_embedded_ctl/dynamic_bitset.h"
#include "legacy_embedded_ctl/interval.h"
#include "legacy_embedded_ctl/math.h"
#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/exceptions.h"
#include "modules/common/execution_context.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/column.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/dictionary.h"
#include "modules/memory/management/swappable_bitset.h"
#include "modules/memory/materialized_data.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {

namespace details {

// TODO(s.pirsch): Unify with iterator_crtp_base.h
template <typename T, typename VALUE_TYPE, typename DERIVED>
class column_iterator_crtp_base {
 public:
  using difference_type = row_id;
  using value_type = VALUE_TYPE;
  using pointer = value_type*;
  using reference = value_type;
  using iterator_category = std::random_access_iterator_tag;

  column_iterator_crtp_base() = delete;
  explicit column_iterator_crtp_base(const row_id current) noexcept : current_{current} {}

  friend DERIVED& operator++(DERIVED& d) noexcept {
    ++d.current_;
    return d;
  }
  [[nodiscard]] friend DERIVED operator++(DERIVED& d, int) noexcept {
    DERIVED ret{d};
    ++d;
    return ret;
  }

  friend DERIVED& operator--(DERIVED& d) noexcept {
    --d.current_;
    return d;
  }

  [[nodiscard]] friend DERIVED operator--(DERIVED& d, int) noexcept {
    DERIVED ret{d};
    --d;
    return ret;
  }

  friend DERIVED& operator+=(DERIVED& d, difference_type offset) noexcept {
    d.current_ += offset;
    return d;
  }
  friend DERIVED& operator-=(DERIVED& d, difference_type offset) noexcept {
    d.current_ -= offset;
    return d;
  }

  [[nodiscard]] friend DERIVED operator+(const DERIVED& d, difference_type offset) noexcept {
    DERIVED ret{d};
    return ret += offset;
  }
  [[nodiscard]] friend DERIVED operator+(difference_type offset, const DERIVED& d) noexcept { return d + offset; }

  [[nodiscard]] friend difference_type operator-(const DERIVED& d1, const DERIVED& d2) noexcept {
    return d1.current_ - d2.current_;
  }

  [[nodiscard]] friend DERIVED operator-(const DERIVED& d, difference_type offset) noexcept {
    DERIVED ret{d};
    return ret -= offset;
  }

  // TODO(l.karnowski) C++20 operator<=>
  [[nodiscard]] friend bool operator==(const DERIVED& d1, const DERIVED& d2) noexcept {
    return d1.current_ == d2.current_;
  }
  [[nodiscard]] friend bool operator!=(const DERIVED& d1, const DERIVED& d2) noexcept { return !(d1 == d2); }
  [[nodiscard]] friend bool operator<(const DERIVED& d1, const DERIVED& d2) noexcept {
    return d1.current_ < d2.current_;
  }
  [[nodiscard]] friend bool operator>(const DERIVED& d1, const DERIVED& d2) noexcept { return d2 < d1; }
  [[nodiscard]] friend bool operator<=(const DERIVED& d1, const DERIVED& d2) noexcept { return !(d1 > d2); }
  [[nodiscard]] friend bool operator>=(const DERIVED& d1, const DERIVED& d2) noexcept { return !(d1 < d2); }

  [[nodiscard]] reference operator[](difference_type idx) const noexcept {
    return *(static_cast<const DERIVED&>(*this) + idx);
  }

 protected:
  [[nodiscard]] row_id current() const noexcept { return current_; }

 private:
  row_id current_{};
};

template <size_t N>
class bitset {
 public:
  using block_type = uint64_t;
  static constexpr size_t BLOCK_SIZE{legacy_embedded_ctl::dynamic_bitset_t::BLOCK_SIZE};
  static constexpr size_t BLOCK_COUNT{legacy_embedded_ctl::div_round_up(N, BLOCK_SIZE)};

  constexpr bitset() = default;
  bitset(const legacy_embedded_ctl::bitset_view_t bitset, legacy_embedded_ctl::half_open_interval<row_id> range) {
    legacy_embedded_debug_assert(range.begin() % BLOCK_SIZE == 0, "Begin of range must be aligned by {}.", BLOCK_SIZE);
    const auto block_offset{static_cast<size_t>(range.begin()) / BLOCK_SIZE};
    const auto block_count{legacy_embedded_ctl::div_round_up(static_cast<size_t>(range.size()), BLOCK_SIZE)};
    if (block_count > BLOCK_COUNT) {
      throw common::internal_exception{"Bitset with block count {} is not large enough for data with block count {}.",
                                       BLOCK_COUNT, block_count};
    }
    std::copy_n(bitset.data() + block_offset, block_count, data_.data());
  }

  constexpr void set_block(size_t block_idx, block_type block) { data_[block_idx] = block; }

  [[nodiscard]] constexpr bool operator[](size_t index) const noexcept {
    const auto [block_index, bit_index] = legacy_embedded_ctl::details::block_and_bit_index::get(index);
    return (data_[block_index] & legacy_embedded_ctl::details::BIT_MASK(bit_index)) != 0;
  }

 private:
  std::array<block_type, BLOCK_COUNT> data_{};
};

}  // namespace details

template <typename T>
class chunk;

template <typename T>
class column_pointer_chunk;

/**
 * A const value iterable (and iterator) for materialized data. Example usage:
 *
 * for (const std::optional<T> value : iterable) { ... }
 */
template <typename T>
class materialized_iterable {
  friend class chunk<T>;

 public:
  using value_accessor_t = typename memory::materialized_typed_data<T>::const_data_accessor_t;
  using null_flags_accessor_t = memory::management::swappable_bitset::const_data_accessor_t;

  class iterator : public details::column_iterator_crtp_base<T, std::optional<T>, iterator> {
    using base = details::column_iterator_crtp_base<T, std::optional<T>, iterator>;

   public:
    using typename base::difference_type;
    using typename base::iterator_category;
    using typename base::pointer;
    using typename base::reference;
    using typename base::value_type;

    iterator(const materialized_iterable& parent, const row_id index) noexcept : base{index}, parent_{&parent} {}

    [[nodiscard]] reference operator*() const noexcept {
      if (parent_->null_flags_accessor_[this->current()]) {
        return std::nullopt;
      }

      return parent_->value_accessor_[this->current()];
    }

   private:
    const materialized_iterable* parent_{};
  };

  materialized_iterable() = delete;
  materialized_iterable(value_accessor_t value_accessor, null_flags_accessor_t null_flags_accessor) noexcept
      : value_accessor_{std::move(value_accessor)}, null_flags_accessor_{std::move(null_flags_accessor)} {}

  [[nodiscard]] iterator begin() const noexcept { return iterator{*this, 0}; }
  [[nodiscard]] iterator end() const noexcept { return iterator{*this, static_cast<row_id>(value_accessor_.size())}; }
  [[nodiscard]] row_id size() const noexcept { return static_cast<row_id>(value_accessor_.size()); }

 private:
  value_accessor_t value_accessor_;
  null_flags_accessor_t null_flags_accessor_;
};

/**
 * A const value iterable (and iterator) for dictified data. Example usage:
 *
 * for (const std::optional<T> value : iterable) { ... }
 */
template <typename T, typename COL_PTR_TYPE>
class dictified_iterable {
  friend class chunk<T>;

 public:
  using dict_accessor_t = typename memory::typed_dictionary<T>::const_data_accessor_t;
  using col_ptrs_accessor_t = typename memory::column_ptrs_impl<COL_PTR_TYPE>::const_data_accessor_t;

  class iterator : public details::column_iterator_crtp_base<T, std::optional<T>, iterator> {
    using base = details::column_iterator_crtp_base<T, std::optional<T>, iterator>;

   public:
    using typename base::difference_type;
    using typename base::iterator_category;
    using typename base::pointer;
    using typename base::reference;
    using typename base::value_type;

    iterator(const dictified_iterable& parent, const row_id index) noexcept : base{index}, parent_{&parent} {}

    [[nodiscard]] reference operator*() const noexcept {
      const auto col_ptr{parent_->col_ptrs_accessor_[this->current()]};
      if (memory::dictionary::is_null(col_ptr)) {
        return std::nullopt;
      }

      return parent_->dict_accessor_[col_ptr];
    }

   private:
    const dictified_iterable* parent_{};
  };

  dictified_iterable(dict_accessor_t dict_accessor, col_ptrs_accessor_t col_ptrs_accessor) noexcept
      : dict_accessor_{std::move(dict_accessor)}, col_ptrs_accessor_{std::move(col_ptrs_accessor)} {}

  [[nodiscard]] iterator begin() const noexcept { return iterator{*this, 0}; }
  [[nodiscard]] iterator end() const noexcept {
    return iterator{*this, static_cast<row_id>(col_ptrs_accessor_.size())};
  }
  [[nodiscard]] row_id size() const noexcept { return static_cast<row_id>(col_ptrs_accessor_.size()); }

 private:
  dict_accessor_t dict_accessor_;
  col_ptrs_accessor_t col_ptrs_accessor_;
};

/**
 * A const column pointer iterable (and iterator) for dictified data. Example usage:
 *
 * for (const memory::col_ptr_64_t pointer : iterable) { ... }
 */
template <typename T, typename COL_PTR_TYPE>
class column_pointer_iterable {
  friend class column_pointer_chunk<T>;

 public:
  using col_ptrs_accessor_t = typename memory::column_ptrs_impl<COL_PTR_TYPE>::const_data_accessor_t;

  class iterator : public details::column_iterator_crtp_base<T, memory::col_ptr_64_t, iterator> {
    using base = details::column_iterator_crtp_base<T, memory::col_ptr_64_t, iterator>;

   public:
    using typename base::difference_type;
    using typename base::iterator_category;
    using typename base::pointer;
    using typename base::reference;
    using typename base::value_type;

    iterator(const column_pointer_iterable& parent, const row_id index) noexcept : base{index}, parent_{&parent} {}

    [[nodiscard]] reference operator*() const noexcept {
      return static_cast<reference>(parent_->col_ptrs_accessor_[this->current()]);
    }

   private:
    const column_pointer_iterable* parent_{};
  };

  explicit column_pointer_iterable(col_ptrs_accessor_t col_ptrs_accessor) noexcept
      : col_ptrs_accessor_{std::move(col_ptrs_accessor)} {}

  [[nodiscard]] iterator begin() const noexcept { return iterator{*this, 0}; }
  [[nodiscard]] iterator end() const noexcept {
    return iterator{*this, static_cast<row_id>(col_ptrs_accessor_.size())};
  }
  [[nodiscard]] row_id size() const noexcept { return static_cast<row_id>(col_ptrs_accessor_.size()); }

 private:
  col_ptrs_accessor_t col_ptrs_accessor_;
};

/**
 * Since a column can be in different states (i.e. materialized or dictified and if dictified, with differing column
 * pointer types), the column iterable is one of these iterable types. This is modelled using a variant.
 */
template <typename T>
using column_iterable_t = std::conditional_t<
    memory::COL_PTR_64_NEEDED,
    std::variant<materialized_iterable<T>, dictified_iterable<T, col_ptr_8_t>, dictified_iterable<T, col_ptr_16_t>,
                 dictified_iterable<T, col_ptr_32_t>, dictified_iterable<T, col_ptr_64_t>>,
    std::variant<materialized_iterable<T>, dictified_iterable<T, col_ptr_8_t>, dictified_iterable<T, col_ptr_16_t>,
                 dictified_iterable<T, col_ptr_32_t>>>;

template <typename T>
using column_pointer_iterable_t =
    std::conditional_t<memory::COL_PTR_64_NEEDED,
                       std::variant<column_pointer_iterable<T, col_ptr_8_t>, column_pointer_iterable<T, col_ptr_16_t>,
                                    column_pointer_iterable<T, col_ptr_32_t>, column_pointer_iterable<T, col_ptr_64_t>>,
                       std::variant<column_pointer_iterable<T, col_ptr_8_t>, column_pointer_iterable<T, col_ptr_16_t>,
                                    column_pointer_iterable<T, col_ptr_32_t>>>;

/**
 * Creates a value iterable for a column.
 *
 * If the column is either materialized or dictified, the internal state is not changed. If the column is missing,
 * it will be loaded first. Depending on the internal state, either a materialized or a dictified iterable is
 * returned.
 *
 * Example usage:
 *
 * ```c++
 * std::visit(
 *     [](const auto& iterable) {
 *       for (const std::optional<T> value : iterable) { ... }
 *     },
 *     memory::to_iterable(column));
 * ```
 */
template <typename T>
[[nodiscard]] column_iterable_t<T> to_iterable(const memory::column_t& column,
                                               const common::execution_context& parent_context = {}) {
  auto context{parent_context.create_sub_context("memory::to_iterable", {})};

  if (auto materialized{column->get_materialized_typed<T>(context)}) {
    return materialized_iterable<T>{materialized->get_const_data(context),
                                    materialized->get_null_flags()->get_const_data(context)};
  }

  const auto dict{column->get_typed_dict<T>(context)};
  const auto& col_ptrs{column->get_column_pointers(context)};

  return memory::cast_execute_column_pointers(
      [&dict = std::as_const(dict), &context = std::as_const(context)](const auto& tuple) -> column_iterable_t<T> {
        const auto& col_ptrs{std::get<0>(tuple)};
        using col_ptr_type = typename std::decay_t<decltype(col_ptrs)>::value_type;
        return dictified_iterable<T, col_ptr_type>{dict->get_const_data(context), col_ptrs.get_const_accessor()};
      },
      col_ptrs);
}

/**
 * Creates a column pointer iterable for a column. If the column is materialized, an internal exception
 * will be thrown.
 */
template <typename T>
[[nodiscard]] column_pointer_iterable_t<T> to_column_pointer_iterable(
    const memory::column_t& column, const common::execution_context& parent_context = {}) {
  auto context{parent_context.create_sub_context("memory::to_column_pointer_iterable", {})};

  if (auto materialized{column->get_materialized_typed<T>(context)}; materialized != nullptr) {
    throw common::internal_exception{"Cannot use column pointer iterable on the materialized column [{}].",
                                     column->get_name()};
  }

  const auto& col_ptrs{column->get_column_pointers(context)};
  return memory::cast_execute_column_pointers(
      [](const auto& tuple) -> column_pointer_iterable_t<T> {
        const auto& col_ptrs{std::get<0>(tuple)};
        using col_ptr_type = typename std::decay_t<decltype(col_ptrs)>::value_type;
        return column_pointer_iterable<T, col_ptr_type>{col_ptrs.get_const_accessor()};
      },
      col_ptrs);
}

template <typename T>
class chunk {
 public:
  static constexpr row_id MAX_CHUNK_SIZE{1024};
  using values_t = std::array<T, MAX_CHUNK_SIZE>;
  using null_flags_t = details::bitset<MAX_CHUNK_SIZE>;
  using element_type_t = std::optional<T>;

  class iterator : public details::column_iterator_crtp_base<T, std::optional<T>, iterator> {
    using base = details::column_iterator_crtp_base<T, std::optional<T>, iterator>;

   public:
    using typename base::difference_type;
    using typename base::iterator_category;
    using typename base::pointer;
    using typename base::reference;
    using typename base::value_type;

    iterator(const chunk& parent, const row_id index) noexcept : base{index}, parent_{&parent} {}

    [[nodiscard]] reference operator*() const noexcept {
      legacy_embedded_debug_assert(0 <= this->current() && this->current() < parent_->chunk_size_);
      if (parent_->null_flags_[this->current()]) {
        return std::nullopt;
      }
      return parent_->values_[this->current()];
    }

   private:
    const chunk* parent_{};
  };

  constexpr chunk() = default;
  chunk(const materialized_iterable<T>& iterable, legacy_embedded_ctl::half_open_interval<row_id> range)
      : chunk_size_{range.size()},
        values_{copy_from(iterable.value_accessor_.get(), range)},
        null_flags_{iterable.null_flags_accessor_.get(), range} {
    if (chunk_size_ > MAX_CHUNK_SIZE) {
      throw common::internal_exception{"Cannot create chunk of size {}.", chunk_size_};
    }
  }

  template <typename COL_PTR>
  chunk(const dictified_iterable<T, COL_PTR>& iterable, legacy_embedded_ctl::half_open_interval<row_id> range)
      : chunk_size_{range.size()} {
    if (chunk_size_ > MAX_CHUNK_SIZE) {
      throw common::internal_exception{"Cannot create chunk of size {}.", chunk_size_};
    }

    static constexpr row_id BLOCK_SIZE{static_cast<row_id>(null_flags_t::BLOCK_SIZE)};
    for (row_id outer{0}; outer < chunk_size_; outer += BLOCK_SIZE) {
      const row_id end_idx{std::min(chunk_size_, outer + BLOCK_SIZE)};
      uint64_t block{0};
      for (row_id inner{outer}; inner < end_idx; ++inner) {
        const auto col_ptr{iterable.col_ptrs_accessor_[range.begin() + inner]};
        const bool is_null{memory::dictionary::is_null(col_ptr)};
        const auto value{iterable.dict_accessor_[col_ptr]};
        values_[inner] = value;
        block |= (static_cast<uint64_t>(is_null) << (inner % BLOCK_SIZE));
      }
      null_flags_.set_block(static_cast<size_t>(outer) / BLOCK_SIZE, block);
    }
  }

  [[nodiscard]] iterator begin() const noexcept { return iterator{*this, 0}; }
  [[nodiscard]] iterator end() const noexcept { return iterator{*this, chunk_size_}; }
  [[nodiscard]] row_id size() const noexcept { return chunk_size_; }

  [[nodiscard]] const values_t& raw_values() const noexcept { return values_; }
  [[nodiscard]] const null_flags_t& raw_null_flags() const noexcept { return null_flags_; }

 private:
  [[nodiscard]] static values_t copy_from(const T* data, legacy_embedded_ctl::half_open_interval<row_id> range) {
    values_t result;
    std::copy_n(data + range.begin(), range.size(), result.data());
    return result;
  }

  // N.B. no types which allocate should be used as members. This way, a chunk will always reside in the stack.
  row_id chunk_size_{0};
  values_t values_{};
  null_flags_t null_flags_{};
};

template <typename T>
class column_pointer_chunk {
 public:
  static constexpr row_id MAX_CHUNK_SIZE{1024};
  using ptrs_t = std::array<memory::col_ptr_64_t, MAX_CHUNK_SIZE>;
  using element_type_t = memory::col_ptr_64_t;

  class iterator : public details::column_iterator_crtp_base<T, memory::col_ptr_64_t, iterator> {
    using base = details::column_iterator_crtp_base<T, memory::col_ptr_64_t, iterator>;

   public:
    using typename base::difference_type;
    using typename base::iterator_category;
    using typename base::pointer;
    using typename base::reference;
    using typename base::value_type;

    iterator(const column_pointer_chunk& parent, const row_id index) noexcept : base{index}, parent_{&parent} {}

    [[nodiscard]] reference operator*() const noexcept {
      legacy_embedded_debug_assert(0 <= this->current() && this->current() < parent_->chunk_size_);
      return parent_->ptrs_[this->current()];
    }

   private:
    const column_pointer_chunk* parent_{};
  };

  constexpr column_pointer_chunk() = default;

  template <typename COL_PTR>
  column_pointer_chunk(const column_pointer_iterable<T, COL_PTR>& iterable,
                       legacy_embedded_ctl::half_open_interval<row_id> range)
      : chunk_size_{range.size()}, ptrs_{copy_from(iterable.col_ptrs_accessor_.get(), range)} {
    if (chunk_size_ > MAX_CHUNK_SIZE) {
      throw common::internal_exception{"Cannot create chunk of size {}.", chunk_size_};
    }
  }

  [[nodiscard]] iterator begin() const noexcept { return iterator{*this, 0}; }
  [[nodiscard]] iterator end() const noexcept { return iterator{*this, chunk_size_}; }
  [[nodiscard]] row_id size() const noexcept { return chunk_size_; }

  [[nodiscard]] const ptrs_t& raw_pointers() const noexcept { return ptrs_; }

 private:
  template <typename COL_PTR>
  [[nodiscard]] static ptrs_t copy_from(const COL_PTR* data, legacy_embedded_ctl::half_open_interval<row_id> range) {
    ptrs_t result{};
    legacy_embedded_debug_assert(static_cast<row_id>(result.size()) >= range.size());
    std::copy_n(data + range.begin(), range.size(), result.data());
    return result;
  }

  // N.B. no types which allocate should be used as members. This way, a chunk will always reside in the stack.
  row_id chunk_size_{0};
  ptrs_t ptrs_{};
};

static constexpr row_id DEFAULT_CHUNK_SIZE{1024};

enum class iterable_type { VALUE, COLUMN_PTR };

/**
 * A value chunked_iterable implementation
 */
template <typename T, iterable_type ITER_TYPE = iterable_type::VALUE>
class chunked_iterable {
 public:
  using chunk_type_t = chunk<T>;
  class iterator : public details::column_iterator_crtp_base<T, chunk_type_t, iterator> {
    using base = details::column_iterator_crtp_base<T, chunk_type_t, iterator>;

   public:
    using typename base::difference_type;
    using typename base::iterator_category;
    using typename base::pointer;
    using typename base::reference;
    using typename base::value_type;

    iterator(const chunked_iterable& parent, const row_id chunk_index) noexcept : base{chunk_index}, parent_{&parent} {}

    [[nodiscard]] reference operator*() const {
      return std::visit(
          [chunk_size = parent_->chunk_size_, chunk_index = this->current()](const auto& iterable) {
            const row_id begin_index{chunk_index * chunk_size};
            const row_id end_index{std::min(begin_index + chunk_size, iterable.size())};
            return chunk<T>{iterable, {begin_index, end_index}};
          },
          parent_->value_upstream_);
    }

   private:
    const chunked_iterable* parent_{};
  };

  explicit chunked_iterable(column_iterable_t<T> upstream, row_id chunk_size = DEFAULT_CHUNK_SIZE)
      : value_upstream_{std::move(upstream)}, chunk_size_{chunk_size} {
    if (chunk_size <= 0 || chunk_size > chunk_type_t::MAX_CHUNK_SIZE || chunk_size % 64 != 0) {
      throw common::internal_exception{"Invalid chunk size: {}.", chunk_size};
    }
  }

  [[nodiscard]] iterator begin() const noexcept { return iterator{*this, 0}; }
  [[nodiscard]] iterator end() const {
    return iterator{*this, std::visit(
                               [chunk_size = chunk_size_](const auto& iterable) {
                                 return legacy_embedded_ctl::div_round_up(iterable.size(), chunk_size);
                               },
                               value_upstream_)};
  }

  [[nodiscard]] row_id get_chunk_size() const noexcept { return chunk_size_; }
  [[nodiscard]] row_id get_element_count() const {
    return std::visit([](const auto& iterable) { return iterable.size(); }, value_upstream_);
  }

 private:
  column_iterable_t<T> value_upstream_;
  row_id chunk_size_;
};

/**
 * A column pointer chunked_iterable implementation
 */
template <typename T>
class chunked_iterable<T, iterable_type::COLUMN_PTR> {
 public:
  using chunk_type_t = column_pointer_chunk<T>;
  class iterator : public details::column_iterator_crtp_base<T, chunk_type_t, iterator> {
    using base = details::column_iterator_crtp_base<T, chunk_type_t, iterator>;

   public:
    using typename base::difference_type;
    using typename base::iterator_category;
    using typename base::pointer;
    using typename base::reference;
    using typename base::value_type;

    iterator(const chunked_iterable& parent, const row_id index) noexcept : base{index}, parent_{&parent} {}

    [[nodiscard]] reference operator*() const {
      return std::visit(
          [chunk_size = parent_->chunk_size_, chunk_index = this->current()](const auto& iterable) {
            const row_id begin_index{chunk_index * chunk_size};
            const row_id end_index{std::min(begin_index + chunk_size, iterable.size())};
            return column_pointer_chunk{iterable, {begin_index, end_index}};
          },
          parent_->col_ptr_upstream_);
    }

   private:
    const chunked_iterable* parent_{};
  };

  explicit chunked_iterable(column_pointer_iterable_t<T> upstream, row_id chunk_size = DEFAULT_CHUNK_SIZE)
      : col_ptr_upstream_{std::move(upstream)}, chunk_size_{chunk_size} {
    if (chunk_size <= 0 || chunk_size > chunk_type_t::MAX_CHUNK_SIZE || chunk_size % 64 != 0) {
      throw common::internal_exception{"Invalid chunk size: {}.", chunk_size};
    }
  }

  [[nodiscard]] iterator begin() const noexcept { return iterator{*this, 0}; }
  [[nodiscard]] iterator end() const {
    return iterator{*this, std::visit(
                               [chunk_size = chunk_size_](const auto& iterable) {
                                 return legacy_embedded_ctl::div_round_up(iterable.size(), chunk_size);
                               },
                               col_ptr_upstream_)};
  }

  [[nodiscard]] row_id get_chunk_size() const noexcept { return chunk_size_; }

  [[nodiscard]] row_id get_element_count() const {
    return std::visit([](const auto& iterable) { return iterable.size(); }, col_ptr_upstream_);
  }

 private:
  column_pointer_iterable_t<T> col_ptr_upstream_;
  row_id chunk_size_;
};

template <typename T, iterable_type ITER_TYPE = iterable_type::VALUE>
struct typed_column {
  using element_type_t = std::optional<T>;
  using chunk_type_t = chunk<T>;
  column_t col;
};

template <typename T>
struct typed_column<T, iterable_type::COLUMN_PTR> {
  using element_type_t = memory::col_ptr_64_t;
  using chunk_type_t = column_pointer_chunk<T>;
  column_t col;
};

// helper class to overload to_chunked_iterable_impl with different ITER_TYPE
template <iterable_type ITER_TYPE>
struct iterable_helper {};

template <typename T>
[[nodiscard]] chunked_iterable<T, iterable_type::VALUE> to_chunked_iterable_impl(
    iterable_helper<iterable_type::VALUE> /*helper*/, const memory::column_t& column,
    const common::execution_context& parent_context, const row_id chunk_size) {
  return chunked_iterable<T, iterable_type::VALUE>{to_iterable<T>(column, parent_context), chunk_size};
}

template <typename T>
[[nodiscard]] chunked_iterable<T, iterable_type::COLUMN_PTR> to_chunked_iterable_impl(
    iterable_helper<iterable_type::COLUMN_PTR> /*helper*/, const memory::column_t& column,
    const common::execution_context& parent_context, const row_id chunk_size) {
  return chunked_iterable<T, iterable_type::COLUMN_PTR>{to_column_pointer_iterable<T>(column, parent_context),
                                                        chunk_size};
}

/**
 * Creates a chunked iterable from a column. Passing a custom chunk size should be used for testing purposes only.
 */
template <typename T, iterable_type ITER_TYPE = iterable_type::VALUE>
[[nodiscard]] chunked_iterable<T, ITER_TYPE> to_chunked_iterable(const memory::column_t& column,
                                                                 const common::execution_context& parent_context,
                                                                 const row_id chunk_size = DEFAULT_CHUNK_SIZE) {
  return to_chunked_iterable_impl<T>(iterable_helper<ITER_TYPE>{}, column, parent_context, chunk_size);
}

/**
 * Creates multiple chunked iterables at once using the default chunk size.
 */
template <typename... TS, iterable_type... ITER_TYPES>
[[nodiscard]] std::tuple<chunked_iterable<TS, ITER_TYPES>...> to_chunked_iterables(
    const common::execution_context& parent_context, typed_column<TS, ITER_TYPES>... columns) {
  return std::make_tuple(to_chunked_iterable<TS, ITER_TYPES>(columns.col, parent_context)...);
}

namespace details {

/*
 * Gets the amount of chunks that the iterables contain. If the amount of chunks does not match
 * across the iterables, an exception is thrown.
 */
template <typename FIRST_T, iterable_type FIRST_ITER_TYPE, typename... TS, iterable_type... ITER_TYPES>
[[nodiscard]] row_id get_amount_of_chunks(const memory::chunked_iterable<FIRST_T, FIRST_ITER_TYPE>& first,
                                          const memory::chunked_iterable<TS, ITER_TYPES>&... tail) {
  const row_id num_chunks_first{std::distance(first.begin(), first.end())};
  if (((std::distance(tail.begin(), tail.end()) != num_chunks_first) || ...)) {
    throw common::internal_exception{
        "Amount of chunks are not matching. At least one iterable did not match the others"};
  }
  return num_chunks_first;
}

template <typename... TS, iterable_type... ITER_TYPES>
[[nodiscard]] row_id get_amount_of_chunks(
    const std::tuple<memory::chunked_iterable<TS, ITER_TYPES>...>& iterables_tuple) {
  return std::apply([](auto&&... iterables) { return get_amount_of_chunks(iterables...); }, iterables_tuple);
}

/*
 * Given iterables, returns the chunks of all iterables at position chunk_index. Does bounds checking.
 */
template <typename... TS, iterable_type... ITER_TYPES>
[[nodiscard]] std::tuple<typename memory::chunked_iterable<TS, ITER_TYPES>::chunk_type_t...> get_chunks_at(
    const row_id chunk_index, const memory::chunked_iterable<TS, ITER_TYPES>&... iterables) {
  const row_id amount_of_chunks{get_amount_of_chunks(iterables...)};

  if (chunk_index >= amount_of_chunks) {
    throw common::internal_exception("Tried to access chunks at index [{}], but there are only [{}] chunks.",
                                     chunk_index, amount_of_chunks);
  }

  return std::make_tuple(*std::next(iterables.begin(), chunk_index)...);
}

template <typename... TS, iterable_type... ITER_TYPES>
[[nodiscard]] std::tuple<typename memory::chunked_iterable<TS, ITER_TYPES>::chunk_type_t...> get_chunks_at(
    const row_id chunk_index, const std::tuple<memory::chunked_iterable<TS, ITER_TYPES>...>& iterables_tuple) {
  return std::apply([&chunk_index](auto&&... args) { return get_chunks_at(chunk_index, args...); }, iterables_tuple);
}

template <typename FIRST_CHUNK, typename... CHUNKS>
row_id check_and_get_chunk_size(const FIRST_CHUNK& first, const CHUNKS&... tail) {
  // Check for equal chunk sizes
  const row_id chunk_size_first{first.size()};

  if (((chunk_size_first != tail.size()) || ...)) {
    throw common::internal_exception(
        "Actual chunk sizes are not matching. At least one chunk did not match the others.");
  }

  return chunk_size_first;
}

/**
 * Throws if chunk sizes of the given chunks do not match.
 */
template <typename... CHUNKS>
void check_chunk_sizes(const CHUNKS&... chunks) {
  details::check_and_get_chunk_size(chunks...);
}

template <typename CHUNK, typename... CHUNKS>
void check_chunk_sizes(const CHUNK& chunk, const std::tuple<CHUNKS...>& further_chunks_tuple) {
  std::apply([&chunk](auto&&... further_chunks) { details::check_and_get_chunk_size(chunk, further_chunks...); },
             further_chunks_tuple);
}

template <typename FUNCTION, typename... CHUNKS>
void for_each_value_in_chunk(FUNCTION&& function, const CHUNKS&... chunks) {
  if (sizeof...(chunks) == 0) {
    return;
  }

  // Check if all chunks are of equal size
  check_chunk_sizes(chunks...);

  auto chunk_begin{boost::make_zip_iterator(boost::make_tuple(chunks.begin()...))};
  auto chunk_end{boost::make_zip_iterator(boost::make_tuple(chunks.end()...))};

  std::for_each(chunk_begin, chunk_end, [&function](const boost::tuple<typename CHUNKS::element_type_t...>& values) {
    boost::fusion::invoke(function, values);
  });
}

/**
 * Iterates over all given chunks simultaneously and calls the given function for each iteration.
 */
template <typename FUNCTION, typename... TS, iterable_type... ITER_TYPES>
void for_each_chunk(FUNCTION&& function, const memory::chunked_iterable<TS, ITER_TYPES>&... iterables) {
  if (sizeof...(iterables) == 0) {
    return;
  }

  // Throws if iterables have different amount of chunks
  const row_id amount_of_chunks{get_amount_of_chunks(iterables...)};

  // Create iterators that yield the chunk index for every iteration
  auto chunk_index_iterator_begin{boost::counting_iterator<row_id>(0)};
  auto chunk_index_iterator_end{boost::counting_iterator<row_id>(amount_of_chunks)};

  auto chunks_begin{boost::make_zip_iterator(boost::make_tuple(chunk_index_iterator_begin, iterables.begin()...))};
  auto chunks_end{boost::make_zip_iterator(boost::make_tuple(chunk_index_iterator_end, iterables.end()...))};

  std::for_each(
      chunks_begin, chunks_end,
      [&function](const boost::tuple<const row_id, typename memory::chunked_iterable<TS, ITER_TYPES>::chunk_type_t...>&
                      chunks) { boost::fusion::invoke(function, chunks); });
};

template <typename FUNCTION, typename... TS, iterable_type... ITER_TYPES>
void for_each_chunk(FUNCTION&& function,
                    const std::tuple<memory::chunked_iterable<TS, ITER_TYPES>...>& iterables_tuple) {
  std::apply([&function](auto&&... iterables) { for_each_chunk(std::forward<FUNCTION>(function), iterables...); },
             iterables_tuple);
}

/**
 * Gets the maximum chunk size for the iterables. This is the maximum chunk size for the iterable, but the last chunk
 * may be of lesser size. Throws if the the amount does not match for all the iterables.
 */
template <typename FIRST_TYPE, iterable_type FIRST_ITER_TYPE, typename... TS, iterable_type... ITER_TYPES>
[[nodiscard]] row_id get_max_chunk_size(const memory::chunked_iterable<FIRST_TYPE, FIRST_ITER_TYPE>& first,
                                        const memory::chunked_iterable<TS, ITER_TYPES>&... tail) {
  const row_id max_chunk_size_first{first.get_chunk_size()};

  // Check for equal chunk sizes
  if (((tail.get_chunk_size() != max_chunk_size_first) || ...)) {
    throw common::internal_exception(
        "Maximum chunk sizes are not matching. At least one iterable did not match the others.");
  }

  return max_chunk_size_first;
}

template <typename... TS, iterable_type... ITER_TYPES>
[[nodiscard]] row_id get_max_chunk_size(
    const std::tuple<memory::chunked_iterable<TS, ITER_TYPES>...>& iterables_tuple) {
  return std::apply([](auto&&... iterables) { return get_max_chunk_size(iterables...); }, iterables_tuple);
}

/**
 * Iterates over all given chunk values simultaneously and calls the given function for each iteration.
 * Also passes a local index (valid inside the chunk) and a global index (based on the chunk index and the maximum
 * chunk size) to the function.
 * @param max_chunk_size The size that a chunk (of the iterable) can have at maximum.
 */
template <typename FUNCTION, typename... CHUNKS>
void for_each_value_in_chunk_with_index(FUNCTION&& function, const row_id max_chunk_size, const row_id chunk_index,
                                        const CHUNKS&... chunks) {
  if (sizeof...(chunks) == 0) {
    return;
  }
  const row_id chunk_size{details::check_and_get_chunk_size(chunks...)};

  // Create iterators that yield the local index inside the chunk for each iteration
  auto local_index_iterator_begin{boost::counting_iterator<row_id>(0)};
  auto local_index_iterator_end{boost::counting_iterator<row_id>(chunk_size)};

  // Create iterators that yield the global index for each iteration, e.g. for writing to the output array
  const row_id global_offset{chunk_index * max_chunk_size};
  auto global_index_iterator_begin{boost::counting_iterator<row_id>(global_offset)};
  auto global_index_iterator_end{boost::counting_iterator<row_id>(global_offset + chunk_size)};

  auto chunk_begin{boost::make_zip_iterator(
      boost::make_tuple(local_index_iterator_begin, global_index_iterator_begin, chunks.begin()...))};
  auto chunk_end{boost::make_zip_iterator(
      boost::make_tuple(local_index_iterator_end, global_index_iterator_end, chunks.end()...))};

  std::for_each(
      chunk_begin, chunk_end,
      [&function](const boost::tuple<const row_id, const row_id, typename CHUNKS::element_type_t...>& values) {
        boost::fusion::invoke(function, values);
      });
};

template <typename FUNCTION, typename... CHUNKS>
void for_each_value_in_chunk_with_index(FUNCTION&& function, const row_id max_chunk_size, const row_id chunk_index,
                                        const std::tuple<CHUNKS...>& chunks) {
  std::apply(
      [&function, &max_chunk_size, &chunk_index](auto&&... chunks) {
        for_each_value_in_chunk_with_index(function, max_chunk_size, chunk_index, chunks...);
      },
      chunks);
}

}  // namespace details

/**
 * Creates a chunked iterable for every column and iterates over all values of the columns simultaneously, calling the
 * supplied function.
 */
template <typename... TS, iterable_type... ITER_TYPES, typename FUNCTION>
  requires std::invocable<FUNCTION, typename typed_column<TS, ITER_TYPES>::element_type_t...>
void for_each_value(FUNCTION&& function, const common::execution_context& context,
                    const typed_column<TS, ITER_TYPES>&... columns) {
  const auto chunked_columns{memory::to_chunked_iterables(context, columns...)};

  details::for_each_chunk(
      [&function](const row_id /* chunk_index */, const typename typed_column<TS, ITER_TYPES>::chunk_type_t... chunks) {
        details::for_each_value_in_chunk(
            [&function](const typename typed_column<TS, ITER_TYPES>::element_type_t... values) {
              std::forward<FUNCTION>(function)(values...);
            },
            chunks...);
      },
      chunked_columns);
}

/**
 * Creates a chunked iterable for every column and iterates over all values of the columns simultaneously, calling the
 * supplied function.
 */
template <typename... TS, iterable_type... ITER_TYPES, typename FUNCTION>
  requires std::invocable<FUNCTION, row_id, typename typed_column<TS, ITER_TYPES>::element_type_t...>
void for_each_value_with_index(FUNCTION&& function, const common::execution_context& context,
                               const typed_column<TS, ITER_TYPES>&... columns) {
  const auto chunked_columns{memory::to_chunked_iterables(context, columns...)};

  size_t global_index{0};
  details::for_each_chunk(
      [&function, &global_index](const row_id /* chunk_index */,
                                 const typename typed_column<TS, ITER_TYPES>::chunk_type_t... chunks) {
        details::for_each_value_in_chunk(
            [&function, &global_index](const typename typed_column<TS, ITER_TYPES>::element_type_t... values) {
              std::forward<FUNCTION>(function)(global_index, values...);
              ++global_index;
            },
            chunks...);
      },
      chunked_columns);
}

/**
 * Similar to for_each_value, but also supplies an global index to the give function and parallelizes the iteration.
 * Therefore also needs a grain_size to be supplied.
 */
template <typename... TS, iterable_type... ITER_TYPES, typename FUNCTION>
  requires std::invocable<FUNCTION, row_id, typename typed_column<TS, ITER_TYPES>::element_type_t...>
void parallel_for_each_value_with_index(FUNCTION&& function, const size_t grain_size,
                                        const common::execution_context& context,
                                        const typed_column<TS, ITER_TYPES>&... columns) {
  const auto chunked_columns{memory::to_chunked_iterables(context, columns...)};
  const row_id max_chunk_size{details::get_max_chunk_size(chunked_columns)};
  const row_id amount_of_chunks{details::get_amount_of_chunks(chunked_columns)};

  tbb::parallel_for(tbb::blocked_range<row_id>{0, amount_of_chunks, grain_size / max_chunk_size},
                    [&function, &max_chunk_size, &chunked_columns](const auto range) {
                      for (row_id chunk_index{range.begin()}; chunk_index < range.end(); chunk_index++) {
                        const auto chunks{details::get_chunks_at(chunk_index, chunked_columns)};

                        details::for_each_value_in_chunk_with_index(
                            [&function](const row_id /* local_index */, const row_id global_index,
                                        const typename typed_column<TS, ITER_TYPES>::element_type_t... values) {
                              std::forward<FUNCTION>(function)(global_index, values...);
                            },
                            max_chunk_size, chunk_index, chunks);
                      }
                    });
}

/**
 * Similar to the other overload, but can also handle an optional column.
 */
template <typename OPTIONAL_T, iterable_type OPTIONAL_ITER_TYPE, typename... TS, iterable_type... ITER_TYPES,
          typename FUNCTION>
  requires std::invocable<FUNCTION, row_id, typename typed_column<OPTIONAL_T, OPTIONAL_ITER_TYPE>::element_type_t,
                          typename typed_column<TS, ITER_TYPES>::element_type_t...>
void parallel_for_each_value_with_index(
    FUNCTION&& function, const size_t grain_size, const common::execution_context& context,
    std::optional<const typed_column<OPTIONAL_T, OPTIONAL_ITER_TYPE>> optional_column,
    const typed_column<TS, ITER_TYPES>&... columns) {
  static_assert(OPTIONAL_ITER_TYPE == iterable_type::VALUE, "do not support optional column pointer typed column.");
  const auto chunked_columns{to_chunked_iterables(context, columns...)};
  const row_id max_chunk_size{details::get_max_chunk_size(chunked_columns)};
  const row_id amount_of_chunks{details::get_amount_of_chunks(chunked_columns)};

  std::optional<chunked_iterable<OPTIONAL_T, OPTIONAL_ITER_TYPE>> optional_chunked_column;
  if (optional_column.has_value()) {
    optional_chunked_column = to_chunked_iterable<OPTIONAL_T, OPTIONAL_ITER_TYPE>(optional_column->col, context);
    auto& optional_chunked_column_value{optional_chunked_column.value()};

    // For the optional column, we have to check if the 1) max chunk size is equal to the other iterables
    if (optional_chunked_column_value.get_chunk_size() != max_chunk_size) {
      throw common::internal_exception(
          "Chunk size of the optional column [{}] does not match the chunk size of the other columns [{}].",
          optional_chunked_column_value.get_chunk_size(), max_chunk_size);
    }

    // and (2) if there is the same amount of chunks as for the other columns
    const row_id optional_column_amount_of_chunks{details::get_amount_of_chunks(optional_chunked_column_value)};
    if (optional_column_amount_of_chunks != amount_of_chunks) {
      throw common::internal_exception(
          "Amount of chunks for the optional column [{}] does not match the amount of chunks of the other columns "
          "[{}].",
          optional_column_amount_of_chunks, amount_of_chunks);
    }
  }

  tbb::parallel_for(
      tbb::blocked_range<row_id>{0, amount_of_chunks, grain_size / max_chunk_size},
      [&optional_chunked_column, &chunked_columns, &function, &max_chunk_size](const auto range) {
        for (row_id chunk_index{range.begin()}; chunk_index < range.end(); chunk_index++) {
          const auto chunks{details::get_chunks_at(chunk_index, chunked_columns)};

          std::optional<typename chunked_iterable<OPTIONAL_T, OPTIONAL_ITER_TYPE>::chunk_type_t> optional_chunk{
              std::nullopt};
          if (optional_chunked_column.has_value()) {
            // Safe to access as we check for equal amount of chunks when creating the chunked iterable
            // for the optional column
            optional_chunk = *std::next(optional_chunked_column->begin(), chunk_index);

            // Check if the size of the optional chunk and the other chunks matches
            details::check_chunk_sizes(optional_chunk.value(), chunks);
          }

          details::for_each_value_in_chunk_with_index(
              [&function, &optional_chunk](const row_id local_index, const row_id global_index,
                                           const typename typed_column<TS, ITER_TYPES>::element_type_t... values) {
                typename typed_column<OPTIONAL_T, OPTIONAL_ITER_TYPE>::element_type_t optional_chunk_value;
                if (optional_chunk.has_value()) {
                  // Safe to access as we check for equal amount of chunks when creating the chunked
                  // iterable for the optional column
                  optional_chunk_value = *std::next(optional_chunk->begin(), local_index);
                }

                std::forward<FUNCTION>(function)(global_index, optional_chunk_value, values...);
              },
              max_chunk_size, chunk_index, chunks);
        }
      });
}

}  // namespace celonis::accelerator::memory
