#pragma once

#include "exprs/celonis/inductive_miner/inductive_miner_helper.h"
#include "exprs/celonis/variant_agg.h"

namespace starrocks {

class InductiveMinerFinalizer : public VariantAggregateFinalizer {
public:
    InductiveMinerFinalizer(FunctionContext* ctx, const VariantAggregateState& state)
            : VariantAggregateFinalizer(ctx, state) {}

    std::string finalize() override;

private:
    std::string json_string(const VariantAggregateState::SliceHashMap& activity_map,
                            const celonis::ResultTable& vertex_table, const ResultTable& edge_table);
};

// Extends VariantAggregateFunction and runs the inductive miner algorithm.
class InductiveMinerAggregateFunction final : public VariantAggregateFunction {
public:
    std::unique_ptr<VariantAggregateFinalizer> get_finalizer(FunctionContext* ctx,
                                                             const VariantAggregateState& state) const override {
        return std::make_unique<InductiveMinerFinalizer>(ctx, state);
    }

    std::string get_name() const override { return "celonis_inductive_miner"; }
};

} // namespace starrocks
