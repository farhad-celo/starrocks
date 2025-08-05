#include "table_to_user_visible_name_mapping.h"

#include "legacy_embedded_ctl/assert.h"
#include "modules/common/exceptions.h"
#include "modules/memory/table.h"

namespace celonis::accelerator::cube {

void table_to_user_visible_name_mapping::add(const memory::table* table,
                                             memory::user_visible_table_name user_visible_name) {
  if (table == nullptr) {
    throw common::internal_exception::with_context({{"name", user_visible_name.get_name()}}, "table cannot be null");
  }
  table_to_visible_name_mapping_[table] = std::move(user_visible_name);
}

std::optional<memory::user_visible_table_name> table_to_user_visible_name_mapping::lookup(
    const memory::table* table) const {
  if (table == nullptr) {
    throw common::internal_exception{"TableToUserVisibleName: table cannot be null"};
  }
  if (const auto& name{table_to_visible_name_mapping_.find(table)}; name != table_to_visible_name_mapping_.end()) {
    return name->second;
  }
  return std::nullopt;
}

}  // namespace celonis::accelerator::cube
