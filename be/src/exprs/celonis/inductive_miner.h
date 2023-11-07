#pragma once

#include "exprs/celonis/modules/operators/process/inductive_miner/inductive_miner_helper.h"
#include "exprs/celonis/variant_agg.h"

namespace starrocks {

class InductiveMinerFinalizer : public VariantAggregateFinalizer {
public:
    InductiveMinerFinalizer(FunctionContext* ctx, const VariantAggregateState& state)
            : VariantAggregateFinalizer(ctx, state) {}

    std::string finalize() override;

private:
    std::string json_string(const std::vector<Slice>& activities,
                            const celonis::ResultTable& vertex_table, const celonis::ResultTable& edge_table,
                            const std::unordered_map<std::string, size_t>& statistics_map);
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
