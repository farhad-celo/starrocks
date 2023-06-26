#pragma once

#include "modules/memory/table_fwd.h"

namespace celonis::accelerator::operators::process {

struct process_tree_ref {
  memory::table_t vertex_table;
  memory::table_t edge_table;
};

}  // namespace celonis::accelerator::operators::process
