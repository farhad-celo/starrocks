#pragma once

#include <vector>

#include "common/status.h"
#include "exprs/celonis/result_table.h"

using starrocks::celonis::ResultTable;
using starrocks::Status;

namespace celonis::accelerator::operators::process::align_model {

class AlignModelHelper {
public:
    AlignModelHelper() {};

    Status execute(const std::vector<std::vector<std::string>>& variants,
                   const std::string& bpmn_model_description_json);

    const ResultTable& result_table() { return *result_table_; }

private:
    std::unique_ptr<ResultTable> result_table_;
};

} // namespace celonis::accelerator::operators::process::align_model
