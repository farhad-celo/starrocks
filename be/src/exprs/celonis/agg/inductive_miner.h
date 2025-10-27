#pragma once

#include "column/column_helper.h"
#include "exprs/celonis/result_table.h"
#include "variant_agg.h"

namespace starrocks {

class InductiveMinerState : public VariantAggregateState {
public:
    InductiveMinerState() : VariantAggregateState() {}
    ~InductiveMinerState() {}

    size_t update(FunctionContext* ctx, const Column** columns, size_t row_num) override {
        if (ctx->is_notnull_constant_column(2)) {
            imfd_frequency_threshold_ = ColumnHelper::get_const_value<TYPE_DOUBLE>(ctx->get_constant_column(2));
        }

        return VariantAggregateState::update(ctx, columns, row_num);
    }

    size_t serialized_size() const override {
        size_t result = sizeof(double);
        result += VariantAggregateState::serialized_size();
        return result;
    };

    void serialize(uint8_t* dst) const override {
        memcpy(dst, &imfd_frequency_threshold_, sizeof(double));
        dst += sizeof(double);
        VariantAggregateState::serialize(dst);
    }

    size_t deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len) override {
        memcpy(&imfd_frequency_threshold_, src, sizeof(double));
        src += sizeof(double);
        len -= sizeof(double);
        return VariantAggregateState::deserialize_and_merge(mem_pool, src, len);
    }

    double imfd_frequency_threshold() const { return imfd_frequency_threshold_; }

private:
    double imfd_frequency_threshold_ = 0.0;
};

class InductiveMinerFinalizer : public VariantAggregateFinalizer {
public:
    InductiveMinerFinalizer(FunctionContext* ctx, const InductiveMinerState& state)
            : VariantAggregateFinalizer(ctx, static_cast<const VariantAggregateState&>(state)),
              imfd_frequency_threshold_(state.imfd_frequency_threshold()) {}

    std::optional<std::string> finalize(FunctionContext* ctx) override;

private:
    std::string json_string(const std::vector<Slice>& activities, const celonis::ResultTable& vertex_table,
                            const celonis::ResultTable& edge_table,
                            const std::unordered_map<std::string, size_t>& statistics_map);

    const double imfd_frequency_threshold_;
};

// Extends VariantAggregateFunction and runs the inductive miner algorithm.
/**
 * @param: [ input_column, weight_column, imfd_frequency_threshold ]
 * @paramType columns: [ BIGINT, BIGINT, DOUBLE ]
 * @return: json string of petri net
 * weight_column : Indicates the frequency of the input(variant)
 * edge_count (optional) : Limits the size of the edge table
 *
 * Implements PQL INDUCTIVE_MINER
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245739/INDUCTIVE+MINER
 */
class InductiveMinerAggregateFunction final : public VariantAggregateFunction<InductiveMinerState> {
public:
    std::unique_ptr<VariantAggregateFinalizer> get_finalizer(FunctionContext* ctx,
                                                             const InductiveMinerState& state) const override {
        return std::make_unique<InductiveMinerFinalizer>(ctx, state);
    }

    std::string get_name() const override { return "celonis_inductive_miner"; }
};

} // namespace starrocks
