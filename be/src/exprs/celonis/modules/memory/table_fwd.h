#pragma once

#include <vector>

#include "legacy_embedded_ctl/checked_ptr.h"
#include "legacy_embedded_ctl/named_type.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {
class table;
using table_t = legacy_embedded_ctl::checked_shared_ptr<table>;
using tables_t = std::vector<table_t>;
using weak_table_ptr_t = legacy_embedded_ctl::checked_weak_ptr<const table>;
using weak_table_ptrs_t = std::vector<weak_table_ptr_t>;
using raw_table_ptr_t = legacy_embedded_ctl::checked_raw_ptr<const table>;
using mut_raw_table_ptr_t = legacy_embedded_ctl::checked_raw_ptr<table>;
using raw_table_ptrs_t = std::vector<raw_table_ptr_t>;
using mut_raw_table_ptrs_t = std::vector<mut_raw_table_ptr_t>;

using table_row_limit_t = legacy_embedded_ctl::named_type<row_id, struct table_row_limit_tag>;

class user_visible_table_name;

constexpr int64_t SEVEN_BILLION{7'000'000'000};
constexpr table_row_limit_t MAX_TABLE_ROW_LIMIT{
    static_cast<row_id>(sizeof(row_id) == 8 ? SEVEN_BILLION : std::numeric_limits<row_id>::max())};

// check rows is under table_row_limits
template <typename T>
[[nodiscard]] bool check_row_limit(T rows, const table_row_limit_t table_row_limit) {
  return std::cmp_less_equal(rows, table_row_limit.get());
}

}  // namespace celonis::accelerator::memory
