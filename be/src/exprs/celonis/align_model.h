#pragma once

#include "exprs/celonis/modules/operators/process/align_model/align_model_helper.h"
#include "exprs/celonis/variant_agg.h"

namespace starrocks {

class AlignModelFinalizer : public VariantAggregateFinalizer {
public:
    AlignModelFinalizer(FunctionContext* ctx, const VariantAggregateState& state)
            : VariantAggregateFinalizer(ctx, state) {}

    std::string finalize() override;
};

// Extends VariantAggregateFunction, runs the align model algorithm and returns result tables in json.
class AlignModelAggregateFunction final : public VariantAggregateFunction {
public:
    std::unique_ptr<VariantAggregateFinalizer> get_finalizer(FunctionContext* ctx,
                                                             const VariantAggregateState& state) const override {
        return std::make_unique<AlignModelFinalizer>(ctx, state);
    }

    std::string get_name() const override { return "celonis_align_model"; }
};

} // namespace starrocks
