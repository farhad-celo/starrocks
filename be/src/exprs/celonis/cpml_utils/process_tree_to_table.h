#pragma once

#include <cpml/model/process_tree_fwd.h>

#include <memory>

#include "exprs/celonis/result_table.h"

namespace starrocks::celonis::cpml_utils {

struct pt_as_tables {
    std::unique_ptr<ResultTable> vertex_table;
    std::unique_ptr<ResultTable> edge_table;
};

[[nodiscard]] pt_as_tables convert_pt_to_tables(const cpml::model::process_tree& pt);

} // namespace starrocks::celonis::cpml_utils