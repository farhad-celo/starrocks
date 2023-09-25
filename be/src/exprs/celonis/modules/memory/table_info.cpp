#include "table_info.h"

#ifndef CELOSTAR
#include "modules/augmentation/augmentation_table.h"
#endif
#include "modules/common/execution_context.h"
#include "modules/memory/column.h"
#include "modules/memory/table.h"

namespace celonis::accelerator::memory {

table_info from_table(table* const table) { return from_table_non_cached(table); }

table_info from_table_non_cached(const table* const table) {
  table_info table_info{};
  table_info.rows = table->get_rows_optional().value_or(0);
  table_info.column_count = table->get_columns();
  table_info.id = table->get_id();
  table_info.name = table->get_name();
  const auto& column_headers = table->get_column_headers();
  std::transform(std::cbegin(column_headers), std::cend(column_headers), std::back_inserter(table_info.column_infos),
                 [](const auto& column_header) { return column_header->dump_header(); });
  return table_info;
}

#ifndef CELOSTAR
table_info from_table(const augmentation::augmentation_table* const table) {
  const augmentation::utils::create_request& meta_data = table->meta_data();
  table_info table_info{};
  /*
  if (!table->empty()) {
     // I think the row count is currently not used in cube info. if we will need it, can query latest table version
     table_info.rows = 0;
  }
   */
  table_info.column_count = static_cast<cel_column_count_t>(meta_data.column_configurations().size());
  table_info.id = meta_data.id();
  table_info.name = meta_data.name();
  for (const auto& col : meta_data.column_configurations()) {
    column_info column_info{};
    column_info.name = col.schema().name();
    column_info.id = col.id();
    column_info.domain_column = col.schema().name();
    column_info.type = col.schema().type();
    // TODO(n.weber): can this (cache_key/format) be ignored for the cube info creation?
    // column_info.cache_key = "";
    // column_info.format = "";

    // the following are not set as they are either not set in column::dump_header() (has_domain_table,
    // is_domain_table_temporary) or they are not used in the cube info column_info.has_domain_table
    // column_info.is_domain_table_temporary
    table_info.column_infos.push_back(std::move(column_info));
  }
  table_info.is_augmentation_table = true;
  return table_info;
}
#endif

}  // namespace celonis::accelerator::memory
