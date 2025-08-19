#pragma once

#include <optional>
#include <vector>

#include "common/status.h"
#include "exprs/celonis/result_table.h"

using starrocks::celonis::ResultTable;
using starrocks::Status;

namespace celonis::accelerator::operators::process::align_model {

class AlignModelHelper {
public:
    using activity_name_t = std::string;
    using trace_t = std::vector<std::optional<activity_name_t>>;
    using traces_t = std::vector<trace_t>;

    AlignModelHelper() = default;

    Status execute(const traces_t& traces, const std::string& bpmn_model_description_json);

    const ResultTable& result_table() { return *result_table_; }

private:
    std::unique_ptr<ResultTable> result_table_;
};

} // namespace celonis::accelerator::operators::process::align_model
