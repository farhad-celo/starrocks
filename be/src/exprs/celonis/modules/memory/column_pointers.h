#pragma once

#include <initializer_list>
#include <limits>
#include <memory>

#include <ctl/assert.h>
#include <ctl/named_type.h>
#include <ctl/static_array.h>
#include <ctl/type_traits.h>

#include "modules/common/exceptions.h"
#include "modules/common/int_types.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/management/raw_data_handler.h"
#include "modules/memory/row_id.h"
#include "modules/memory/types.h"

/**
 * This file defines two types for storing column pointers.
 * 1) The column_ptrs_impl class stores data in a raw_data_handler with cache. See column_data_handler_t.
 *    Cache information for the raw_data_handler must be provided when creating a column_ptrs_impl.
 *    This type of column pointer can be used directly for creating a column.
 *
 * 2) The raw_column_ptrs_impl class stores data directly in a shared_ptr array without cache.
 *    See column_raw_data_t. Cache information is not required when creating a raw_column_ptrs_impl.
 *    For creating a column, a column_ptrs_impl can be created from this type with the functions
 *    create_cache_column_pointer and create_temp_column_pointer. Cache information is required for these functions.
 *
 * Data type:
 * For both column pointer types the underlying raw data type is determined dynamically during creation by the size of
 * a dictionary to allow a more space efficient data storage. The different raw data types are implemented by
 * templating column_ptrs_impl and raw_column_ptrs_impl.
 *
 * Data abstraction:
 * To handle the different raw data type template instances uniformly, column_ptrs_impl resp. raw_column_ptrs_impl
 * derive from the abstract class column_ptrs_abstract resp. raw_column_ptrs_abstract. Both abstract classes have a
 * uniform interface column_ptrs_abstract_base for providing the size and the type of the underlying raw data.
 * The creator of a column pointer receives only a pointer to one of the abstract classes and doesn't need to know which
 * concrete template instantiation is used.
 *
 * Data access:
 * For both column pointer types the underlying raw data can be accessed uniformly with the functions get_data and
 * get_const_accessor. This access interface is defined in the base class column_ptrs_impl_base for both column
 * pointer types. The size of the underlying raw data can be retrieved with the function get_row_count.
 * Since data access is not defined in the abstract class of a column pointer, the abstract column pointer can be
 * automatically down casted to its implementation type with function cast_execute_column_pointers and the functions
 * defined in execute_casted namespace.
 */

namespace celonis::accelerator::memory {

using col_ptr_64_t = int64_t;
using col_ptr_32_t = int32_t;
using col_ptr_16_t = int16_t;
using col_ptr_8_t = int8_t;
static_assert(sizeof(row_id) == sizeof(col_ptr_64_t) || sizeof(row_id) == sizeof(col_ptr_32_t),
              "row_id data type does not match.");
constexpr bool COL_PTR_64_NEEDED{sizeof(row_id) == 8};

using col_ptr_binary_domain = col_ptr_8_t;

template <class T>
using column_data_handler_t = management::raw_data_handler_t<T>;

template <typename COL_PTR_TYPE>
using column_raw_data_t = ctl::shared_static_array<COL_PTR_TYPE>;

class raw_column_ptrs_abstract;
using raw_column_ptrs_t = std::shared_ptr<raw_column_ptrs_abstract>;

class raw_immutable_column_ptrs_abstract;
using raw_immutable_column_ptrs_t = std::shared_ptr<raw_immutable_column_ptrs_abstract>;

namespace details {

template <col_pointer_type ENUM_VALUE>
struct col_ptr_enum_to_type;

template <>
struct col_ptr_enum_to_type<col_pointer_type::PTR_8> {
  using type = col_ptr_8_t;
};

template <>
struct col_ptr_enum_to_type<col_pointer_type::PTR_16> {
  using type = col_ptr_16_t;
};

template <>
struct col_ptr_enum_to_type<col_pointer_type::PTR_32> {
  using type = col_ptr_32_t;
};

template <>
struct col_ptr_enum_to_type<col_pointer_type::PTR_64> {
  using type = col_ptr_64_t;
};

template <col_pointer_type ENUM_VALUE>
using col_ptr_enum_to_type_t = typename col_ptr_enum_to_type<ENUM_VALUE>::type;

template <typename COL_PTR_TYPE>
[[nodiscard]] constexpr col_pointer_type col_ptr_type_to_enum() noexcept {
  if constexpr (std::is_same_v<COL_PTR_TYPE, col_ptr_8_t>) {
    return col_pointer_type::PTR_8;
  } else if constexpr (std::is_same_v<COL_PTR_TYPE, col_ptr_16_t>) {
    return col_pointer_type::PTR_16;
  } else if constexpr (std::is_same_v<COL_PTR_TYPE, col_ptr_32_t>) {
    return col_pointer_type::PTR_32;
  } else if constexpr (std::is_same_v<COL_PTR_TYPE, col_ptr_64_t>) {
    return col_pointer_type::PTR_64;
  } else {
    static_assert(ctl::always_false_v<COL_PTR_TYPE>, "Unknown column pointer type!");
  }
}

class column_ptrs_abstract_base {
 public:
  explicit column_ptrs_abstract_base(col_pointer_type type) noexcept : type{type} {}
  virtual ~column_ptrs_abstract_base() = default;

