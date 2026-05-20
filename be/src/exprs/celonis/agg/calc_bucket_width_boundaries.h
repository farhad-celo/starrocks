#pragma once

#include "column/array_column.h"
#include "column/column_helper.h"
#include "column/object_column.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/util.h"
#include "gutil/casts.h"
#include "util/percentile_value.h"
#include "util/tdigest.h"

namespace starrocks {

template <LogicalType LT>
struct CelonisCalcBucketWidthBoundariesState {
public:
    using CppType = RunTimeCppType<LT>;
    using WidthType = RunTimeCppType<TYPE_BIGINT>;

    CelonisCalcBucketWidthBoundariesState() : percentile(new PercentileValue()) {}

    ~CelonisCalcBucketWidthBoundariesState() = default;

    void update(CppType value) {
        if constexpr (LT == TYPE_DATETIME) {
            if (!value.is_valid()) {
                return;
            }
        }
        true_min = std::min<CppType>(true_min, value);
        true_max = std::max<CppType>(true_max, value);
        percentile->add(to_histogram_value<LT>(value));
        is_null = false;
    }

    void deserialize_and_merge(const uint8_t* src) {
        WidthType src_width;
        memcpy(&src_width, src, sizeof(WidthType));
        src += sizeof(WidthType);
        CppType src_true_min;
        memcpy(&src_true_min, src, sizeof(CppType));
        src += sizeof(CppType);
        CppType src_true_max;
        memcpy(&src_true_max, src, sizeof(CppType));
        src += sizeof(CppType);
        PercentileValue src_percentile;
        src_percentile.deserialize((const char*)src);

        width = src_width;
        true_min = std::min<CppType>(true_min, src_true_min);
        true_max = std::max<CppType>(true_max, src_true_max);
        percentile->merge(&src_percentile);
        is_null = false;
    }

    size_t serialized_size() const {
        size_t result = 0;
        result += sizeof(WidthType); // width
        result += sizeof(CppType);   // true_min
        result += sizeof(CppType);   // true_max
        result += percentile->serialize_size();
        return result;
    }

    void serialize(uint8_t* dst) const {
        memcpy(dst, &width, sizeof(WidthType));
        dst += sizeof(WidthType);
        memcpy(dst, &true_min, sizeof(CppType));
        dst += sizeof(CppType);
        memcpy(dst, &true_max, sizeof(CppType));
        dst += sizeof(CppType);
        percentile->serialize(dst);
    }

