#include "exprs/celonis/math_functions.h"

#include "column/array_column.h"
#include "column/column_viewer.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"

namespace starrocks {

template<typename T>
std::optional<T> safe_square(const T& value) {
    if constexpr (std::is_integral_v<T>) {
        if (value == 0) {
            return 0;
        }
        T pos_value = std::abs(value);
        if (pos_value > std::numeric_limits<T>::max() / pos_value) {
            return std::nullopt;
        }
        return pos_value * pos_value;
    } else if constexpr (std::is_floating_point_v<T>) {
        return value * value;
    } else {
        return std::nullopt;
    }
}

template<LogicalType LT>
StatusOr<ColumnPtr>
CelonisMathFunctions<LT>::square([[maybe_unused]] starrocks::FunctionContext* context,
                                 const starrocks::Columns& columns) {
    using CppType = RunTimeCppValueType<LT>;
    DCHECK_EQ(columns.size(), 1);
    ColumnViewer value_viewer = ColumnViewer<LT>(columns[0]);

    const size_t n_rows = columns[0]->size();
    ColumnBuilder<LT> result(n_rows);
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        auto square_value = safe_square<CppType>(value_viewer.value(row));
        if (square_value.has_value()) {
            result.append(square_value.value());
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

template
class CelonisMathFunctions<TYPE_INT>;

template
class CelonisMathFunctions<TYPE_BIGINT>;

template
class CelonisMathFunctions<TYPE_DOUBLE>;

} // namespace starrocks
