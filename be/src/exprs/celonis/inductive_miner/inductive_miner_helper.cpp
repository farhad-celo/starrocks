#include "inductive_miner_helper.h"

#include "exprs/celonis/result_table.h"
#include "exprs/celonis/variant.h"
#include "inductive_miner/directly_follows_graph.h"
#include "inductive_miner/inductive_miner.h"
#include "inductive_miner/inductive_miner_statistics.h"
#include "inductive_miner/splittable_eventlog_config.h"
#include "modules/common/execution_context.h"

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
    inductive_miner_operator_config op_config{make_splittable_eventlog_config(variant_map, 1024), filter_config};
    common::execution_context dummy_context;
    auto miner_config = inductive_miner_config::from_op_config(op_config, dummy_context);
    auto dfg{dfg::initialize_dfg(miner_config.eventlog, dummy_context, miner_config.grain_size)};

    inductive_miner_statistics dummy_miner_statistics;
    process_tree pt = inductive_miner(miner_config, dfg, dummy_context, dummy_miner_statistics).tree;

    auto tables = convert_to_tables(pt);
    vertex_table_ = std::move(tables.vertex_table);
    edge_table_ = std::move(tables.edge_table);
}

} // namespace celonis::accelerator::operators::process
