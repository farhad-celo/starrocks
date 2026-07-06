#include "exprs/celonis/align_model.h"

#include "column/column_helper.h"
#include "exprs/celonis/modules/operators/process/align_model/align_model_helper.h"

using celonis::accelerator::operators::process::align_model::AlignModelHelper;

namespace starrocks {

std::string AlignModelFinalizer::finalize() {
    auto json_bpmn_model_description =
            ColumnHelper::get_const_value<TYPE_VARCHAR>(ctx_->get_constant_column(2)).to_string();

    AlignModelHelper helper;
    auto status = helper.execute(variant_map_, activity_map_, json_bpmn_model_description);
    if (!status.ok()) {
        // TODO(j.kim): Find a better way to propagate the error.
        return status.get_error_msg();
    }
    return helper.json_string();
}

} // namespace starrocks