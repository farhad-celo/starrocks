#include "exprs/celonis/calc_throughput.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_viewer.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

enum Label {
    INVALID,
    FIRST,
    LAST,
    CASE_START,
    CASE_END,
};

// TODO(jiacheng): Use a different template for non-nullable activities.
template<typename ActivityColumn, typename ActivityType>
int findActivity(const ActivityColumn& activity_data, const NullColumn::Container* activity_nulls, uint32_t begin_offset,
                 uint32_t end_offset, const ActivityType& name, Label label) {
    if (label == CASE_START) return begin_offset;
    if (label == CASE_END) return end_offset - 1;

    int found_idx = -1;
    for (size_t i = begin_offset; i < end_offset; ++i) {
        if (activity_nulls != nullptr && (*activity_nulls)[i]) {
            continue;
        }
        if constexpr (std::is_same_v<ActivityType, Slice>) {
            auto s = activity_data.get_slice(i);
            if (s == name) {
                if (label == FIRST) {
                    found_idx = i;
                    return found_idx;
                }
                found_idx = i;
            }
        } else {
            int64_t s = 0;
            if constexpr(std::is_same_v<ActivityType, int64>) {
                s = activity_data.get(i).get_int64();
            } else if constexpr(std::is_same_v<ActivityType, int>){
                s = activity_data.get(i).get_int32();
            } else {
                std::stringstream error;
                error << "unsupported input data type in celonis_calc_throughput " << typeid(ActivityType).name() << std::endl;
                throw std::runtime_error(error.str());
            }
            if (s == name) {
                if (label == FIRST) {
                    found_idx = i;
                    return found_idx;
                }
                found_idx = i;
            }
        }
    }
    return found_idx;
}

Label parseLabel(const std::string& format) {
    if (format == "first") return FIRST;
    if (format == "last") return LAST;
    if (format == "case_start") return CASE_START;
    if (format == "case_end") return CASE_END;
    return INVALID;
}

} // namespace

template<typename ActivityColumn, LogicalType ActivityType>
ColumnPtr CelonisCalcThroughputFunctions::_celonis_calc_throughput_impl(const ActivityColumn& activity_elements,
                                                                        const UInt32Column& activity_offsets,
                                                                        const NullColumn::Container* activity_nulls,
                                                                        const NullColumn::Container* activity_array_nulls,
                                                                        ColumnPtr timestamp_col,
                                                                        ColumnPtr start_activity_col,
                                                                        ColumnPtr end_activity_col,
                                                                        ColumnPtr start_label_col, ColumnPtr end_label_col) {
    const Column* timestamp_array = timestamp_col.get();
    const NullableColumn* nullable_timestamp_array = nullptr;
    const NullColumn::Container* timestamp_array_nulls = nullptr;
    if (timestamp_array->is_nullable()) {
        nullable_timestamp_array = down_cast<const NullableColumn*>(timestamp_array);
        timestamp_array = nullable_timestamp_array->data_column().get();
        timestamp_array_nulls = &(nullable_timestamp_array->null_column()->get_data());
    }

    // timestamp_array
    const auto& timestamp_array_column = extract_array_column(timestamp_array);
    const UInt32Column& timestamp_offsets = timestamp_array_column.offsets();
    const Column* timestamp_elements = &timestamp_array_column.elements();
    const NullColumn::Container* timestamp_nulls = nullptr;
    const Int64Column::Container* timestamp_data = nullptr;
    if (timestamp_elements->has_null()) {
        timestamp_nulls = &(down_cast<const NullableColumn*>(timestamp_elements)->null_column()->get_data());
    }
    if (auto nullable = dynamic_cast<const NullableColumn*>(timestamp_elements); nullable != nullptr) {
        timestamp_data = &(down_cast<const Int64Column*>(nullable->data_column().get())->get_data());
    } else {
        timestamp_data = &(down_cast<const Int64Column*>(timestamp_elements))->get_data();
    }

    auto start_activity = ColumnViewer<ActivityType>(start_activity_col).value(0);
    auto end_activity = ColumnViewer<ActivityType>(end_activity_col).value(0);
    // TODO(jiacheng): Parse and validate the label once per query instead of per batch.
    auto start_label = parseLabel(ColumnViewer<TYPE_VARCHAR>(start_label_col).value(0).to_string());
    auto end_label = parseLabel(ColumnViewer<TYPE_VARCHAR>(end_label_col).value(0).to_string());

    const size_t num_cases = activity_offsets.size() - 1;
    ColumnBuilder<TYPE_BIGINT> result(num_cases);
    result.reserve(num_cases);

    if (start_label == INVALID || end_label == INVALID || start_label == CASE_END || end_label == CASE_START) {
        std::stringstream error;
        error << "unsupported format in celonis_calc_throughput" << std::endl;
        throw std::runtime_error(error.str());
    }

    if (activity_offsets.size() != timestamp_offsets.size()) {
        std::stringstream error;
        error << "unmatched activity offsets (" << activity_offsets.size() << ") and timestamp offsets ("
              << timestamp_offsets.size() << ") in celonis_calc_throughput" << std::endl;
        throw std::runtime_error(error.str());
    }

    auto activity_offsets_ptr = activity_offsets.get_data().data();
    auto timestamp_offsets_ptr = timestamp_offsets.get_data().data();
    for (size_t i = 0; i < num_cases; i++) {
        if (activity_array_nulls != nullptr && (*activity_array_nulls)[i]) {
            result.append_null();
            continue;
        }
        if (timestamp_array_nulls != nullptr && (*timestamp_array_nulls)[i]) {
            result.append_null();
            continue;
        }
        const size_t activity_size = activity_offsets_ptr[i + 1] - activity_offsets_ptr[i];
        const size_t timestamp_size = timestamp_offsets_ptr[i + 1] - timestamp_offsets_ptr[i];
        if (activity_size != timestamp_size) {
            std::stringstream error;
            error << "activity array and timestamp array have different sizes: " << activity_size << " vs "
                  << timestamp_size << std::endl;
            throw std::runtime_error(error.str());
        }

        int start_activity_idx = findActivity(activity_elements, activity_nulls, activity_offsets_ptr[i],
                                              activity_offsets_ptr[i + 1], start_activity, start_label);
        if (start_activity_idx < 0) {
            // Did not find the start activity.
            result.append_null();
            continue;
        }
        int end_activity_idx = findActivity(activity_elements, activity_nulls, activity_offsets_ptr[i],
                                            activity_offsets_ptr[i + 1], end_activity, end_label);
        if (end_activity_idx < 0) {
            // Did not find the end activity.
            result.append_null();
            continue;
        }

        // TODO(jiacheng): Use a different template for non-nullable timestamps.
        if (timestamp_nulls != nullptr && (*timestamp_nulls)[start_activity_idx]) {
            // Start timestamp is null.
            result.append_null();
            continue;
        }

        if (timestamp_nulls != nullptr && (*timestamp_nulls)[end_activity_idx]) {
            // End timestamp is null.
            result.append_null();
            continue;
        }

        const int64 throughput = (*timestamp_data)[end_activity_idx] - (*timestamp_data)[start_activity_idx];
        result.append(throughput);
    }

    return result.build(activity_elements.is_constant() && timestamp_col->is_constant());

}

