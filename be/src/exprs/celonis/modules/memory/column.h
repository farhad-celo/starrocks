#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_set>
#include <utility>

#include "column_fwd.h"  // IWYU pragma: export
#include "concurrency/concurrency_utils.h"
#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/named_type.h"
#include "modules/common/execution_context.h"
#ifndef CELOSTAR
#include "modules/cube/query_scope_fwd.h"
#endif
#include "modules/memory/column_info.h"
#include "modules/memory/column_loading/column_loader.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/column_processing_state.h"
#include "modules/memory/management/raw_data_handler.h"
#include "modules/memory/management/swap_info_fwd.h"
#include "modules/memory/materialized_data_fwd.h"
#include "modules/memory/null_flags_fwd.h"
#include "modules/memory/table_fwd.h"
#include "modules/memory/tracking/dynamic_bitset_with_context_tracking.h"
#include "modules/memory/typed_dictionary.h"
#include "modules/memory/types.h"
#include "modules/operators/framework/cached_operator_fwd.h"
#include "modules/operators/framework/dictify_inputs.h"

namespace celonis::accelerator::memory {
namespace builders {
class scalar_column_builder;
class temp_column_builder;
}  // namespace builders
namespace cache {
class column_register;
class data_table_cache;
}  // namespace cache
}  // namespace celonis::accelerator::memory

namespace celonis::accelerator::cube {
class event_table_calculation;
}

namespace celonis::accelerator::memory {

/** Column ID strong type */
struct col_id {
  explicit col_id(std::string id) : val(std::move(id)) {}

  const std::string val;
};

/** Column name strong type */
struct col_name {
  explicit col_name(std::string name) : val(std::move(name)) {}

  const std::string val;
};

/** Column cache key strong type */
struct col_cache_key {
  explicit col_cache_key(std::string cache_key) : val(std::move(cache_key)) {}

  const std::string val;
};

/**
 * @brief The central data structure to represent columns in Saola DB.
 *
 * @description A column can be in one of three states:
 * 1. Missing - The column is not loaded from the source (i.e., parquet) yet.
 * 2. Materialized - The data is fully loaded and stored without any compression (e.g., dictionaries) in an array.
 * 3. Dictified - The data is fully loaded and dictionary compressed (to reduce the memory overhead).
 * If a column is accessed and in the state 'Missing' it is loaded from disk. Otherwise, or after the load, the column
 * is either materialized or dictified. When the dictionary of a materialized column is accessed, it will be dictified
 * implicitly and transparently for the caller.
 * @note The current column state can be obtained via methods provided in its interface. However, one can not guarantee
 * that this status doesn't change immediately. If an (operator) implementation requires a materialized column, you can
 * request the materialized data (method provided in the interface) and check that it is not null.
 * TODO(n.weber): This class desperately needs a clean up. Maybe feasible as part of delta loading. CPL-6234
 */
class column {
  friend class table;
  friend class builders::scalar_column_builder;
  friend class builders::temp_column_builder;
  friend class cache::column_register;
  friend class cache::data_table_cache;
  friend class cube::event_table_calculation;

  using no_dictify_request_t = std::optional<operators::no_dictify_request>;

 public:
  struct column_data_missing {};
  static constexpr const char* COLUMN_DICTIFY_KEY = "COLUMN_DICTIFY";

  std::string get_string_value(row_id row, const common::execution_context& context = {});
  std::optional<std::string> get_string_value_opt(row_id row, const common::execution_context& context = {});

  /* Type information about the column */
  data_type get_data_type() const { return config_.type; }
  bool is_cel_string_type() const { return get_data_type() == data_type::cel_string; }
  bool is_cel_int_type() const { return get_data_type() == data_type::cel_int; }
  bool is_cel_float_type() const { return get_data_type() == data_type::cel_float; }
  bool is_cel_date_type() const { return get_data_type() == data_type::cel_date; }
  bool is_cel_boolean_type() const { return get_data_type() == data_type::cel_boolean; }

  row_id get_row_count(const common::execution_context& context = {});

