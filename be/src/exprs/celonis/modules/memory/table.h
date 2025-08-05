#pragma once

#include <limits>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <vector>

#include "legacy_embedded_ctl/conversion.h"
#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/shared_types_fwd.h"
#ifndef CELOSTAR
#include "modules/memory/cache/data_table_cache.h"
#endif
#ifdef CELOSTAR
#include "modules/memory/column.h"
#endif
#include "modules/memory/column_fwd.h"
#include "modules/memory/dictionary_fwd.h"
#include "modules/memory/null_flags_fwd.h"
#include "modules/memory/table_fwd.h"
#include "modules/memory/table_info.h"
#include "modules/memory/table_meta_data.h"

namespace celonis::accelerator::memory {

using column_or_error_message_t = std::variant<column_t, std::string>;

/**
 * @brief A simple helper function that lets us append an operator name to the error message returned by
 * get_column_header.
 * @description This is defined as a free function to reduce the coupling between the table class and the respective
 * calling operator. In particular, we want to avoid adding an additional parameter to the get_column_header function
 * that holds the calling operators name.
 */
template <typename ERROR_TYPE>
column_t get_column_or_throw(const column_or_error_message_t& variant, const std::string& operator_name) {
  if (std::holds_alternative<std::string>(variant)) {
    throw ERROR_TYPE{"{}: {}", operator_name, std::get<std::string>(variant)};
  }

  legacy_embedded_debug_assert(std::holds_alternative<column_t>(variant));
  return std::get<column_t>(variant);
}

// check rows is under both numeric_limits<row_id>::max() and table_row_limits
template <typename T>
[[nodiscard]] bool check_row_limit(T rows, int64_t table_row_limit) {
  return legacy_embedded_ctl::is_safe_to_cast<row_id>(rows) && std::cmp_less_equal(rows, table_row_limit);
}

// get minimum of numeric_limits<row_id>::max() and table_row_limits
[[nodiscard]] inline int64_t get_row_limit(int64_t table_row_limit) {
  return std::min<int64_t>(std::numeric_limits<row_id>::max(), table_row_limit);
}

using optional_string_values_t = std::vector<std::optional<std::string>>;

/**
 * @brief A simple utility wrapper containing a table name which can be shown to the user.
 * @description Sometimes, there are internal tables (e.g., some operator tables, MEL tables, ...) which might have
 * rather cryptic names (e.g., with internal tags). For the sake of user-facing warning/error reporting, additionally to
 * the internal table name, one can define a user visible name which is shown in such cases.
 */
class user_visible_table_name {
 public:
  user_visible_table_name() = default;
  explicit user_visible_table_name(std::string name) : name(std::move(name)) {}
  [[nodiscard]] const std::string& get_name() const { return name; }

 private:
  std::string name;
};

/**
 * @brief The central data structure to represent tables in Saola DB.
 * @description This data structure is shared for all kinds of tables (e.g., regular DM tables, query scope tables or
 * augmentation tables). It is mainly composed by a set of columns and some additional meta data. If and how a table and
 * its columns can be swapped out to disk is controlled via a provided 'swap_info' object.
 * TODO(n.weber): Refactor in CPL-6233 (potentially before delta loading)
 */
class table {
 public:
  /** ctors, assignment operations and dtor */
  table(row_id rows, std::string name, std::string id, const std::string& version, const management::swap_info& sinfo,
        const table_meta_data& meta_data, table_row_limit_t table_row_limit);
  table(std::nullopt_t rows, std::string name, std::string id, const std::string& version,
        const management::swap_info& sinfo, const table_meta_data& meta_data);

  table(row_id rows, std::string name, std::string id, const management::swap_info& sinfo,
        const table_meta_data& meta_data, user_visible_table_name user_visible_name, table_row_limit_t table_row_limit);
  table(std::nullopt_t rows, std::string name, std::string id, const management::swap_info& sinfo,
        const table_meta_data& meta_data, user_visible_table_name user_visible_name);

  /**
   * @brief Ctor for temporary query scope tables
   */
  table(row_id rows, const std::string& name, table_row_limit_t table_row_limit);
  table(std::nullopt_t rows, const std::string& name);

  table(const table&) = delete;
  table& operator=(const table&) = delete;
  table(table&&) noexcept = delete;
  table& operator=(table&&) noexcept = delete;

  virtual ~table();

  /** getters */
  /**
   * @brief Returns the columns managed by this table
   */
  [[nodiscard]] std::vector<column_t> get_column_headers() const;

  /**
   * @brief Returns whether this table manages a column with the given column name
   */
  [[nodiscard]] bool has_column(std::string_view column_name) const;

