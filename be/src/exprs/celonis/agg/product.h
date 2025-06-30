#pragma once

#include <boost/graph/named_function_params.hpp>

#include "column/struct_column.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/serialization_utils.h"
#include "gutil/casts.h"
#include "types/logical_type.h"

namespace starrocks {

template <LogicalType LT, typename = guard::Guard>
inline constexpr LogicalType ProductResultLT = LT;

template <LogicalType LT>
inline constexpr LogicalType ProductResultLT<LT, IntegerLTGuard<LT>> = TYPE_BIGINT;

template <LogicalType LT>
inline constexpr LogicalType ProductResultLT<LT, FloatLTGuard<LT>> = TYPE_DOUBLE;

template <typename T>
class ProductAggregateState {
public:
    /* Represents the current stage of the aggregation. */
    enum class AggregationStage : int8_t { UNINITIALIZED = 0, INITIALIZED = 1, OVERFLOW = 2 };

    /* Columns types for fields in struct column that this state can be converted to. */
    using StageFieldcolumnType = FixedLengthColumn<std::underlying_type_t<AggregationStage>>;
    using ProductFieldColumnType = FixedLengthColumn<T>;

    ProductAggregateState() = default;
    ProductAggregateState(AggregationStage stage, T product);

    /* Get the current product in the state. Should only be called if it is initialized and has not overflowed. */
    T get_product() const;

    /* Get the current stage of the aggregation. */
    AggregationStage get_stage() const;

    /* Check if the state is initialized, meaning the current product is neither NULL nor overflowed. */
    bool is_initialized() const;

    /* Check if the product in the state has overflowed. */
    bool has_overflowed() const;

    /* Updates the state by multiplying a new value to the product. */
    void update(T val);

    /* Updates the state by merging another state. If one of the states is uninitialized, the initialized state is taken.
     * If one of the states has overflown, the resulting state is overflown as well. */
    void merge(const ProductAggregateState& other);

    /* Serializes the state into an appended row of a given struct column. For output type T the struct column consists
     * of a int8_t column and a column of type T. */
    void append_to_struct_column(StructColumn& column) const;

    /* Deserializes a specific row from a struct column into a state. For output type T the struct column consists
   * of a int8_t column and a column of type T. */
    static ProductAggregateState read_from_struct_column(const StructColumn& column, size_t row_num);

private:
    AggregationStage stage_{AggregationStage::UNINITIALIZED};
    T product_{1};
};

template <LogicalType LT, typename T = RunTimeCppType<LT>, LogicalType ResultLT = ProductResultLT<LT>,
          typename ResultType = RunTimeCppType<ResultLT>>
class ProductAggregateFunction final
        : public AggregateFunctionBatchHelper<ProductAggregateState<ResultType>,
                                              ProductAggregateFunction<LT, T, ResultLT, ResultType>> {
public:
    using InputColumnType = RunTimeColumnType<LT>;
    using ResultColumnType = RunTimeColumnType<ResultLT>;
    using StateType = ProductAggregateState<ResultType>;

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr state) const override;

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override;

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override;

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override;

    std::string get_name() const override { return "celonis_product"; }
};

} // namespace starrocks