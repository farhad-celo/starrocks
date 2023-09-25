#include "extended_tables.h"

#include <ranges>

#include "modules/memory/column.h"
#include "modules/memory/table.h"

namespace celonis::accelerator::cube {

void extended_tables::add(const memory::table* table, const std::string& column_name, memory::column_t column) {
  tables_[table].emplace(column_name, column);
}

std::optional<memory::column_t> extended_tables::lookup(const memory::table* table,
                                                        const std::string_view column_name) const {
  if (!tables_.contains(table)) {
    return std::nullopt;
  }
  if (const auto& it{tables_.at(table).find(column_name.data())}; it != tables_.at(table).end()) {
    return it->second;
  }
  return std::nullopt;
}

std::vector<std::string> extended_tables::get_columns(const memory::table* table) const {
  std::vector<std::string> columns{};
  if (!tables_.contains(table)) {
    return columns;
  }
  columns.reserve(tables_.at(table).size());
  std::ranges::for_each(tables_.at(table), [&columns](const auto& it) { columns.emplace_back(it.first); });
  return columns;
}

}  // namespace celonis::accelerator::cube
