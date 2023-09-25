#include "table_group.h"

#ifndef CELOSTAR
#include "modules/common/exceptions.h"

namespace celonis::accelerator::memory {

table_group::table_group(std::string name, table_map_t table_map)
    : name_{std::move(name)}, table_map_{std::move(table_map)} {}

const std::string& table_group::get_name() const noexcept { return name_; }

memory::table_t table_group::get_table(std::string_view table_name) const {
  auto it{table_map_.find(table_name)};
  if (it == table_map_.end()) {
    throw common::cpm_exception(R"(Table with name ["{}"] does not exist in table group ["{}"].)", table_name, name_);
  }
  return it->second;
}

const table_map_t& table_group::get_tables() const { return table_map_; }
table_map_t::size_type table_group::size() const { return table_map_.size(); }

}  // namespace celonis::accelerator::memory
#endif