  /**
   * @brief Returns the column at the given column index
   * @throws common::internal_exception when an invalid index was provided
   */
  [[nodiscard]] column_t get_column_header(row_id column_index) const;

  /**
   * @brief Returns the column with the given column name
   * @throws common::cpm_exception when an invalid column name was provided
   */
  [[nodiscard]] column_t get_column_header(std::string_view column_name,
                                           const common::execution_context& context) const;

  /**
   * @brief Returns the column with the given column name or an exception string whenever an error occurs
   */
  [[nodiscard]] column_or_error_message_t get_column_header_or_error_string(
      std::string_view column_name, const common::execution_context& context) const;

  /**
   * @brief If exactly one column name in the input list matches a column name in the table, the respective column
   * is returned.
   * @throws Whenever there is no match or there are multiple ones, a common::cpm_exception is thrown.
   */
  [[nodiscard]] column_t get_column_header(const std::initializer_list<std::string_view>& column_names,
                                           const common::execution_context& context) const;

  /**
   * @brief Returns the column with the given column name or an exception string whenever an error occurs
   */
  [[nodiscard]] column_or_error_message_t get_column_header_or_error_string(
      const std::initializer_list<std::string_view>& column_names, const common::execution_context& context) const;

  /**
   * @brief Returns the number of columns managed by this table
   */
  [[nodiscard]] cel_column_count_t get_columns() const;

  /**
   * @brief Returns the last time the table's 'data' was accessed (i.e., the last usage time of all its columns) or
   * std::nullopt if the table has no columns or all of them are not in use (i.e., are not loaded)
   */
  [[nodiscard]] std::optional<usage_time_t> time_of_last_usage() const;

  [[nodiscard]] bool is_row_count_set() const;

  /**
   * @brief Returns the number of rows stored in this table. An exception is thrown in case the row count is unknown.
   */
  [[nodiscard]] row_id get_rows() const;

  /**
   * @brief Returns the number of rows stored in this table if (already) known.
   */
  [[nodiscard]] std::optional<row_id> get_rows_optional() const;

  /**
   * @brief Returns the table's internal name
   */
  [[nodiscard]] const std::string& get_name() const noexcept { return name; }

  [[nodiscard]] const std::string& get_id() const noexcept { return id; }

  /**
   * @brief Returns the table's user visible name
   */
  [[nodiscard]] std::string get_user_visible_name(const common::execution_context& context, bool bounds = true) const;

  [[nodiscard]] const table_meta_data& get_meta_data() const noexcept { return meta_data; }

  [[nodiscard]] const management::swap_info& get_swap_info() const noexcept { return sinfo; }

#ifndef CELOSTAR
  [[nodiscard]] cache::data_table_cache& get_table_cache() noexcept { return cache; }
#endif

  /**
   * @brief Returns whether this table is a query scope (i.e., temporary) table
   */
  [[nodiscard]] bool is_query_scope() const noexcept;

  /**
   * @brief Returns whether this table contains a constant result (i.e., is a query scope result table with one row)
   */
  [[nodiscard]] virtual bool is_constant_result() const;

  /* setters */

  /**
   * @brief Set the table's default row count if not already set
   */
  void verify_set_row_count(row_id new_row_count, std::optional<table_row_limit_t> table_row_limit);

  /**
   * @brief Clears/invalidates the tables cache
   * @note Only meant to be used for error handling (i.e., roll-backs) and benchmarking
   */
  void clear_cache();

  /** add column functionalities */
  column_t add_column_with_data(data_type type, const col_name& column_name, const col_id& column_id, row_id row_count,
                                const std::shared_ptr<materialized_data>& plain_data,
                                const column_processing_state& state, table_row_limit_t table_row_limit);

  column_t add_empty_column(data_type type, const col_name& column_name, const col_id& column_id,
                            const col_cache_key& cache_key, row_id row_count, const column_processing_state& state,
                            const std::shared_ptr<column_loading::column_loader>& column_load,
                            table_row_limit_t table_row_limit);

  column_t add_empty_column(data_type type, const col_name& column_name, const col_id& column_id, row_id row_count,
                            const column_processing_state& state,
                            const std::shared_ptr<column_loading::column_loader>& column_load,
                            table_row_limit_t table_row_limit);

  template <typename T>
  column_t add_empty_column(const col_name& column_name, const col_id& column_id, row_id row_count,
                            const column_processing_state& state,
                            const std::shared_ptr<column_loading::column_loader>& column_load,
                            const table_row_limit_t table_row_limit) {
    return add_empty_column(get_matching_data_type<T>(), column_name, column_id, row_count, state, column_load,
                            table_row_limit);
  }