StatusOr<ColumnPtr> CelonisCalcThroughputFunctions::celonis_calc_throughput(FunctionContext* context, const Columns& columns) {
    if (columns[0]->only_null() || columns[1]->only_null()) {
        return NullableColumn::create(Int64Column::create(columns[0]->size(), 0), NullColumn::create(columns[0]->size(), true));
    }
    const Column* activity_array =  columns[0].get();
    const NullableColumn* nullable_activity_array = nullptr;
    const NullColumn::Container* activity_array_nulls = nullptr;

    if (activity_array->is_nullable()) {
        nullable_activity_array = down_cast<const NullableColumn*>(activity_array);
        activity_array = nullable_activity_array->data_column().get();
        activity_array_nulls = &(nullable_activity_array->null_column()->get_data());
    }

    // activity_array
    const auto& activity_array_column = extract_array_column(activity_array);
    const UInt32Column& activity_offsets = activity_array_column.offsets();
    const Column* activity_elements = &activity_array_column.elements();
    const NullColumn::Container* activity_nulls = nullptr;
    if (activity_elements->has_null()) {
        activity_nulls = &(down_cast<const NullableColumn*>(activity_elements)->null_column()->get_data());
    }
    if (auto nullable = dynamic_cast<const NullableColumn*>(activity_elements); nullable != nullptr) {
        activity_elements = nullable->data_column().get();
    }


    if (typeid(*activity_elements) == typeid(BinaryColumn)) {
        return _celonis_calc_throughput_impl<BinaryColumn, TYPE_VARCHAR>(
                *down_cast<const BinaryColumn*>(activity_elements),
                activity_offsets, activity_nulls,
                activity_array_nulls, columns[1], columns[2],
                columns[3], columns[4], columns[5]);
    } else if (typeid(*activity_elements) == typeid(Int64Column)) {
        return _celonis_calc_throughput_impl<Int64Column, TYPE_BIGINT>(
                *down_cast<const Int64Column*>(activity_elements),
                activity_offsets, activity_nulls, activity_array_nulls,
                columns[1], columns[2], columns[3], columns[4],
                columns[5]);
    } else if (typeid(*activity_elements) == typeid(Int32Column)) {
        return _celonis_calc_throughput_impl<Int32Column, TYPE_INT>(
                *down_cast<const Int32Column*>(activity_elements),
                activity_offsets, activity_nulls, activity_array_nulls,
                columns[1], columns[2], columns[3], columns[4],
                columns[5]);
    } else {
        std::stringstream error_msq;
        error_msq << "unhandled input type " << typeid(*activity_elements).name();
        throw std::runtime_error(error_msq.str());
    }
}

} // namespace starrocks