  [[nodiscard]] col_pointer_type get_type() const noexcept { return type; };
  [[nodiscard]] virtual size_t get_row_count() const = 0;
  [[nodiscard]] virtual raw_immutable_column_ptrs_t create_immutable_view(size_t offset, size_t size) const = 0;

 private:
  col_pointer_type type;
};

// TODO(l.karnowski) This class can probably be replaced by ctl::shared_static_array<const COL_PTR_TYPE>
template <typename COL_PTR_TYPE>
class const_column_ptrs_accessor {
 public:
  using type = COL_PTR_TYPE;

  explicit const_column_ptrs_accessor(ctl::shared_static_array<const COL_PTR_TYPE> row_ptr) noexcept
      : row_ptr_{std::move(row_ptr)} {}

  [[nodiscard]] const COL_PTR_TYPE& operator[](const size_t idx) const { return row_ptr_[idx]; }
  [[nodiscard]] const COL_PTR_TYPE& at(const size_t idx) const { return row_ptr_.at(idx); }

  [[nodiscard]] size_t size() const noexcept { return row_ptr_.size(); }

  [[nodiscard]] const COL_PTR_TYPE* get() const noexcept { return row_ptr_.get(); }

 private:
  ctl::shared_static_array<const COL_PTR_TYPE> row_ptr_;
};

}  // namespace details

class column_ptrs_abstract : public details::column_ptrs_abstract_base {
 public:
  template <class COL_PTRS_TYPE>
  using concrete_type = column_ptrs_impl<COL_PTRS_TYPE>;

  [[nodiscard]] virtual usage_time_t time_of_last_usage() const = 0;
  [[nodiscard]] virtual management::load_status get_load_status() const = 0;
  /**
   * return column pointer at position rid.
   *
   * This is about 3 times slower due the required vtable lookup/impossible inlining compared to
   * access via casted column pointers
   */
  [[nodiscard]] virtual row_id get_ptr_slow(row_id rid) const = 0;
  [[nodiscard]] virtual column_ptrs_abstract* clone(const std::string& id, const std::string& description,
                                                    common::execution_context& context) const = 0;
  [[nodiscard]] virtual std::shared_ptr<management::data_handler> get_abstract() = 0;
  [[nodiscard]] virtual size_t max_value() const = 0;

  explicit column_ptrs_abstract(col_pointer_type type) noexcept : column_ptrs_abstract_base{type} {}
};

using column_ptrs_t = std::shared_ptr<column_ptrs_abstract>;

template <class COL_PTRS_TYPE>
class column_ptrs_impl final : public column_ptrs_abstract {
 public:
  using value_type = COL_PTRS_TYPE;
  using ptrs_t = column_data_handler_t<COL_PTRS_TYPE>;
  using const_data_accessor_t = typename details::const_column_ptrs_accessor<COL_PTRS_TYPE>;

  explicit column_ptrs_impl(ptrs_t ptrs)
      : column_ptrs_abstract{details::col_ptr_type_to_enum<COL_PTRS_TYPE>()}, ptrs{std::move(ptrs)} {}

  friend column_ptrs_abstract;

  // column_ptrs_abstract_base interface
  [[nodiscard]] size_t get_row_count() const override { return ptrs->get_size(); }

