#pragma once

#ifdef CELOSTAR
#include "exprs/celonis/result_table.h"
#else
#include "modules/memory/table_fwd.h"
#endif

namespace celonis::accelerator::operators::process {

struct process_tree_ref {
#ifdef CELOSTAR
  std::unique_ptr<starrocks::celonis::ResultTable> vertex_table;
  std::unique_ptr<starrocks::celonis::ResultTable> edge_table;
#else
  memory::table_t vertex_table;
  memory::table_t edge_table;
#endif
};

}  // namespace celonis::accelerator::operators::process