    WidthType width = 1;
    CppType true_min = RunTimeTypeLimits<LT>::max_value();
    CppType true_max = RunTimeTypeLimits<LT>::min_value();
    std::unique_ptr<PercentileValue> percentile;
    bool is_null = true;
};

/**
 * @param: [col, width]
 * @paramType: [BIGINT | DOUBLE | DATETIME, CONST BIGINT]
 *   width: bucket width.
 * @return: ARRAY of col type
 *
 * Outputs a boundary array to be used as an input of celonis_histogram_boundaries() to implement PQL HISTOGRAM with
 * mode BUCKET_WIDTH. https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245736/HISTOGRAM
 *
 * The implementation is similar to calc_bucket_count_boundaries which implements PQL HISTOGRAM with mode BUCKET_COUNT.
 *
 * Note: PercentileValue uses float so it may lose some precision especially with DATETIME with narrow ranges.
 */
template <LogicalType LT>
class CelonisCalcBucketWidthBoundariesAggregateFunction final
        : public AggregateFunctionBatchHelper<CelonisCalcBucketWidthBoundariesState<LT>,
                                              CelonisCalcBucketWidthBoundariesAggregateFunction<LT>> {
public:
    using CppType = RunTimeCppType<LT>;
    using ColumnType = RunTimeColumnType<LT>;
    using WidthType = RunTimeCppType<TYPE_BIGINT>;

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr state, size_t row_num) const override {
        CppType column_value;
        if (columns[0]->is_nullable()) {
            if (columns[0]->is_null(row_num)) {
                return;
            }
            column_value = down_cast<const NullableColumn*>(columns[0])->data_column()->get(row_num).get<CppType>();
        } else {
            column_value = down_cast<const ColumnType*>(columns[0])->get_data()[row_num];
        }

        if (this->data(state).is_null && ctx->get_num_args() == 2) {
            DCHECK(!columns[1]->only_null());
            DCHECK(!columns[1]->is_null(0));
            WidthType width = columns[1]->get(0).get<WidthType>();
            if (width > 0) {
                this->data(state).width = width;
            } else if (width == 0) {
                this->data(state).width = 1;
            }
        }

        this->data(state).update(column_value);
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override {
        Slice src;
        if (column->is_nullable()) {
            if (column->is_null(row_num)) {
                return;
            }
            const auto* nullable_column = down_cast<const NullableColumn*>(column);
            src = nullable_column->data_column()->get(row_num).get_slice();
        } else {
            const auto* binary_column = down_cast<const BinaryColumn*>(column);
            src = binary_column->get_slice(row_num);
        }

        this->data(state).deserialize_and_merge((const uint8_t*)src.data);
    }

    bool support_nullable_immediate_input() const override { return true; }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        const auto& state_impl = this->data(state);
        auto size = state_impl.serialized_size();
        uint8_t result[size];
        state_impl.serialize(result);

        if (to->is_nullable()) {
            auto* column = down_cast<NullableColumn*>(to);
            if (state_impl.is_null) {
                column->append_default();
            } else {
                down_cast<BinaryColumn*>(column->data_column().get())->append(Slice(result, size));
                column->null_column_data().push_back(0);
            }
        } else {
            auto* column = down_cast<BinaryColumn*>(to);
            column->append(Slice(result, size));
        }
    }

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override {
        const ColumnType* input = nullptr;
        BinaryColumn* result = nullptr;
        // get input data column
        if (src[0]->is_nullable()) {
            const auto* nullable_column = down_cast<const NullableColumn*>(src[0].get());
            input = down_cast<const ColumnType*>(nullable_column->data_column().get());

            auto* dst_nullable_column = down_cast<NullableColumn*>((*dst).get());
            result = down_cast<BinaryColumn*>(dst_nullable_column->data_column().get());
            dst_nullable_column->null_column_data() = nullable_column->immutable_null_column_data();
        } else {
            input = down_cast<const ColumnType*>(src[0].get());
            // Even if the input column is non-nullable, the result column still could be nullable
            if ((*dst)->is_nullable()) {
                auto* dst_nullable_column = down_cast<NullableColumn*>((*dst).get());
                result = down_cast<BinaryColumn*>(dst_nullable_column->data_column().get());
                dst_nullable_column->null_column_data().resize(chunk_size, 0);
            } else {
                result = down_cast<BinaryColumn*>((*dst).get());
            }
        }

        DCHECK_EQ(2, ctx->get_num_args());
        DCHECK(src[1]->is_constant());

        WidthType width = src[1]->get(0).get<WidthType>();
        Bytes& bytes = result->get_bytes();
        bytes.reserve(chunk_size * 20);
        result->get_offset().resize(chunk_size + 1);

        // serialize percentile one by one
        size_t old_size = bytes.size();
        for (size_t i = 0; i < chunk_size; ++i) {
            if (src[0]->is_null(i)) {
                auto* dst_nullable_column = down_cast<NullableColumn*>((*dst).get());
                dst_nullable_column->set_has_null(true);
                result->get_offset()[i + 1] = old_size;
            } else {
                CppType value = input->get_data()[i];
                PercentileValue percentile;
                if constexpr (LT == TYPE_DATETIME) {
                    if (!value.is_valid()) {
                        auto* dst_nullable_column = down_cast<NullableColumn*>((*dst).get());
                        dst_nullable_column->set_has_null(true);
                        result->get_offset()[i + 1] = old_size;
                        continue;
                    }
                }
                percentile.add(to_histogram_value<LT>(value));

                size_t new_size = old_size + sizeof(WidthType) + sizeof(CppType) * 2 + percentile.serialize_size();
                bytes.resize(new_size);
                uint8_t* dst = bytes.data() + old_size;
                memcpy(dst, &width, sizeof(WidthType)); // width
                dst += sizeof(WidthType);
                memcpy(dst, &value, sizeof(CppType)); // true_min
                dst += sizeof(CppType);
                memcpy(dst, &value, sizeof(CppType)); // true_max
                dst += sizeof(CppType);
                percentile.serialize(dst);

                result->get_offset()[i + 1] = new_size;
                old_size = new_size;
            }
        }
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        const auto& state_impl = this->data(state);
        auto* data_column = to;
        NullData* null_data = nullptr;
        if (to->is_nullable()) {
            auto nullable_column = down_cast<NullableColumn*>(to);
            null_data = &nullable_column->null_column_data();
            if (state_impl.is_null) {
                nullable_column->append_default();
                return;
            }
            data_column = nullable_column->mutable_data_column();
        } else if (this->data(state).is_null) {
            return;
        }

        double min_value = state_impl.percentile->quantile(HISTOGRAM_MIN_TARGET_QUANTILE);
        double max_value = state_impl.percentile->quantile(HISTOGRAM_MAX_TARGET_QUANTILE);

        generate_boundaries(ctx, min_value, max_value, state_impl.true_min, state_impl.true_max, state_impl.width,
                            down_cast<ArrayColumn*>(data_column), null_data);
    }