  // column_ptrs_abstract interface
  [[nodiscard]] usage_time_t time_of_last_usage() const override { return ptrs->get_last_usage(); }
  [[nodiscard]] management::load_status get_load_status() const override { return ptrs->get_load_status(); }
  [[nodiscard]] row_id get_ptr_slow(row_id rid) const override { return ptrs->get_const_data()[rid]; }
  [[nodiscard]] column_ptrs_abstract* clone(const std::string& id, const std::string& description,
                                            common::execution_context& context) const override {
    auto row_count = ptrs->get_size();
    auto data{ctl::make_static_array_for_overwrite<COL_PTRS_TYPE>(row_count, ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};
    std::copy_n(ptrs->get_const_data(context).get(), row_count, data.get());
    auto raw_clone{management::raw_data_handler<COL_PTRS_TYPE>::create_data_handler(
        std::move(data), description + " " + management::COLUMN_PTR_DESC)};
    return new column_ptrs_impl<COL_PTRS_TYPE>(std::move(raw_clone));
  }
  [[nodiscard]] std::shared_ptr<management::data_handler> get_abstract() noexcept override { return ptrs; }
  [[nodiscard]] size_t max_value() const noexcept override { return std::numeric_limits<value_type>::max(); }
  [[nodiscard]] raw_immutable_column_ptrs_t create_immutable_view(size_t offset, size_t size) const override;

  // column_ptrs_impl_base interface
  [[nodiscard]] const_data_accessor_t get_const_accessor() const {
    return const_data_accessor_t{get()->get_const_data().shared()};
  }

  [[nodiscard]] memory::column_raw_data_t<const COL_PTRS_TYPE> get_const_data(
      const common::execution_context& context) const {
    return ptrs->get_const_data(context).shared();
  }

 private:
  [[nodiscard]] ptrs_t get() const noexcept { return ptrs; }

  ptrs_t ptrs;
};

class raw_column_ptrs_abstract : public details::column_ptrs_abstract_base {
 public:
  template <class COL_PTRS_TYPE>
  using concrete_type = raw_column_ptrs_impl<COL_PTRS_TYPE>;

  explicit raw_column_ptrs_abstract(col_pointer_type type) noexcept : column_ptrs_abstract_base{type} {}

  [[nodiscard]] virtual column_ptrs_t create_temp_column_pointer() = 0;

  [[nodiscard]] virtual raw_immutable_column_ptrs_t as_immutable() const = 0;
  [[nodiscard]] virtual raw_column_ptrs_t create_view(size_t offset, size_t size) const = 0;
};

template <typename COL_PTRS_TYPE>
class raw_column_ptrs_impl final : public raw_column_ptrs_abstract {
 public:
  using value_type = COL_PTRS_TYPE;
  using const_data_accessor_t = details::const_column_ptrs_accessor<COL_PTRS_TYPE>;

  raw_column_ptrs_impl(const row_id row_count, const zero_init_t initialize_to_0,
                       const common::execution_context& context)
      : raw_column_ptrs_abstract{details::col_ptr_type_to_enum<COL_PTRS_TYPE>()},
        data_{initialize_to_0.get() ? ctl::make_shared_static_array_value_init<COL_PTRS_TYPE>(
                                          row_count, ALLOC_MSG(ctl::TEMPORARY_COLUMN_MSG))
                                    : ctl::make_shared_static_array_for_overwrite<COL_PTRS_TYPE>(
                                          row_count, ALLOC_MSG(ctl::TEMPORARY_COLUMN_MSG))},
        context{context} {}

  friend raw_column_ptrs_abstract;

  // column_ptrs_abstract_base interface
  [[nodiscard]] size_t get_row_count() const noexcept override { return data_.size(); }

  [[nodiscard]] column_ptrs_t create_temp_column_pointer() override;
  [[nodiscard]] raw_immutable_column_ptrs_t as_immutable() const override;
  [[nodiscard]] raw_immutable_column_ptrs_t create_immutable_view(size_t offset, size_t size) const override;
  [[nodiscard]] raw_column_ptrs_t create_view(size_t offset, size_t size) const override;

