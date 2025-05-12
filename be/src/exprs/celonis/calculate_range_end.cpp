#include "exprs/celonis/calculate_range_end.h"

#include "column/array_column.h"
#include "column/column_viewer.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "types/date_value.h"
#include <sstream>

namespace starrocks {

namespace {
static std::optional<std::pair<int64_t, char>> parse_step_size(const std::string& step_size) {
    if (step_size.size() < 2) {
        return std::nullopt;
    }
    char unit = step_size[step_size.length() - 1];
    if (unit != 'h' && unit != 'D' && unit != 'M' && unit != 'Y') {
        return std::nullopt;
    }
    std::istringstream iss(step_size.substr(0, step_size.length() - 1));
    int64_t number;
    char remaining;
    if (!(iss >> number)) {
        return std::nullopt;
    }
    if (iss >> remaining) {
        return std::nullopt;
    }
    return std::make_pair(number, unit);
}

static TimestampValue add_timeunits(const TimestampValue& timestamp, char time_unit, int64_t add_value) {
    std::vector<int> adds;
    const int64_t max_int = std::numeric_limits<int>::max();
    const int64_t min_int = std::numeric_limits<int>::min();
    while (add_value > max_int) {
        adds.push_back(max_int);
        add_value -= max_int;
    }
    while (add_value < min_int) {
        adds.push_back(min_int);
        add_value -= min_int;
    }
    if (add_value != 0) {
        adds.push_back(add_value);
    }
    TimestampValue rv = timestamp;
    for (auto add: adds) {
        if (time_unit == 'h') {
            rv = rv.add<TimeUnit::HOUR>(add);
        } else if (time_unit == 'D') {
            rv = rv.add<TimeUnit::DAY>(add);
        } else if (time_unit == 'M') {
            rv = rv.add<TimeUnit::MONTH>(add);
        } else {
            DCHECK_EQ(time_unit, 'Y');
            rv = rv.add<TimeUnit::YEAR>(add);
        }
    }
    return rv;
}

}

StatusOr<ColumnPtr>
CelonisCalculateRangeEnd::calculate_range_end([[maybe_unused]] starrocks::FunctionContext* context,
                                              const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 3);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    size_t n_rows = columns[0]->size();
    ColumnViewer start_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer step_size_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);
    ColumnViewer step_count_viewer = ColumnViewer<TYPE_BIGINT>(columns[2]);
    ColumnBuilder<TYPE_DATETIME> result(n_rows);
    for (auto row = 0; row < n_rows; ++row) {
        if (start_viewer.is_null(row) || step_size_viewer.is_null(row) || step_count_viewer.is_null(row)) {
            result.append_null();
            continue;
        }
        std::string step_size = step_size_viewer.value(row).to_string();
        auto number_unit = parse_step_size(step_size);
        if (!number_unit.has_value()) {
            return Status::InvalidArgument(
                    fmt::format("CELONIS_CALCULATE_RANGE_END: Invalid step size {}.", step_size));
        }
        const auto number = number_unit->first;
        const auto time_unit = number_unit->second;
        const auto start_timestamp = start_viewer.value(row);
        auto step_count = step_count_viewer.value(row);
        int128_t add_value = static_cast<int128_t>(number) * static_cast<int128_t>(step_count);
        if (add_value > std::numeric_limits<int64_t>::max() || add_value < std::numeric_limits<int64_t>::min()) {
            result.append_null();
            continue;
        }
        auto end_timestamp = add_timeunits(start_timestamp, time_unit, number * step_count);
        result.append(end_timestamp);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
