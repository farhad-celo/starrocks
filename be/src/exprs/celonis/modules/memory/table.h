#pragma once

#include <optional>
#include <shared_mutex>
#include <vector>

#include <ctl/conversion.h>
#include <ctl/static_array.h>

#include "modules/memory/column.h"
#include "modules/memory/table_fwd.h"
#include "modules/memory/table_meta_data.h"

namespace celonis::accelerator::memory {

// check rows is under both numeric_limits<row_id>::max() and table_row_limits
template <typename T>
[[nodiscard]] bool check_row_limit(T rows, int64_t table_row_limit) {
  return ctl::is_safe_to_cast<row_id>(rows) && std::cmp_less_equal(rows, table_row_limit);
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
  table(row_id rows, std::string name, std::string id, const management::swap_info& sinfo,
        const table_meta_data& meta_data, user_visible_table_name user_visible_name, table_row_limit_t table_row_limit);
  table(std::nullopt_t rows, std::string name, std::string id, const management::swap_info& sinfo,
        const table_meta_data& meta_data, user_visible_table_name user_visible_name);

  table(const table&) = delete;
  table& operator=(const table&) = delete;
  table(table&&) noexcept = delete;
  table& operator=(table&&) noexcept = delete;

  virtual ~table() = default;

  /**
   * @brief Returns the number of rows stored in this table. An exception is thrown in case the row count is unknown.
   */
  [[nodiscard]] row_id get_rows() const;

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

  /**
   * Adds an existing column to the table.
   * @note the given column won't be managed/owned by the table (in the sense that the columns 'owner' might refer to
   * another table)
   */
  void add_existing_column(const column_t& column, table_row_limit_t table_row_limit);

 private:
  std::optional<row_id> get_rows_if_known() const;

  row_id get_rows_intern() const;

  void verify_set_row_count_no_lock(row_id new_row_count, std::optional<table_row_limit_t> table_row_limit);

  std::vector<column_t> headers;

  std::optional<row_id> default_row_count;
  const std::string name;
  const std::string id;
  std::optional<user_visible_table_name> user_visible_name{std::nullopt};

  table_meta_data meta_data;

  const management::swap_info sinfo;

  mutable std::shared_timed_mutex table_mutex;
};

}  // namespace celonis::accelerator::memory
