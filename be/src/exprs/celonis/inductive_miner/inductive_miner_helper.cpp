#include "inductive_miner_helper.h"

#include <chrono>

#include "exprs/celonis/result_table.h"
#include "exprs/celonis/variant.h"
#include "inductive_miner/directly_follows_graph.h"
#include "inductive_miner/inductive_miner.h"
#include "inductive_miner/inductive_miner_statistics.h"
#include "inductive_miner/splittable_eventlog_config.h"
#include "modules/common/execution_context.h"
#include "modules/cube/execution/tracking/stop_token.h"

using starrocks::celonis::ResultTable;

namespace celonis::accelerator::operators::process {

InductiveMinerHelper::InductiveMinerHelper(const starrocks::VariantHashMap& variant_map,
                                           double imfd_frequency_threshold) {
    dfg_filter_config filter_config;
    // TODO(j.kim): Return an error if the threshold is invalid.
    if (imfd_frequency_threshold > 0.0 && imfd_frequency_threshold <= 1.0) {
       filter_config.edges = true;
       filter_config.edges_threshold = imfd_frequency_threshold;
    }
    common::execution_context dummy_context;
    size_t grain_size{1024};
    auto miner_config = inductive_miner_config{make_splittable_eventlog_config(variant_map, grain_size), dummy_context,
                                               grain_size, filter_config};
    auto dfg{dfg::initialize_dfg(miner_config.eventlog(), dummy_context, miner_config.grain_size())};

    inductive_miner_statistics dummy_miner_statistics;
    cube::execution::tracking::stop_token dummy_stop_token{"INDUCTIVE_MINER", std::chrono::minutes(10)};
    process_tree pt = inductive_miner(miner_config, dfg, dummy_miner_statistics, dummy_stop_token).tree;

    auto tables = convert_to_tables(pt);
    vertex_table_ = std::move(tables.vertex_table);
    edge_table_ = std::move(tables.edge_table);
}

} // namespace celonis::accelerator::operators::process
