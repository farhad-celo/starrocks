#pragma once

#include <map>
#include <memory>
#include <string>

#ifdef CELOSTAR
#include "exprs/celonis/result_table.h"
#endif
#include "modules/memory/table_fwd.h"

namespace celonis::accelerator::memory {

#ifdef CELOSTAR
using table_group_t = starrocks::celonis::ResultTableMap;
#else
// Use transparent comparator to make lookup work with string views
using table_map_t = std::map<std::string, memory::table_t, std::less<> >;

/**
 * For now table groups can be thought of as a temporary immutable containers that can be used to return a set of
 * tables. Table groups do not take unique ownership of tables and are not registered in the cube.
 *
 * Each table is registered in the table group with a name. That name is local to the table group and can differ from
 * the actual name of the table.
 *
 * In the future it might make sense to let every table live in a table group (either in a specific one or in a default
 * group) and bind their lifetimes to the table group.
 */
class table_group {
 public:
  table_group(std::string name, table_map_t table_map);

  /**
   * Get the name of the table group.
   *
   * @return Name of the table group
   */
  [[nodiscard]] const std::string& get_name() const noexcept;

  /**
   * Get a table from the table group by name. Throws if the table group has no table with that name.
   *
   * @param table_name Name of the table (local to the table group)
   * @return Shared pointer to the requested table
   */
  [[nodiscard]] memory::table_t get_table(std::string_view table_name) const;

  /**
   * Get all tables contained in this group
   *
   * @return The underlying table_map
   */
  [[nodiscard]] const table_map_t& get_tables() const;

  /**
   * Get the size of the table group.
   *
   * @return The number of tables in the table group
   */
  [[nodiscard]] table_map_t::size_type size() const;

 private:
  std::string name_;
  table_map_t table_map_;
};

using table_group_t = ctl::checked_shared_ptr<table_group>;
#endif

}  // namespace celonis::accelerator::memory