  std::shared_ptr<dictionary> get_dict(const common::execution_context& context,
                                       const no_dictify_request_t& no_dictify_request = std::nullopt) {
    check_implicit_dictification(no_dictify_request);
    dictify_if_needed(context);
    return dict_;
  }

  /** Returns a raw pointer to the table which owns this column */
  table* get_owner() const { return owner_; }

  const table* get_owner_after_pull_up() const { return owner_after_pull_up_; }

  /** Returns whether this column has the given table in its list of dependencies */
  [[nodiscard]] bool has_dependency(const table* table) const;

  /** Returns whether this column has any of the given tables in its list of dependencies */
  [[nodiscard]] bool has_some_dependency(const std::unordered_set<memory::table*>& tables) const;

  /** Returns the list of all dependencies this column has */
  [[nodiscard]] const std::unordered_set<memory::table*>& get_dependencies() const noexcept { return dependencies_; }

  void set_dependencies(const operators::operator_input_columns_t& input_columns);

  /* Trivial getters */
  std::string get_id() const { return config_.id; }
  const std::string& get_name() const noexcept { return config_.name; }
  const std::string& get_cache_key() const noexcept { return config_.cache_key; }
  std::string get_description() const { return config_.description; }
  std::string get_user_visible_name(const common::execution_context& context);
  std::string get_user_visible_owner_name(const common::execution_context& context) const;
  [[nodiscard]] const management::swap_info& get_swap_info() const noexcept { return config_.swap_information; }

  /** Returns whether the swap file of this column is broken */
  bool swap_file_broken() const {
    std::shared_lock lock{column_mutex_};
    auto local_materialized_data = plain_data_;
    if (local_materialized_data != nullptr && local_materialized_data->swap_file_broken()) {
      return true;
    }
    if (column_pointers_ != nullptr && column_pointers_->get_abstract()->swap_file_broken()) {
      return true;
    }
    if (dict_ != nullptr && dict_->swap_file_broken()) {
      return true;
    }
    return false;
  }

  const column_processing_state& get_processing_state() const noexcept { return processing_state_; }

  /* Swapping */
  void swap_in(common::execution_context& context);
  #ifndef CELOSTAR
  void swap_out(common::execution_context& context);
  bool write_out(common::execution_context& context);

  // Swap out if the column was swapped in or created for the current query
  void swap_out_transaction(common::execution_context& context, std::chrono::steady_clock::time_point query_start);
  #endif
  /**
   * A column is at least partially swapped out if it is materialized and swapped out or if it is dictified and
   * the column pointers and/or the dictionary is swapped out.
   */
  [[nodiscard]] bool is_at_least_partially_swapped_out() const;

  /**
   * Create an alias of the column. This is not required for calculations but for managing multiple selections on the
   * same domain as well as naming the selections. Alias columns are only intended to be used as part of a query session
   */
  column_t alias_column(const std::string& alias, const std::string& cache_key, const std::string& format,
                        common::execution_context& context);

  /** Update the null flags given as an input with the nulls of the current column */
  void project_null_flags(null_flags_bitset_t& null_flags, const common::execution_context& context);

  /** Returns the number of null values stored in this column */
  row_id get_null_value_count(const common::execution_context& context);

