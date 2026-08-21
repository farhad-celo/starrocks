#include "table.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <utility>

#include <boost/algorithm/string/predicate.hpp>
#include <fmt/format.h>

#include "concurrency/concurrency_utils.h"
#include "legacy_embedded_format/json/json.h"
#include "log/log.h"
#include "modules/memory/column.h"
#include "modules/memory/column_pointers.h"

namespace celonis::accelerator::memory {

namespace {

/**
 * @brief Verifies the table's meta data validity in relation to its swap info
 */
void verify_table_setting(const table& tbl) {
  const auto& tbl_meta_data{tbl.get_meta_data()};
  const bool is_no_swap{tbl.get_swap_info().is_no_swap()};
  const bool is_augmentation_or_query_scope_table{
      tbl_meta_data.is_augmentation_table() || tbl_meta_data.is_query_scope_table() ||
      tbl_meta_data.is_query_scope_aggregation_table() || tbl_meta_data.is_query_scope_result_table()};
  if (is_no_swap && !is_augmentation_or_query_scope_table) {
    log::error(
        "Invalid table setting for table [{}]. Swap info is 'no swap' but table type is not an augmentation- or query "
        "scope table and thus the table must be swappable.",
        tbl.get_name());
    throw common::internal_exception{"Invalid table setting for table [{}].", tbl.get_name()};
  }
  if (!is_no_swap && is_augmentation_or_query_scope_table) {
    log::error(
        "Invalid table setting for table [{}]. Swap info is not 'no swap' but table type is an augmentation- or query "
        "scope table which are not swappable.",
        tbl.get_name());
    throw common::internal_exception{"Invalid table setting for table [{}].", tbl.get_name()};
  }
}

void verify_row_limit(row_id row_count, const table_row_limit_t table_row_limit, const std::string& table_name) {
  if (row_count > 5'000'000'000) {
    log::jinfo("Very large table created", {{"row_count", row_count}});
  }
  if (!memory::check_row_limit(row_count, table_row_limit)) {
    throw common::internal_exception::with_context(
        {{"new_row_count", row_count}, {"row_limit", table_row_limit.get()}, {"table_name", table_name}},
        "Row count exceeds row limit.");
  }
}

}  // namespace

table::table(std::nullopt_t rows, std::string name, std::string id, const management::swap_info& sinfo,
             const table_meta_data& meta_data, user_visible_table_name user_visible_name)
    : default_row_count{rows},
      name{std::move(name)},
      id{std::move(id)},
      user_visible_name{user_visible_name.get_name().empty() ? std::nullopt
                                                             : std::make_optional(std::move(user_visible_name))},
      meta_data{meta_data},
      sinfo{sinfo.swap_into_sub_dir(this->id)} {
  verify_table_setting(*this);
}

table::table(const row_id rows, std::string name, std::string id, const management::swap_info& sinfo,
             const table_meta_data& meta_data, user_visible_table_name user_visible_name,
             const table_row_limit_t table_row_limit)
    : table{std::nullopt, std::move(name), std::move(id), sinfo, meta_data, std::move(user_visible_name)} {
  verify_row_limit(rows, table_row_limit, get_name());
  default_row_count = rows;
}

namespace {
constexpr const char* QUERY_SCOPE_TABLE_VERSION{"0"};
}  // anonymous namespace

std::string table::get_user_visible_name(const common::execution_context& context, bool bounds) const {
  if (user_visible_name.has_value()) {
    return bounds ? fmt::format(R"(<{}>)", user_visible_name->get_name()) : user_visible_name->get_name();
  }
  return bounds ? fmt::format(R"("{}")", get_name()) : get_name();
}

row_id table::get_rows() const {
  std::shared_lock<std::shared_timed_mutex> lck(table_mutex);
  return get_rows_intern();
}

void table::add_existing_column(const column_t& column, const table_row_limit_t table_row_limit) {
  const auto lck{concurrency::lock_validated(table_mutex, std::chrono::seconds{60})};
  if (std::cmp_greater_equal(headers.size(), std::numeric_limits<cel_column_count_t>::max())) {
    throw common::cpm_exception{"Maximum number of columns per table reached. Supported limit is [{}], but found [{}].",
                                std::to_string(std::numeric_limits<cel_column_count_t>::max()),
                                std::to_string(headers.size())};
  }
  if (!get_meta_data().is_augmentation_table()) {
    verify_set_row_count_no_lock(column->get_row_count(), table_row_limit);
  }
  headers.push_back(column);
}

std::optional<row_id> table::get_rows_if_known() const {
  if (headers.empty()) {
    return default_row_count;
  }
  return headers[0]->get_row_count();
}

row_id table::get_rows_intern() const {
  auto rows{get_rows_if_known()};
  if (!rows.has_value()) {
    throw common::internal_exception::with_context({{"table_name", get_name()}}, "Row count unknown.");
  }
  return rows.value();
}

void table::verify_set_row_count_no_lock(const row_id new_row_count, std::optional<table_row_limit_t> table_row_limit) {
  // Only other method that accesses default_row_count is private method get_rows_intern, which is only called
  // by shared_lock(table_mutex) protected get_rows

  auto current_row_count{get_rows_if_known()};

  if (current_row_count.has_value()) {
    if (current_row_count.value() != new_row_count) {
      throw common::internal_exception::with_context({{"table_name", get_name()},
                                                      {"current_row_count", current_row_count.value()},
                                                      {"new_row_count", new_row_count}},
                                                     "Row count does not match.");
    }
  } else {
    if (!table_row_limit.has_value()) {
      log::warn(
          "The table {} is in an initial status that the default row count is unset. The new "
          "row count should be verified against the table row limit, but the row limit is not provided, which "
          "can lead to potential problems.",
          get_name());
    } else {
      verify_row_limit(new_row_count, table_row_limit.value(), get_name());
    }
  }

  if (!default_row_count.has_value()) {
    default_row_count = new_row_count;
  }
}

}  // namespace celonis::accelerator::memory
