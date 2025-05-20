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

template<typename ActivityCppType>
int findActivity(const ActivityCppType* activity_elements, const NullColumn::Container* activity_nulls, uint32_t begin_offset,
                 uint32_t end_offset, const ActivityCppType& name, Label label) {
    int64_t begin = static_cast<int64_t>(begin_offset);
    int64_t end = static_cast<int64_t>(end_offset);
    // Saola does not ignore NULLs for CASE_START and CASE_END
    switch (label) {
        case CASE_START:
            return begin;
        case CASE_END:
            return end - 1;
        case FIRST:
            for (auto i = begin; i < end; ++i) {
                if (activity_nulls != nullptr && (*activity_nulls)[i]) {
                    continue;
                }
                if (activity_elements[i] == name) {
                    return i;
                }
            }
            return -1;
        case LAST:
            for (auto i = end - 1; i >= begin; --i) {
                if (activity_nulls != nullptr && (*activity_nulls)[i]) {
                    continue;
                }
                if (activity_elements[i] == name) {
                    return i;
                }
            }
            return -1;
        default:
            return -1;
    }
}

Label parseLabel(const std::string& format) {
    if (format == "first") return FIRST;
    if (format == "last") return LAST;
    if (format == "case_start") return CASE_START;
    if (format == "case_end") return CASE_END;
    return INVALID;
}

} // namespace

template<LogicalType ActivityLT>
StatusOr<ColumnPtr> CelonisCalcThroughputFunctions<ActivityLT>::celonis_calc_throughput([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    using ActivityColumn = RunTimeColumnType<ActivityLT>;
    using TimestampColumn = RunTimeColumnType<TYPE_BIGINT>;

    RETURN_IF_COLUMNS_ONLY_NULL({ columns[0] });
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[1] });
    if (UNLIKELY(columns[0]->size() != columns[1]->size())) {
        std::stringstream error;
        error << "unmatched activity offsets (" << columns[0]->size() << ") and timestamp offsets ("
              << columns[1]->size() << ") in celonis_calc_throughput" << std::endl;
        throw std::runtime_error(error.str());
    }

    ColumnPtr activity_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
    UnnestedArrayData activity_array_data = prepare_array_input(activity_column.get());
    const auto* activity_elements = down_cast<const ActivityColumn*>(activity_array_data.elements)->get_data().data();

    ColumnPtr timestamp_column = ColumnHelper::unpack_and_duplicate_const_column(columns[1]->size(), columns[1]);
    UnnestedArrayData timestamp_array_data = prepare_array_input(timestamp_column.get());
    const auto* timestamp_elements = down_cast<const TimestampColumn*>(timestamp_array_data.elements)->get_data().data();

    auto start_activity = ColumnViewer<ActivityLT>(columns[2]).value(0);
    auto end_activity = ColumnViewer<ActivityLT>(columns[3]).value(0);
    auto start_label = parseLabel(ColumnViewer<TYPE_VARCHAR>(columns[4]).value(0).to_string());
    auto end_label = parseLabel(ColumnViewer<TYPE_VARCHAR>(columns[5]).value(0).to_string());
    if (start_label == INVALID || end_label == INVALID || start_label == CASE_END || end_label == CASE_START) {
        std::stringstream error;
        error << "unsupported format in celonis_calc_throughput" << std::endl;
        throw std::runtime_error(error.str());
    }

    const size_t num_cases = columns[0]->size();
    ColumnBuilder<TYPE_BIGINT> result(num_cases);
    result.reserve(num_cases);

    auto activity_offsets_ptr = activity_array_data.offsets->get_data().data();
    auto timestamp_offsets_ptr = timestamp_array_data.offsets->get_data().data();
    for (size_t i = 0; i < num_cases; i++) {
        if (activity_array_data.null_arrays != nullptr && (*activity_array_data.null_arrays)[i]) {
            result.append_null();
            continue;
        }
        if (timestamp_array_data.null_arrays != nullptr && (*timestamp_array_data.null_arrays)[i]) {
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

        int start_activity_idx = findActivity(activity_elements, activity_array_data.null_elements, activity_offsets_ptr[i],
                                              activity_offsets_ptr[i + 1], start_activity, start_label);
        if (start_activity_idx < 0) {
            // Did not find the start activity.
            result.append_null();
            continue;
        }
        int end_activity_idx = findActivity(activity_elements, activity_array_data.null_elements, activity_offsets_ptr[i],
                                            activity_offsets_ptr[i + 1], end_activity, end_label);
        if (end_activity_idx < 0) {
            // Did not find the end activity.
            result.append_null();
            continue;
        }

        if (timestamp_array_data.null_elements != nullptr && (*timestamp_array_data.null_elements)[start_activity_idx]) {
            // Start timestamp is null.
            result.append_null();
            continue;
        }

        if (timestamp_array_data.null_elements != nullptr && (*timestamp_array_data.null_elements)[end_activity_idx]) {
            // End timestamp is null.
            result.append_null();
            continue;
        }
        if (end_activity_idx <= start_activity_idx) {
            result.append_null();
            continue;
        }
        const int64 throughput = timestamp_elements[end_activity_idx] - timestamp_elements[start_activity_idx];
        result.append(throughput);
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

template
class CelonisCalcThroughputFunctions<TYPE_INT>;

template
class CelonisCalcThroughputFunctions<TYPE_BIGINT>;

template
class CelonisCalcThroughputFunctions<TYPE_VARCHAR>;
} // namespace starrocks
