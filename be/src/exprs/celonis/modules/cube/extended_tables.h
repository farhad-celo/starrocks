#pragma once

#include <map>
#include <unordered_map>

#include "modules/common/ignore_case_comparator.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/table_fwd.h"

namespace celonis::accelerator::cube {

class extended_tables {
 public:
  explicit extended_tables() = default;

  void add(const memory::table* table, const std::string& column_name, memory::column_t column);

  [[nodiscard]] std::optional<memory::column_t> lookup(const memory::table* table, std::string_view column_name) const;

  [[nodiscard]] std::vector<std::string> get_columns(const memory::table* table) const;

 private:
  using columns_entry_t = std::map<std::string, memory::column_t, common::ignore_case_comparator_less>;
  std::unordered_map<const memory::table*, columns_entry_t> tables_;
};

}  // namespace celonis::accelerator::cube
