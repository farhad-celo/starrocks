#pragma once

#include <vector>

#include "exprs/celonis/result_table.h"
#include "exprs/celonis/variant.h"

using starrocks::celonis::ResultTable;

namespace celonis::accelerator::operators::process {

class InductiveMinerHelper {
public:
    InductiveMinerHelper(const starrocks::VariantHashMap& variant_map, double imfd_frequency_threshold);

    const ResultTable& vertex_table() { return *vertex_table_; }
    const ResultTable& edge_table() { return *edge_table_; }
    const std::unordered_map<std::string, size_t>& statistics() { return statistics_; }

private:
    std::unique_ptr<ResultTable> vertex_table_;
    std::unique_ptr<ResultTable> edge_table_;
    std::unordered_map<std::string, size_t> statistics_;
};

} // namespace celonis::accelerator::operators::process
