#pragma once

#include <vector>

#include "common/status.h"
#include "exprs/celonis/result_table.h"

using starrocks::celonis::ResultTable;
using starrocks::StatusOr;

namespace celonis::accelerator::operators::mo {

class MoBpmnGraphHelper {
public:
    MoBpmnGraphHelper() {};

    StatusOr<std::string> execute(const std::vector<std::string>& process_trees_json);
};

} // namespace celonis::accelerator::operators::mo
