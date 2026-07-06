#pragma once

#include <vector>

#include "common/status.h"
#include "exprs/celonis/result_table.h"
#include "exprs/celonis/variant.h"

using starrocks::celonis::ResultTable;
using starrocks::Status;

namespace celonis::accelerator::operators::process::align_model {

class AlignModelHelper {
public:
    AlignModelHelper() {};

    Status execute(const starrocks::VariantHashMap& variant_map, const starrocks::SliceHashMap& activity_map,
                   const std::string& bpmn_model_description_json);

    std::string json_string();

private:
    std::unique_ptr<ResultTable> alignment_table_;
    std::unique_ptr<ResultTable> association_table_;
    std::unique_ptr<ResultTable> edge_class_table_;
};

} // namespace celonis::accelerator::operators::process::align_model
