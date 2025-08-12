#include "exprs/celonis/mo_bpmn_graph.h"

#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/celonis/modules/operators/mo/mo_bpmn_graph_helper.h"
#include "exprs/function_context.h"

using celonis::accelerator::operators::mo::MoBpmnGraphHelper;

namespace starrocks {

StatusOr<ColumnPtr> CelonisMoBpmnGraph::mo_bpmn_graph(FunctionContext* context, const Columns& columns) {
    auto num_rows = columns[0]->size();
    ColumnBuilder<TYPE_VARCHAR> results(num_rows);
    for (int row = 0; row < num_rows; row++) {
        std::vector<std::string> process_trees;
        for (auto column: columns) {
            if (column->is_null(row)) {
                return Status::InvalidArgument("input should not be null.");
            }
            process_trees.push_back(column->get(row).get_slice().to_string());
        }
        MoBpmnGraphHelper helper;
        ASSIGN_OR_RETURN(auto result, helper.execute(process_trees));
        results.append(result);
    }

    return results.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
