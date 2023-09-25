#pragma once

#include <map>
#include <optional>

#include "modules/memory/table_fwd.h"

namespace celonis::accelerator::cube {

/**
 * This class maps memory::table* to a user visible name defined by REGISTER operator in PQL.
 */
class table_to_user_visible_name_mapping {
 public:
  explicit table_to_user_visible_name_mapping() = default;

  /**
   * Stores user visible name by a table.
   */
  void add(const memory::table* table, memory::user_visible_table_name user_visible_name);

  /**
   * Gets a registered user visible name with the given table.
   */
  [[nodiscard]] std::optional<memory::user_visible_table_name> lookup(const memory::table* table) const;

 private:
  std::map<const memory::table*, memory::user_visible_table_name> table_to_visible_name_mapping_;
};

}  // namespace celonis::accelerator::cube