  template <typename ACCESSOR>
  decltype(auto) access_column_data_under_lock(ACCESSOR&& accessor) {
    const std::shared_lock lck{column_mutex_};
    switch (status_.load()) {
      case column_loading::column_status::MISSING:
        return accessor(column_data_missing{});
      case column_loading::column_status::MATERIALIZED: {
        switch (config_.type) {
          case data_type::cel_string:
            return accessor(std::static_pointer_cast<materialized_typed_data<cel_string_t>>(plain_data_));
          case data_type::cel_int:
            return accessor(std::static_pointer_cast<materialized_typed_data<cel_int_t>>(plain_data_));
          case data_type::cel_float:
            return accessor(std::static_pointer_cast<materialized_typed_data<cel_float_t>>(plain_data_));
          case data_type::cel_date:
            return accessor(std::static_pointer_cast<materialized_typed_data<cel_date_t>>(plain_data_));
          case data_type::cel_boolean:
            return accessor(std::static_pointer_cast<materialized_typed_data<cel_boolean_t>>(plain_data_));
          case data_type::cel_uuid:
            return accessor(std::static_pointer_cast<materialized_typed_data<cel_uuid_t>>(plain_data_));
          case data_type::cel_null:
            return accessor(std::static_pointer_cast<materialized_typed_data<cel_null_t>>(plain_data_));
        }
#ifndef __clang__
        legacy_embedded_ctl::assert_unreachable();
#endif
      }
      case column_loading::column_status::DICTIFIED: {
        switch (config_.type) {
          case data_type::cel_string:
            return accessor(column_pointers_, std::static_pointer_cast<string_dictionary>(dict_));
          case data_type::cel_int:
            return accessor(column_pointers_, std::static_pointer_cast<int_dictionary>(dict_));
          case data_type::cel_float:
            return accessor(column_pointers_, std::static_pointer_cast<float_dictionary>(dict_));
          case data_type::cel_date:
            return accessor(column_pointers_, std::static_pointer_cast<date_dictionary>(dict_));
          case data_type::cel_boolean:
            return accessor(column_pointers_, std::static_pointer_cast<boolean_dictionary>(dict_));
          case data_type::cel_uuid:
            return accessor(column_pointers_, std::static_pointer_cast<uuid_dictionary>(dict_));
          case data_type::cel_null:
            return accessor(column_pointers_, std::static_pointer_cast<null_dictionary>(dict_));
        }
      }
    }
#ifndef __clang__
    legacy_embedded_ctl::assert_unreachable();
#endif
  }

  /** Returns null if column is not in materialized state. */
  template <typename T>
  std::shared_ptr<materialized_typed_data<T>> get_materialized_typed(const common::execution_context& context = {}) {
    if (config_.type != get_matching_data_type<T>()) {
      throw common::internal_exception{"Column [{}] does not have expected type. Expected [{}], but got [{}].",
                                       get_user_visible_name(context), convert_to_string(get_matching_data_type<T>()),
                                       convert_to_string(config_.type)};
    }

    load_if_missing(context);

    auto wait_span{context.get_span().start_child_span("get_materialized_typed_wait_for_lock", {})};
    const auto lck{concurrency::lock_shared_with_logging(column_mutex_, LOCK_LOGGING_THRESHOLD)};
    wait_span.finish_span();

    if (status_ != column_loading::column_status::MATERIALIZED) {
      return std::shared_ptr<materialized_typed_data<T>>();
    }

    return std::static_pointer_cast<materialized_typed_data<T>>(plain_data_);
  }

  /* Functions for dictified columns (If column is not dictified they will trigger change to dictified column) */
  template <typename T>
  std::shared_ptr<typed_dictionary<T>> get_typed_dict(const common::execution_context& context,
                                                      const no_dictify_request_t& no_dictify_request = std::nullopt) {
    if (config_.type != get_matching_data_type<T>()) {
      throw common::internal_exception{"Column [{}] does not have expected type. Expected [{}], but got [{}].",
                                       get_user_visible_name(context), convert_to_string(get_matching_data_type<T>()),
                                       convert_to_string(config_.type)};
    }
    return std::static_pointer_cast<typed_dictionary<T>>(get_dict(context, no_dictify_request));
  }

  std::shared_ptr<string_dictionary> get_string_dict(const common::execution_context& context,
                                                     const no_dictify_request_t& no_dictify_request = std::nullopt) {
    if (config_.type != data_type::cel_string) {
      throw common::internal_exception{"Column [{}] expected to be of type STRING, but got [{}].",
                                       get_user_visible_name(context), convert_to_string(config_.type)};
    }
    return std::static_pointer_cast<string_dictionary>(get_dict(context, no_dictify_request));
  }

  std::shared_ptr<int_dictionary> get_int_dict(const common::execution_context& context,
                                               const no_dictify_request_t& no_dictify_request = std::nullopt) {
    if (config_.type != data_type::cel_int) {
      throw common::internal_exception{"Column [{}] expected to be of type INT, but got [{}].",
                                       get_user_visible_name(context), convert_to_string(config_.type)};
    }
    return std::static_pointer_cast<int_dictionary>(get_dict(context, no_dictify_request));
  }