  // column_ptrs_impl_base interface
  [[nodiscard]] const_data_accessor_t get_const_accessor() const noexcept {
    return const_data_accessor_t{get_const_data()};
  }
  // TODO(s.griebel) as this function returns reference to the mutable array this function should not be const
  [[nodiscard]] memory::column_raw_data_t<COL_PTRS_TYPE> get_data() const noexcept { return data_; }
  [[nodiscard]] memory::column_raw_data_t<const COL_PTRS_TYPE> get_const_data() const noexcept { return data_; }
  [[nodiscard]] memory::column_raw_data_t<COL_PTRS_TYPE> get_mut_data() noexcept { return data_; }

 private:
  // Construct from existing data array with custom offset
  raw_column_ptrs_impl(memory::column_raw_data_t<COL_PTRS_TYPE> data, const row_id offset, const row_id row_count,
                       const common::execution_context& context)
      : raw_column_ptrs_abstract{details::col_ptr_type_to_enum<COL_PTRS_TYPE>()},
        // aliasing constructor
        data_{data.sub_array(static_cast<size_t>(offset), static_cast<size_t>(row_count))},
        context{context} {
    debug_assert(0 <= offset);
  }

  memory::column_raw_data_t<COL_PTRS_TYPE> data_{};
  const common::execution_context& context;
};

class raw_immutable_column_ptrs_abstract : public details::column_ptrs_abstract_base {
 public:
  template <class COL_PTRS_TYPE>
  using concrete_type = raw_immutable_column_ptrs_impl<COL_PTRS_TYPE>;

  explicit raw_immutable_column_ptrs_abstract(col_pointer_type type) noexcept : column_ptrs_abstract_base{type} {}
};

template <typename COL_PTRS_TYPE>
class raw_immutable_column_ptrs_impl final : public raw_immutable_column_ptrs_abstract {
 public:
  using value_type = COL_PTRS_TYPE;
  using const_data_accessor_t = details::const_column_ptrs_accessor<COL_PTRS_TYPE>;

  raw_immutable_column_ptrs_impl(memory::column_raw_data_t<const COL_PTRS_TYPE> data, row_id offset, row_id row_count)
      : raw_immutable_column_ptrs_abstract{details::col_ptr_type_to_enum<COL_PTRS_TYPE>()},
        // Aliasing constructor
        data_{data.sub_array(static_cast<size_t>(offset), static_cast<size_t>(row_count))} {
    debug_assert(0 <= offset);
  }

  [[nodiscard]] size_t get_row_count() const noexcept override { return data_.size(); }

  [[nodiscard]] const_data_accessor_t get_const_accessor() const noexcept { return const_data_accessor_t{data_}; }
  [[nodiscard]] memory::column_raw_data_t<const COL_PTRS_TYPE> get_const_data() const noexcept { return data_; }

  [[nodiscard]] raw_immutable_column_ptrs_t create_immutable_view(size_t offset, size_t size) const override;

