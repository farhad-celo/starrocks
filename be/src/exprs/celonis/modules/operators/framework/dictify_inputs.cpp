#include "modules/memory/column.h"
#include "modules/memory/table.h"

namespace celonis::accelerator::operators::dictify {

bool shall_dictify_pull_up_column(const memory::column_t& column, const memory::table* target) {
  // conditions from cube::pull_up_column_if_necessary and cube::pull_up_column
  return target != nullptr && !column->is_constant() && (column->get_owner() != target) &&
         (column->get_owner_after_pull_up() != target || column->get_row_count() != target->get_rows()) &&
         column->get_owner_after_pull_up() == nullptr;
}

}  // namespace celonis::accelerator::operators::dictify
