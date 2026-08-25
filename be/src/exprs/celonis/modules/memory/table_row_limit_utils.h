#pragma once

#include <cstdint>
#include <limits>
#include <utility>

#include <ctl/conversion.h>
#include <ctl/named_type.h>

#include "log/log.h"
#include "modules/common/exceptions.h"
#include "modules/memory/column.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {

using table_row_limit_t = ctl::named_type<row_id, struct table_row_limit_tag>;

constexpr int64_t SEVEN_BILLION{7'000'000'000};
constexpr table_row_limit_t MAX_TABLE_ROW_LIMIT{
    static_cast<row_id>(sizeof(row_id) == 8 ? SEVEN_BILLION : std::numeric_limits<row_id>::max())};

// check rows is under table_row_limits
template <typename T>
[[nodiscard]] bool check_row_limit(T rows, const table_row_limit_t table_row_limit) {
  return std::cmp_less_equal(rows, table_row_limit.get());
}

// check rows is under both numeric_limits<row_id>::max() and table_row_limits
template <typename T>
[[nodiscard]] bool check_row_limit(T rows, int64_t table_row_limit) {
  return ctl::is_safe_to_cast<row_id>(rows) && std::cmp_less_equal(rows, table_row_limit);
}

inline void verify_row_limit(const column_t& col, const table_row_limit_t table_row_limit) {
  const auto row_count{col->get_row_count()};
  if (row_count > 5'000'000'000) {
    log::jinfo("Very large column created", {{"row_count", row_count}, {"column_name", col->get_user_visible_name()}});
  }
  if (!check_row_limit(row_count, table_row_limit)) {
    throw common::internal_exception::with_context({{"new_row_count", row_count},
                                                    {"row_limit", table_row_limit.get()},
                                                    {"column_name", col->get_user_visible_name()}},
                                                   "Row count exceeds row limit.");
  }
}

}  // namespace celonis::accelerator::memory
