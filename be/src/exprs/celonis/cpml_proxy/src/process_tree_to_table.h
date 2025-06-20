#pragma once

#include <cpml/model/process_tree_fwd.h>
#include <cpml/model/pt/node_to_counts_mapping.h>
#include <cpml/model/pt/replay.h>

#include <memory>

#include "exprs/celonis/result_table.h"

namespace cpml_proxy {

struct pt_as_tables {
    std::unique_ptr<starrocks::celonis::ResultTable> vertex_table;
    std::unique_ptr<starrocks::celonis::ResultTable> edge_table;
};

[[nodiscard]] pt_as_tables convert_pt_to_tables(const cpml::model::process_tree& pt);

} // namespace cpml_proxy