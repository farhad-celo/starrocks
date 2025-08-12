#include "exprs/celonis/remap_timestamp_weekday.h"

#include "column/array_column.h"
#include "column/column.h"
#include "column/column_builder.h"
#include "column/column_hash.h"
#include "column/column_viewer.h"
#include "exprs/celonis/util.h"

namespace starrocks {

// See https://docs.google.com/document/d/1q_MDFvi3Y9VP_HlAud7Nf--KXi_uB-FU0OODWO1pgJU/edit#.
static int weekday[] = {0, 1, 2, 2, 2, 3, 4};

// Number of days since the start of the unix epoch.
static constexpr JulianDate UNIX_EPOCH_JULIAN = 2440588;

static int64_t convert_timestamp_to_weekday(long timestamp_val) {
    JulianDate year_in_days = timestamp::to_julian(timestamp_val);
    int64_t days_from_unix_epoch = year_in_days - UNIX_EPOCH_JULIAN;
    return (days_from_unix_epoch/7) * 5 + weekday[days_from_unix_epoch % 7];
}

template <bool has_null>
ColumnPtr CelonisRemapTimestampWeekday::_celonis_remap_timestamp_weekday_impl(FunctionContext* context,
                                                                              const TimestampColumn& timestamp_elements,
                                                                              const UInt32Column& timestamp_offsets,
                                                                              NullColumnPtr timestamp_nulls,
                                                                              NullColumnPtr array_nulls) {
    const size_t num_cases = timestamp_offsets.size() - 1;
    auto timestamp_offsets_ptr = timestamp_offsets.get_data().data();
    ColumnBuilder<TYPE_BIGINT> result(num_cases);

    for (size_t i = 0; i < num_cases; i++) {
        if constexpr (has_null) {
            if (array_nulls != nullptr && array_nulls->get(i).get_uint8()) {
                if (timestamp_offsets_ptr[i + 1] - timestamp_offsets_ptr[i] != 0) {
                    std::stringstream error;
                    error << "internal error while evaluating celonis_remap_timestamp_weekday: NULL array has non-zero offset size" << std::endl;
                    throw std::runtime_error(error.str());
                }
                continue;
            }
        }
        for (int j = timestamp_offsets_ptr[i]; j < timestamp_offsets_ptr[i+1]; ++j) {
            if constexpr(has_null) {
                if (timestamp_nulls != nullptr && timestamp_nulls->get(j).get_uint8()){
                    result.append_null();
                    continue;
                }
            }
            const TimestampValue& val = timestamp_elements.get(j).get_timestamp();
            result.append(convert_timestamp_to_weekday(val.timestamp()));
        }
    }
    return ArrayColumn::create(
            ColumnHelper::cast_to_nullable_column(result.build(false)),
            UInt32Column::create(timestamp_offsets));
}

StatusOr<ColumnPtr> CelonisRemapTimestampWeekday::celonis_remap_timestamp_weekday_scalar(FunctionContext* context, const Columns& columns) {
    ColumnViewer<TYPE_DATETIME> viewer(columns[0]);
    size_t size = columns[0]->size();
    ColumnBuilder<TYPE_BIGINT> builder(size);
    for (int row = 0; row < size; ++row) {
        if (viewer.is_null(row)) {
            builder.append_null();
        } else {
            const long timestamp_val = viewer.value(row).timestamp();
            builder.append(convert_timestamp_to_weekday(timestamp_val));
        }
    }
    return builder.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisRemapTimestampWeekday::celonis_remap_timestamp_weekday(FunctionContext* context, const Columns& columns) {
    const Column* timestamp_array = columns[0].get();
    const NullableColumn* nullable_timestamp_array = nullptr;
    if (timestamp_array->is_nullable()) {
        nullable_timestamp_array = down_cast<const NullableColumn*>(timestamp_array);
        timestamp_array = nullable_timestamp_array->data_column().get();
    }
    const auto& timestamp_array_column = extract_array_column(timestamp_array);
    const Column* timestamp_elements = &timestamp_array_column.elements();
    const UInt32Column& timestamp_offsets = timestamp_array_column.offsets();
    NullColumnPtr timestamp_nulls = nullptr;
    if (timestamp_elements->has_null()) {
        timestamp_nulls = (down_cast<const NullableColumn*>(timestamp_elements)->null_column());
    }
    if (auto nullable = dynamic_cast<const NullableColumn*>(timestamp_elements); nullable != nullptr) {
        timestamp_elements = nullable->data_column().get();
    }

    if (typeid(*timestamp_elements) != typeid(TimestampColumn)) {
        std::stringstream error;
        error << "wrong column type as input to celonis_remap_timestamp_weekday" << typeid(*timestamp_elements).name() << std::endl;
        throw std::runtime_error(error.str());
    }

    ColumnPtr res = nullptr;
    if (timestamp_nulls != nullptr || nullable_timestamp_array != nullptr) {
        res = _celonis_remap_timestamp_weekday_impl<true>(context, *down_cast<const TimestampColumn*>(timestamp_elements),
                                                          timestamp_offsets, timestamp_nulls, nullable_timestamp_array->null_column());
    } else {
        res = _celonis_remap_timestamp_weekday_impl<false>(context, *down_cast<const TimestampColumn*>(timestamp_elements),
                                                          timestamp_offsets, nullptr, nullptr);
    }

    if (nullable_timestamp_array != nullptr) {
        return NullableColumn::create(res, nullable_timestamp_array->null_column());
    }
    return res;
}

} // namespace starrocks