  std::shared_ptr<float_dictionary> get_float_dict(const common::execution_context& context,
                                                   const no_dictify_request_t& no_dictify_request = std::nullopt) {
    if (config_.type != data_type::cel_float) {
      throw common::internal_exception{"Column [{}] expected to be of type FLOAT, but got [{}].",
                                       get_user_visible_name(context), convert_to_string(config_.type)};
    }
    return std::static_pointer_cast<float_dictionary>(get_dict(context, no_dictify_request));
  }

  std::shared_ptr<date_dictionary> get_date_dict(const common::execution_context& context,
                                                 const no_dictify_request_t& no_dictify_request = std::nullopt) {
    if (config_.type != data_type::cel_date) {
      throw common::internal_exception{"Column [{}] expected to be of type DATE, but got [{}].",
                                       get_user_visible_name(context), convert_to_string(config_.type)};
    }
    return std::static_pointer_cast<date_dictionary>(get_dict(context, no_dictify_request));
  }

  std::shared_ptr<boolean_dictionary> get_boolean_dict(const common::execution_context& context,
                                                       const no_dictify_request_t& no_dictify_request = std::nullopt) {
    if (config_.type != data_type::cel_boolean) {
      throw common::internal_exception{"Column [{}] expected to be of type BOOLEAN, but got [{}].",
                                       get_user_visible_name(context), convert_to_string(config_.type)};
    }
    return std::static_pointer_cast<boolean_dictionary>(get_dict(context, no_dictify_request));
  }

  /** Returns the dictionary's size (i.e., the number of distinct values in this column) */
  row_id get_domain_count(const common::execution_context& context = {},
                          const no_dictify_request_t& no_dictify_request = std::nullopt);

  const column_ptrs_abstract& get_column_pointers(const common::execution_context& context,
                                                  const no_dictify_request_t& no_dictify_request = std::nullopt) {
    check_implicit_dictification(no_dictify_request);
    dictify_if_needed(context);
    column_pointers_->get_abstract()->swap_in(context);
    return *column_pointers_.get();
  }

  column_ptrs_t get_column_ptr_handle(const common::execution_context& context,
                                      const no_dictify_request_t& no_dictify_request = std::nullopt) {
    check_implicit_dictification(no_dictify_request);
    dictify_if_needed(context);
    return column_pointers_;
  }

  column_info dump_header() const;

  bool has_domain_null(const common::execution_context& context = {});

  column_t get_domain_column(common::execution_context& context);

  /**
   * Returns a nullptr if column is not materialized.
   * Do not rely on checking the status and expect the column to stay materialized.
   * Another thread can cause the column to become dictified.
   *
   * The correct way is to get the materialized data object and check if it is not null.
   */
  std::shared_ptr<materialized_data> get_materialized_data(const common::execution_context& context = {});

  /** Methods to request the status the column is currently in */
  column_loading::column_status get_column_status() const { return status_; }
  [[nodiscard]] bool is_missing() const { return get_column_status() == column_loading::column_status::MISSING; }
  bool is_dictified() const { return get_column_status() == column_loading::column_status::DICTIFIED; }
  bool is_materialized() const { return get_column_status() == column_loading::column_status::MATERIALIZED; }

  /**
   * @brief Returns the last time the column's 'data' was accessed (i.e., the last usage time of its data handlers) or
   * std::nullopt if the column is unused (i.e., is not loaded)
   */
  [[nodiscard]] std::optional<usage_time_t> time_of_last_usage() const;

  /// Checks if a column is a scalar column, i.e. only has one value and is intended to be used as a constant
  /// (note: e.g. a datamodel column with only one value is not considered a scalar column)
  [[nodiscard]] bool is_scalar_value();

  /**
   * Returns the previous column pointers.
   */
  column_ptrs_t set_and_register_column_pointer(const column_ptrs_t& new_column_pointers,
                                                const common::execution_context& context);

  /**
   * This function currently can not differentiate between a typed integer null constant (which can appear as the
   * output of an operator) and the untyped NULL constant, which can be used in queries.
   * Operators that deal with NULL have to handle both cases the same.
   */
  [[nodiscard]] bool maybe_untyped_null_constant();

