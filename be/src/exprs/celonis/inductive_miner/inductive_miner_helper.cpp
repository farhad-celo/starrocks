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

InductiveMinerHelper::InductiveMinerHelper(const starrocks::VariantHashMap& variant_map) {
    // TODO(j.kim): Support IMFD_frequency_threshold.
    inductive_miner_operator_config op_config{make_splittable_eventlog_config(variant_map, 1024), {}};
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
