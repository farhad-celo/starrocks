#pragma once

#include <memory>

#include "exprs/celonis/agg/variant.h"
#include "exprs/celonis/result_table.h"

namespace cpml_proxy {

struct inductive_miner_result {
    std::unique_ptr<starrocks::celonis::ResultTable> vertex_table;
    std::unique_ptr<starrocks::celonis::ResultTable> edge_table;
    std::unordered_map<std::string, std::size_t> statistics;
};

[[nodiscard]] inductive_miner_result inductive_miner(const starrocks::Variants& variants,
                                                     double imfd_frequency_threshold);

} // namespace cpml_proxy