  [[nodiscard]] bool is_constant() {
    bool no_owner = get_owner() == nullptr;
    legacy_embedded_debug_assert(!no_owner || get_row_count() == 1);
    return no_owner;
  }

  void load_if_missing(const common::execution_context& context);

  /**
   * For  full description see the cube version of this function
   *
   * This function is only meant for testing. It is not safe to be used in production!
   */
  void check_consistency_for_testing(const std::unordered_set<table*>& tables) const;

  void set_remove_swap_files_on_destruct() { remove_swap_files_on_destruct_.store(true, std::memory_order_relaxed); }

  column(column&&) = delete;
  column(const column&) = delete;
  column& operator=(column&&) = delete;
  column& operator=(const column&) = delete;

  ~column();

 private:
  void dictify_if_needed(const common::execution_context& context);

  /** Check whether dictification is triggered implicitly */
  void check_implicit_dictification(const no_dictify_request_t& no_dictify_request) const {
    // enable/disable implicit dictification checking
    if (constexpr bool enable_check{false}; enable_check && no_dictify_request) {
      legacy_embedded_warning_assert(is_dictified(),
                     fmt::format("Implicit dictification: {}", no_dictify_request->get_source_location()));
    }
  }

  column(column_loading::column_config config, table* owner, const table* owner_after_pull_up,
         column_ptrs_t column_pointers, std::shared_ptr<dictionary> dict, std::shared_ptr<materialized_data> plain_data,
         column_loading::column_status status, std::shared_ptr<management::managed_memory_group> managed_group,
         column_processing_state processing_state = column_processing_state(),
         std::shared_ptr<column_loading::column_loader> column_load =
             std::shared_ptr<column_loading::column_loader>(nullptr))
      : config_(std::move(config)),
        owner_(owner),
        owner_after_pull_up_(owner_after_pull_up),
        column_pointers_(std::move(column_pointers)),
        dict_(std::move(dict)),
        plain_data_(std::move(plain_data)),
        status_(status),
        managed_group_(std::move(managed_group)),
        processing_state_(std::move(processing_state)),
        column_load_(std::move(column_load)) {
    legacy_embedded_ctl::abort_assert(config_.row_count >= 0);
  }

  /**
   * Returns a pair of the previous column pointers and previous dictionary.
   */
  std::pair<column_ptrs_t, std::shared_ptr<dictionary>> set_and_register_dictified_data(
      const column_ptrs_t& new_column_pointers, const std::shared_ptr<dictionary>& new_dictionary,
      const common::execution_context& context);

  // Note: this method is not protected by locks and needs to be called in a scope in which column_mutex is locked
  void register_to_managed_group(const common::execution_context& context = {});

  void deregister_from_managed_group();

  // Note: this method is not protected by locks and needs to be called in a scope in which column_mutex is locked
  void erase_column();

  [[nodiscard]] bool belongs_to_augmentation_table() const noexcept;

  column_loading::column_config config_;
  table* owner_;
  const table* owner_after_pull_up_{nullptr};
  std::unordered_set<table*> dependencies_;
  // we store a reference to the owner - we need this in order to navigate in the object graph
  // the mutex is used for making swapping operations exclusive.
  // const row_id row_count;
  // data is either dictified or ...
  column_ptrs_t column_pointers_;
  std::shared_ptr<dictionary> dict_;
  // plain, but never both to save memory
  std::shared_ptr<materialized_data> plain_data_;

  std::atomic<column_loading::column_status> status_;

  std::shared_ptr<management::managed_memory_group> managed_group_;
  mutable std::shared_mutex column_mutex_;
  const column_processing_state processing_state_;
  std::shared_ptr<column_loading::column_loader> column_load_;

  // don't access directly, use has_domain_null()
  std::atomic<std::optional<bool>> has_null_{std::nullopt};

  std::atomic<bool> remove_swap_files_on_destruct_{false};

  static constexpr auto LOCK_LOGGING_THRESHOLD = std::chrono::seconds{1200};
};

legacy_embedded_ctl::dynamic_bitset<> get_null_flags_copy(const column_t& column, const common::execution_context& context);

}  // namespace celonis::accelerator::memory
