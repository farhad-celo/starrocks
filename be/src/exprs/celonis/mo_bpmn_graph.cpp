#include "exprs/celonis/mo_bpmn_graph.h"

#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/celonis/modules/operators/mo/mo_bpmn_graph_helper.h"
#include "exprs/function_context.h"

using celonis::accelerator::operators::mo::MoBpmnGraphHelper;

namespace starrocks {

StatusOr<ColumnPtr> CelonisMoBpmnGraph::mo_bpmn_graph(FunctionContext* context, const Columns& columns) {
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_VARCHAR> results(num_rows);
    for (int row = 0; row < num_rows; row++) {
        std::vector<std::string> process_trees;
        bool has_null = false;
        for (auto column: columns) {
            if (column->is_null(row)) {
                has_null = true;
                break;
            }
            process_trees.push_back(column->get(row).get_slice().to_string());
        }
        if (has_null) {
            results.append_null();
            continue;
        }
        MoBpmnGraphHelper helper;
        ASSIGN_OR_RETURN(auto result, helper.execute(process_trees));
        results.append(result);
    }

    return results.build(all_const);
}

} // namespace starrocks
