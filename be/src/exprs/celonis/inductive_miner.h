#pragma once

#include "exprs/celonis/inductive_miner/inductive_miner_helper.h"
#include "exprs/celonis/variant_stats.h"

namespace starrocks {

// Extends VariantStatsAggregateFunction as it reuses all aggregation logic except for finalize_to_column() which
// implements the inductive miner algorithm on top of the activity_map produced by variant stats.
class InductiveMinerAggregateFunction final : public VariantStatsAggregateFunction {
public:
    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    std::string get_name() const override { return "celonis_inductive_miner"; }

private:
    //inductive_miner_operator_config operator_config_;
};

} // namespace starrocks