  column_t add_column_with_dictified_data(data_type type, const col_name& column_name, const col_id& column_id,
                                          const col_cache_key& cache_key, const column_ptrs_t& column_pointers,
                                          const std::shared_ptr<dictionary>& dict, table_row_limit_t table_row_limit);

  column_t add_column_with_dictified_data(const column_loading::column_config& col_config,
                                          const column_loading::dictified_column_data& dictified_data,
                                          table_row_limit_t table_row_limit);

  column_t add_column_with_dictified_data(data_type type, const col_name& column_name, const col_id& column_id,
                                          const col_cache_key& cache_key, const column_ptrs_t& column_pointers,
                                          const std::shared_ptr<dictionary>& dict,
                                          const column_processing_state& processing_state,
                                          table_row_limit_t table_row_limit);

#ifndef CELOSTAR
  column_t add_column_from_swap(data_type type, const col_name& column_name, const col_id& column_id,
                                const column_processing_state& state, table_row_limit_t table_row_limit);
#endif

  /**
   * Adds an existing column to the table.
   * @note the given column won't be managed/owned by the table (in the sense that the columns 'owner' might refer to
   * another table)
   */
  void add_existing_column(const column_t& column, table_row_limit_t table_row_limit);

  column_t add_column_by_blueprint(const col_name& column_name, const col_cache_key& cache_key,
                                   const column_t& blueprint, const table_row_limit_t table_row_limit,
                                   common::execution_context& context) {
    return add_column_with_dictified_data(blueprint->get_data_type(), column_name, col_id(column_name.val), cache_key,
                                          blueprint->get_column_ptr_handle(context), blueprint->get_dict(context),
                                          blueprint->get_processing_state(), table_row_limit);
  }

  /**
   * @brief Adds a new column based on a blueprint column to the table. The data type and dictionary of the new column
   * is based on the given blueprint column.
   */
  column_t add_column_by_blueprint(const col_name& column_name, const col_cache_key& cache_key,
                                   const column_t& blueprint, const raw_column_ptrs_t& new_column_pointers,
                                   const column_processing_state& state, table_row_limit_t table_row_limit,
                                   common::execution_context& context);

  column_t add_column_by_blueprint(const col_name& column_name, const col_cache_key& cache_key,
                                   const column_t& blueprint, const raw_column_ptrs_t& column_pointer,
                                   const table_row_limit_t table_row_limit, common::execution_context& context) {
    return add_column_by_blueprint(column_name, cache_key, blueprint, column_pointer, blueprint->processing_state_,
                                   table_row_limit, context);
  }

  template <typename T>
  column_t add_column(const col_name& column_name, const col_id& column_id, const legacy_embedded_ctl::shared_static_array<T>& data,
                      const null_flags_t& null_flags, const column_processing_state& state,
                      const table_row_limit_t table_row_limit) {
    // Use static_assert instead of enable_if to get a more helpful compile error
    // compared to a candidate error due to substitution failure
    static_assert(!std::is_same<cel_string_t, T>(),
                  "table::add_column does not support cel_string_t. Use table::add_string_column instead.");
    std::shared_ptr<materialized_typed_data<T>> plain_data = materialized_typed_data<T>::init_materialized_data(
        column_id.val, get_swap_info(), create_column_description(column_name), data.size(), data, null_flags);

    return add_column_with_data(get_matching_data_type<T>(), column_name, column_id, data.size(), std::move(plain_data),
                                state, table_row_limit);
  }

  template <typename T>
  column_t add_column(const col_name& column_name, const col_id& column_id, legacy_embedded_ctl::static_array<T> data,
                      const null_flags_t& null_flags, const column_processing_state& state,
                      const table_row_limit_t table_row_limit) {
    // Use static_assert instead of enable_if to get a more helpful compile error
    // compared to a candidate error due to substitution failure
    static_assert(!std::is_same<cel_string_t, T>(),
                  "table::add_column does not support cel_string_t. Use table::add_string_column instead.");
    const auto data_size{data.size()};
    std::shared_ptr<materialized_typed_data<T>> plain_data = materialized_typed_data<T>::init_materialized_data(
        column_id.val, get_swap_info(), create_column_description(column_name), data_size, std::move(data), null_flags);

    return add_column_with_data(get_matching_data_type<T>(), column_name, column_id, data_size, std::move(plain_data),
                                state, table_row_limit);
  }

