#pragma once

#include <string>
#include <vector>

#ifndef CELOSTAR
#include "modules/augmentation/augmentation_table_fwd.h"
#endif
#include "modules/common/execution_context_fwd.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/column_info.h"
#include "modules/memory/row_id.h"
#include "modules/memory/table_fwd.h"

namespace celonis::accelerator::memory {

/**
 * @brief this POD and its 'factory' functions below are only used for writing out table meta data for the cube info
 * or memory statistic logging
 */
struct table_info final {
  row_id rows{0};
  cel_table_column_count_type column_count{0};
  std::string id{};
  std::vector<column_info> column_infos{};
  std::string name{};
  bool is_augmentation_table{false};
};

table_info from_table(table* table);

table_info from_table_non_cached(const table* table);

#ifndef CELOSTAR
table_info from_table(const augmentation::augmentation_table* table);
#endif

}  // namespace celonis::accelerator::memory