    std::string get_name() const override { return "celonis_calc_bucket_width_boundaries"; }

private:
    void generate_boundaries(FunctionContext* ctx, double min_value, double max_value, CppType true_min,
                             CppType true_max, WidthType width, ArrayColumn* to, NullData* null_data) const {
        double true_min_value = to_histogram_value<LT>(true_min);
        double true_max_value = to_histogram_value<LT>(true_max);

        WidthType count = static_cast<WidthType>(std::ceil((true_max_value - true_min_value + 1) / width));
        if (count > MAX_NUM_BUCKETS) {
            ctx->set_error(std::string("The number of buckets is more than " + std::to_string(MAX_NUM_BUCKETS)).c_str(),
                           false);
            return;
        }
        // The output column is nullable, populate null_data.
        if (null_data != nullptr) {
            null_data->push_back(0);
        }

        if (min_value + (width * static_cast<double>(count)) > true_max_value && min_value > true_min_value) {
            min_value = min_value - std::min(min_value - true_min_value,
                                             (min_value + (width * static_cast<double>(count))) - true_max_value);
        }

        if constexpr (LT != TYPE_DOUBLE) {
            min_value = min_value < 0 ? std::ceil(min_value - 0.5) : std::floor(min_value + 0.5);
        }
        max_value = min_value + static_cast<double>(count) * width;

        auto* elements_column = to->elements_column().get();
        auto* offsets_column = to->offsets_column().get();
        int new_offset = 0;

        const Datum lower_bound = from_histogram_value<LT>(std::min(min_value, std::floor(true_min_value)));
        elements_column->append_datum(lower_bound);
        new_offset++;
        Datum prev_boundary = lower_bound;
        for (int i = 1; i < count; i++) {
            const Datum boundary = from_histogram_value<LT>(min_value + (static_cast<double>(i) * width));
            if (prev_boundary.convert2DatumKey() != boundary.convert2DatumKey()) {
                elements_column->append_datum(boundary);
                new_offset++;
                prev_boundary = boundary;
            }
        }
        elements_column->append_datum(from_histogram_value<LT>(std::max(max_value, std::ceil(true_max_value + 1))));
        new_offset++;
        offsets_column->get_data().push_back(new_offset);
    }
};

} // namespace starrocks