  /* Same as above but with the option to pass a cache key */
  // TODO(n.weber): The interfaces to add a column should be consolidated and many can probably be removed
  template <typename T>
  [[nodiscard]] column_t add_column(const col_name& column_name, const col_id& column_id,
                                    const col_cache_key& column_cache_key, legacy_embedded_ctl::static_array<T> data,
                                    const null_flags_t& null_flags, const column_processing_state& state,
                                    const table_row_limit_t table_row_limit) {
    auto col{add_column<T>(column_name, column_id, std::move(data), null_flags, state, table_row_limit)};
    legacy_embedded_debug_assert(col->config_.cache_key.empty());
    col->config_.cache_key = column_cache_key.val;
    return col;
  }

  column_t add_string_column(const col_name& column_name, const col_id& column_id, legacy_embedded_ctl::static_array<cel_string_t> ptrs,
                             legacy_embedded_ctl::static_array<char> string_bfr, const null_flags_t& null_flags,
                             table_row_limit_t table_row_limit);

  /* Same as above but with the option to pass a cache key */
  [[nodiscard]] column_t add_string_column(const col_name& column_name, const col_id& column_id,
                                           const col_cache_key& column_cache_key, legacy_embedded_ctl::static_array<cel_string_t> ptrs,
                                           legacy_embedded_ctl::static_array<char> string_bfr, const null_flags_t& null_flags,
                                           const table_row_limit_t table_row_limit) {
    auto col{
        add_string_column(column_name, column_id, std::move(ptrs), std::move(string_bfr), null_flags, table_row_limit)};
    legacy_embedded_debug_assert(col->config_.cache_key.empty());
    col->config_.cache_key = column_cache_key.val;
    return col;
  }
  /**
   * Adds a string column from a list of string values.
   * @param column_name Name of the column to be created.
   * @param column_id Id of the column to be created.
   * @param values List of nullable string values. If no value is given the value is assumed to be NULL.
   * @param table_row_limit Allowed maximum row count of tables.
   * @param context Execution context.
   * @return The constructed column.
   */
  column_t add_string_column(const col_name& column_name, const col_id& column_id,
                             const optional_string_values_t& values, table_row_limit_t table_row_limit,
                             const common::execution_context& context);

  /* others */
  /**
   * @brief Creates a temporary query scope table with the given number of rows and table name
   */
  static table_t create_query_scope_table(std::nullopt_t rows, const std::string& name);
  static table_t create_query_scope_table(row_id rows, const std::string& name,
                                          table_row_limit_t table_row_limit = MAX_TABLE_ROW_LIMIT);

  void deregister();

  [[nodiscard]] std::string create_column_description(const col_name& column_name) const;

  /**
   * For  full description see the cube version of this function
   *
   * This function is only meant for testing. It is not safe to be used in production!
   */
  void check_consistency_for_testing(const std::unordered_set<table*>& tables) const;

  void set_remove_swap_files_on_destruct() { remove_swap_files_on_destruct_.store(true, std::memory_order_relaxed); }

 protected:
  table(row_id rows, const std::string& name, table_meta_data meta_data, table_row_limit_t table_row_limit);
  table(std::nullopt_t rows, const std::string& name, table_meta_data meta_data);

 private:
  std::optional<row_id> get_rows_if_known() const;

  row_id get_rows_intern() const;

  void verify_set_row_count_no_lock(row_id new_row_count, std::optional<table_row_limit_t> table_row_limit);

  column_or_error_message_t get_column_header_not_locked(std::string_view column_name,
                                                         const common::execution_context& context) const;

  column_t add_column(column_t&& column, memory::table_row_limit_t table_row_limit);

  column_t create_column_with_data(data_type type, const col_name& column_name, const col_id& column_id,
                                   row_id row_count, const std::shared_ptr<materialized_data>& plain_data,
                                   const column_processing_state& state);

  column_t create_empty_column(data_type type, const col_name& column_name, const col_id& column_id,
                               const col_cache_key& cache_key, row_id row_count, const column_processing_state& state,
                               const std::shared_ptr<column_loading::column_loader>& column_load);

  column_t create_column_with_dictified_data(data_type type, const col_name& column_name, const col_id& column_id,
                                             const col_cache_key& cache_key, const column_ptrs_t& column_pointers,
                                             const std::shared_ptr<dictionary>& dict,
                                             const column_processing_state& processing_state);

  column_t create_column_from_swap(data_type type, const col_name& column_name, const col_id& column_id,
                                   const column_processing_state& state);

  std::vector<column_t> headers;

  std::optional<row_id> default_row_count;
  const std::string name;
  const std::string id;
  std::optional<user_visible_table_name> user_visible_name{std::nullopt};
  std::atomic<bool> remove_swap_files_on_destruct_{false};

  table_meta_data meta_data;

  const management::swap_info sinfo;

#ifndef CELOSTAR
  cache::data_table_cache cache;
#endif

  mutable std::shared_timed_mutex table_mutex;
};

}  // namespace celonis::accelerator::memory
