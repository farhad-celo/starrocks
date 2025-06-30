#include "product.h"

#include "column/column_helper.h"
#include "column/struct_column.h"
#include "storage/delta_writer.h"

namespace starrocks {

namespace {
template <typename T>
using StageFieldColumnType = typename ProductAggregateState<T>::StageFieldcolumnType;

template <typename T>
using ProductFieldColumnType = typename ProductAggregateState<T>::ProductFieldColumnType;

template <typename T>
std::pair<StageFieldColumnType<T>&, ProductFieldColumnType<T>&> deconstruct(StructColumn& column) {
    auto& field_columns = column.fields_column();
    DCHECK_EQ(field_columns.size(), 2);
    Column* uncasted_stage_column{ColumnHelper::get_data_column(field_columns[0].get())};
    Column* uncasted_product_column{ColumnHelper::get_data_column(field_columns[1].get())};

    auto& stage_column{down_cast<StageFieldColumnType<T>&>(*uncasted_stage_column)};
    auto& product_column{down_cast<ProductFieldColumnType<T>&>(*uncasted_product_column)};
    return {stage_column, product_column};
}

template <typename T>
std::pair<const StageFieldColumnType<T>&, const ProductFieldColumnType<T>&> deconstruct(const StructColumn& column) {
    const auto& field_columns = column.fields();
    DCHECK_EQ(field_columns.size(), 2);
    const Column& uncasted_stage_column{*ColumnHelper::get_data_column(field_columns[0].get())};
    const Column& uncasted_product_column{*ColumnHelper::get_data_column(field_columns[1].get())};
    const auto& stage_column{down_cast<const StageFieldColumnType<T>&>(uncasted_stage_column)};
    const auto& product_column{down_cast<const ProductFieldColumnType<T>&>(uncasted_product_column)};
    return {stage_column, product_column};
}

template <typename T, typename StateType, typename InputColumnType, bool SRC_NULLABLE>
void write_serialized_chunk(const ColumnPtr& src, ColumnPtr& dst, const size_t chunk_size) {
    DCHECK(SRC_NULLABLE == src->is_nullable());
    using RawStageType = typename StageFieldColumnType<T>::ValueType;

    Column* dst_data_column{ColumnHelper::get_data_column(dst.get())};
    const Column* src_column{src.get()};

    /* Get the raw data of the struct column fields and resize them to chunk size. */
    StructColumn& dst_struct_column{down_cast<StructColumn&>(*dst_data_column)};
    dst_struct_column.resize(chunk_size);
    auto&& [dst_stage_column, dst_product_column]{deconstruct<T>(dst_struct_column)};
    Buffer<RawStageType>& dst_stage_column_data{dst_stage_column.get_data()};
    auto& dst_product_column_data{dst_product_column.get_data()};

    /* If dst ist nullable, we also need to write null flags. */
    if (dst.get()->is_nullable()) {
        down_cast<NullableColumn*>(dst.get())->mutable_null_column()->get_data().resize(chunk_size, 0);
    }

    /* Access the casted raw source data. */
    const auto* src_data{
            down_cast<const InputColumnType*>(ColumnHelper::get_data_column(src_column))->get_data().data()};

    /* Write out the chunk. Use template variable to determine whether we need to write null flags to avoid virtual
     * function calls within the loop. */
    for (size_t i{0}; i < chunk_size; i++) {
        if constexpr (SRC_NULLABLE) {
            const auto& null_flags{down_cast<const NullableColumn*>(src_column)->null_column()->get_data()};
            if (null_flags[i]) {
                dst_stage_column_data[i] = static_cast<RawStageType>(StateType::AggregationStage::UNINITIALIZED);
                continue;
            }
        }
        dst_stage_column_data[i] = static_cast<RawStageType>(StateType::AggregationStage::INITIALIZED);
        dst_product_column_data[i] = src_data[i];
    }
}

} // namespace

template <typename T>
ProductAggregateState<T>::ProductAggregateState(AggregationStage stage, T product) : stage_{stage}, product_{product} {}

template <typename T>
T ProductAggregateState<T>::get_product() const {
    DCHECK(is_initialized() && !has_overflowed());
    return product_;
}

template <typename T>
typename ProductAggregateState<T>::AggregationStage ProductAggregateState<T>::get_stage() const {
    return stage_;
}

template <typename T>
bool ProductAggregateState<T>::is_initialized() const {
    return get_stage() == AggregationStage::INITIALIZED;
}

template <typename T>
bool ProductAggregateState<T>::has_overflowed() const {
    return get_stage() == AggregationStage::OVERFLOW;
}

template <typename T>
void ProductAggregateState<T>::update(T val) {
    if (has_overflowed()) {
        return;
    }
    if (!is_initialized()) {
        stage_ = AggregationStage::INITIALIZED;
        product_ = val;
    } else {
        if constexpr (std::is_floating_point_v<T>) {
            product_ *= val;
        } else {
            static_assert(std::is_integral_v<T>);
            const bool overflow_occurred{__builtin_smull_overflow(product_, val, &product_)};
            if (overflow_occurred) {
                stage_ = AggregationStage::OVERFLOW;
            }
        }
    }
}

template <typename T>
void ProductAggregateState<T>::merge(const ProductAggregateState& other) {
    if (other.has_overflowed()) {
        stage_ = AggregationStage::OVERFLOW;
        return;
    }
    if (!other.is_initialized()) {
        return;
    }
    update(other.product_);
}

template <typename T>
void ProductAggregateState<T>::append_to_struct_column(StructColumn& column) const {
    auto stage_val{static_cast<typename StageFieldColumnType<T>::ValueType>(stage_)};
    auto product_val{product_};

    column.append_datum(DatumStruct{Datum{stage_val}, Datum{product_val}});
}

template <typename T>
ProductAggregateState<T> ProductAggregateState<T>::read_from_struct_column(const StructColumn& column,
                                                                           const size_t row_num) {
    const auto& [stage_column, product_column]{deconstruct<T>(column)};

    return ProductAggregateState{static_cast<AggregationStage>(stage_column.get_data()[row_num]),
                                 product_column.get_data()[row_num]};
}

template <LogicalType LT, typename T, LogicalType ResultLT, typename ResultType>
void ProductAggregateFunction<LT, T, ResultLT, ResultType>::reset(FunctionContext* ctx, const Columns& args,
                                                                  AggDataPtr state) const {
    this->data(state) = StateType{};
}

template <LogicalType LT, typename T, LogicalType ResultLT, typename ResultType>
void ProductAggregateFunction<LT, T, ResultLT, ResultType>::update(FunctionContext* ctx, const Column** columns,
                                                                   AggDataPtr __restrict state, size_t row_num) const {
    const Column* data_column{ColumnHelper::get_data_column(columns[0])};

    /* If the null flag for the given row is set, we do not need to update the state. */
    if (columns[0]->is_null(row_num)) {
        return;
    }

    DCHECK(data_column->is_numeric());
    const auto& column = down_cast<const InputColumnType&>(*data_column);
    this->data(state).update(column.get_data()[row_num]);
}

template <LogicalType LT, typename T, LogicalType ResultLT, typename ResultType>
void ProductAggregateFunction<LT, T, ResultLT, ResultType>::merge(FunctionContext* ctx, const Column* column,
                                                                  AggDataPtr __restrict state, size_t row_num) const {
    const Column* data_column{ColumnHelper::get_data_column(column)};

    const auto& struct_column{down_cast<const StructColumn&>(*data_column)};
    StateType state_to_merge{StateType::read_from_struct_column(struct_column, row_num)};

    this->data(state).merge(state_to_merge);
}

template <LogicalType LT, typename T, LogicalType ResultLT, typename ResultType>
void ProductAggregateFunction<LT, T, ResultLT, ResultType>::serialize_to_column([[maybe_unused]] FunctionContext* ctx,
                                                                                ConstAggDataPtr __restrict state,
                                                                                Column* to) const {
    const StateType& typed_state{this->data(state)};
    Column* data_column{ColumnHelper::get_data_column(to)};

    /* If the column is nullable, we need to write the null flag first. */
    if (to->is_nullable()) {
        down_cast<NullableColumn&>(*to).null_column()->append(0);
    }

    auto& struct_column{down_cast<StructColumn&>(*data_column)};
    typed_state.append_to_struct_column(struct_column);
}

template <LogicalType LT, typename T, LogicalType ResultLT, typename ResultType>
void ProductAggregateFunction<LT, T, ResultLT, ResultType>::finalize_to_column([[maybe_unused]] FunctionContext* ctx,
                                                                               ConstAggDataPtr __restrict state,
                                                                               Column* to) const {
    /* We require the output column to be nullable to ensure correct null behavior of the operator. */
    DCHECK(to->is_nullable());
    auto& nullable_result_column{down_cast<NullableColumn&>(*to)};
    if (this->data(state).has_overflowed() || !this->data(state).is_initialized()) {
        nullable_result_column.append_nulls(1);
    } else {
        nullable_result_column.append_datum(this->data(state).get_product());
    }
}

template <LogicalType LT, typename T, LogicalType ResultLT, typename ResultType>
void ProductAggregateFunction<LT, T, ResultLT, ResultType>::convert_to_serialize_format(
        [[maybe_unused]] FunctionContext* ctx, [[maybe_unused]] const Columns& src, [[maybe_unused]] size_t chunk_size,
        [[maybe_unused]] ColumnPtr* dst) const {
    const ColumnPtr& src_col{src[0]};
    DCHECK(src_col->size() >= chunk_size);

    if (src_col->is_nullable()) {
        write_serialized_chunk<T, StateType, InputColumnType, true>(src_col, *dst, chunk_size);
    } else
        write_serialized_chunk<T, StateType, InputColumnType, false>(src_col, *dst, chunk_size);
}

template class ProductAggregateState<RunTimeCppType<TYPE_BIGINT>>;
template class ProductAggregateState<RunTimeCppType<TYPE_DOUBLE>>;

template class ProductAggregateFunction<TYPE_BIGINT>;
template class ProductAggregateFunction<TYPE_DOUBLE>;

} // namespace starrocks