 private:
  memory::column_raw_data_t<const COL_PTRS_TYPE> data_{nullptr};
};

namespace details {

template <class ABSTRACT_PTR_TYPE, col_pointer_type PTR_SIZE>
struct get_ptr_impl_type_of_decayed {
  static_assert(!std::is_same_v<column_ptrs_t, std::remove_reference_t<ABSTRACT_PTR_TYPE>> &&
                    !std::is_same_v<raw_column_ptrs_t, std::remove_reference_t<ABSTRACT_PTR_TYPE>> &&
                    !std::is_same_v<raw_immutable_column_ptrs_t, std::remove_reference_t<ABSTRACT_PTR_TYPE>>,
                "cast_execute_column_pointers: Require column pointers, not shared_ptr of column pointers.");
  static_assert(!std::is_same_v<column_ptrs_abstract*, std::remove_reference_t<ABSTRACT_PTR_TYPE>> &&
                    !std::is_same_v<raw_column_ptrs_abstract*, std::remove_reference_t<ABSTRACT_PTR_TYPE>> &&
                    !std::is_same_v<raw_immutable_column_ptrs_abstract*, std::remove_reference_t<ABSTRACT_PTR_TYPE>>,
                "cast_execute_column_pointers: Require column pointers, not pointer to column pointers.");
  static_assert(ctl::always_false_v<ABSTRACT_PTR_TYPE>, "Unknown abstract pointer type!");
};

template <col_pointer_type PTR_SIZE>
struct get_ptr_impl_type_of_decayed<column_ptrs_abstract, PTR_SIZE> {
  using type = column_ptrs_impl<col_ptr_enum_to_type_t<PTR_SIZE>>;
};

template <col_pointer_type PTR_SIZE>
struct get_ptr_impl_type_of_decayed<raw_column_ptrs_abstract, PTR_SIZE> {
  using type = raw_column_ptrs_impl<col_ptr_enum_to_type_t<PTR_SIZE>>;
};

template <col_pointer_type PTR_SIZE>
struct get_ptr_impl_type_of_decayed<raw_immutable_column_ptrs_abstract, PTR_SIZE> {
  using type = raw_immutable_column_ptrs_impl<col_ptr_enum_to_type_t<PTR_SIZE>>;
};

template <typename ABSTRACT_PTR_TYPE, col_pointer_type PTR_SIZE>
using get_ptr_impl_type_of_decayed_t = typename get_ptr_impl_type_of_decayed<ABSTRACT_PTR_TYPE, PTR_SIZE>::type;

template <typename ABSTRACT_PTR_TYPE, col_pointer_type PTR_SIZE>
using get_ptr_impl_type =
    ctl::adopt_cvr<ABSTRACT_PTR_TYPE, get_ptr_impl_type_of_decayed_t<std::decay_t<ABSTRACT_PTR_TYPE>, PTR_SIZE>>;

template <class ABSTRACT_PTR_TYPE, col_pointer_type PTR_SIZE>
using get_ptr_impl_type_t = typename get_ptr_impl_type<ABSTRACT_PTR_TYPE, PTR_SIZE>::type;

template <typename FUNCTION, class TUPLE>
[[nodiscard]] decltype(auto) cast_execute_column_pointers_impl(FUNCTION&& f, TUPLE&& args_tuple) {
  return std::invoke(std::forward<FUNCTION>(f), std::forward<TUPLE>(args_tuple));
}

template <typename FUNCTION, class TUPLE, class FIRST_ARG, class... ARGS>
requires(std::is_base_of_v<column_ptrs_abstract_base, std::remove_cvref_t<FIRST_ARG>>) [[nodiscard]] decltype(auto)
    cast_execute_column_pointers_impl(FUNCTION&& f, TUPLE&& args_tuple, FIRST_ARG&& first_arg, ARGS&&... args) {
  switch (first_arg.get_type()) {
    case col_pointer_type::PTR_64: {
      if constexpr (COL_PTR_64_NEEDED) {
        using FIRST_PTR_IMPL = get_ptr_impl_type_t<FIRST_ARG&&, col_pointer_type::PTR_64>;
        auto new_args{
            std::tuple_cat(std::forward<TUPLE>(args_tuple),
                           std::forward_as_tuple(dynamic_cast<FIRST_PTR_IMPL>(std::forward<FIRST_ARG>(first_arg))))};
        return cast_execute_column_pointers_impl(std::forward<FUNCTION>(f), std::move(new_args),
                                                 std::forward<ARGS>(args)...);
      }
    }
    case col_pointer_type::PTR_32: {
      using FIRST_PTR_IMPL = get_ptr_impl_type_t<FIRST_ARG&&, col_pointer_type::PTR_32>;
      auto new_args{std::tuple_cat(std::forward<TUPLE>(args_tuple), std::forward_as_tuple(dynamic_cast<FIRST_PTR_IMPL>(
                                                                        std::forward<FIRST_ARG>(first_arg))))};
      return cast_execute_column_pointers_impl(std::forward<FUNCTION>(f), std::move(new_args),
                                               std::forward<ARGS>(args)...);
    }
    case col_pointer_type::PTR_16: {
      using FIRST_PTR_IMPL = get_ptr_impl_type_t<FIRST_ARG&&, col_pointer_type::PTR_16>;
      auto new_args{std::tuple_cat(std::forward<TUPLE>(args_tuple), std::forward_as_tuple(dynamic_cast<FIRST_PTR_IMPL>(
                                                                        std::forward<FIRST_ARG>(first_arg))))};
      return cast_execute_column_pointers_impl(std::forward<FUNCTION>(f), std::move(new_args),
                                               std::forward<ARGS>(args)...);
    }
    case col_pointer_type::PTR_8: {
      using FIRST_PTR_IMPL = get_ptr_impl_type_t<FIRST_ARG&&, col_pointer_type::PTR_8>;
      auto new_args{std::tuple_cat(
          args_tuple, std::forward_as_tuple(dynamic_cast<FIRST_PTR_IMPL>(std::forward<FIRST_ARG>(first_arg))))};
      return cast_execute_column_pointers_impl(std::forward<FUNCTION>(f), std::move(new_args),
                                               std::forward<ARGS>(args)...);
    }
    default:
      throw common::internal_exception{"Unknown column pointer type {}",
                                       ctl::enum_to_underlying_type(first_arg.get_type())};
  }
}

template <typename FUNCTION, class TUPLE, class FIRST_ARG, class... ARGS>
[[nodiscard]] decltype(auto) cast_execute_column_pointers_impl(FUNCTION&& f, TUPLE&& args_tuple, FIRST_ARG&& first_arg,
                                                               ARGS&&... args) {
  return std::visit(
      [&f, &args_tuple, &args...](auto&& proj) {
        auto new_args{std::tuple_cat(args_tuple, std::forward_as_tuple(std::forward<decltype(proj)>(proj)))};
        return cast_execute_column_pointers_impl(std::forward<FUNCTION>(f), std::move(new_args),
                                                 std::forward<ARGS>(args)...);
      },
      first_arg);
}

}  // namespace details

[[nodiscard]] size_t type_to_size(col_pointer_type type);

template <class FUNCTOR_TYPE, class... ARGS>
[[nodiscard]] decltype(auto) cast_execute_column_pointers(FUNCTOR_TYPE&& f, ARGS&&... args) {
  return details::cast_execute_column_pointers_impl(std::forward<FUNCTOR_TYPE>(f), std::tuple<>{},
                                                    std::forward<ARGS>(args)...);
}

template <typename FUNCTION>
[[nodiscard]] auto execute_with_column_pointers_type(FUNCTION&& f, const row_id dict_size_with_null) {
  if constexpr (COL_PTR_64_NEEDED) {
    if (dict_size_with_null - 1 > std::numeric_limits<col_ptr_32_t>::max()) {
      return f.template operator()<details::col_ptr_enum_to_type_t<col_pointer_type::PTR_64>>();
    }
  }
  if (dict_size_with_null - 1 > std::numeric_limits<col_ptr_16_t>::max()) {
    return f.template operator()<details::col_ptr_enum_to_type_t<col_pointer_type::PTR_32>>();
  }
  if (dict_size_with_null - 1 > std::numeric_limits<col_ptr_8_t>::max()) {
    return f.template operator()<details::col_ptr_enum_to_type_t<col_pointer_type::PTR_16>>();
  }
  return f.template operator()<details::col_ptr_enum_to_type_t<col_pointer_type::PTR_8>>();
}

/** Create column pointers based on raw column pointers. Only to be used "internally", don't use in operators
 */
[[nodiscard]] column_ptrs_t create_column_pointers(const raw_column_ptrs_t& raw_column_pointers,
                                                   const std::string& cache_id, const std::string& cache_description);

template <typename COL_PTR_TYPE>
[[nodiscard]] std::shared_ptr<raw_column_ptrs_impl<COL_PTR_TYPE>> create_raw_column_pointer(
    const row_id row_count, const zero_init_t initialize_to_0, const common::execution_context& context) {
  if (row_count < 0) {
    throw common::internal_exception{"No negative row count allowed but is [{}]", row_count};
  }
  return std::make_shared<raw_column_ptrs_impl<COL_PTR_TYPE>>(row_count, initialize_to_0, context);
}

template <typename COL_PTR_TYPE>
[[nodiscard]] raw_column_ptrs_t create_raw_column_pointer(std::initializer_list<COL_PTR_TYPE> data,
                                                          const common::execution_context& context) {
  auto result = create_raw_column_pointer<COL_PTR_TYPE>(static_cast<row_id>(data.size()), zero_init_t{false}, context);
  std::copy_n(data.begin(), data.size(), result->get_data().get());
  return result;
}

[[nodiscard]] raw_column_ptrs_t create_raw_column_pointer(row_id row_count, row_id dict_size_with_null,
                                                          zero_init_t initialize_to_0,
                                                          const common::execution_context& context);

[[nodiscard]] column_ptrs_t create_tmp_column_pointers(const raw_column_ptrs_t& raw_column_pointers);

}  // namespace celonis::accelerator::memory
