#include "exprs/celonis/qnorm.h"

#include <cmath>
#include "column/array_column.h"
#include "column/column_viewer.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

// Abramowitz-Stegun Formula 26.2.23
[[nodiscard]] double compute_qnorm(double input) {
    double result = 0.0;

    // Constants
    constexpr double split = 0.42;
    constexpr double a0 = 2.50662823884;
    constexpr double a1 = -18.61500062529;
    constexpr double a2 = 41.39119773534;
    constexpr double a3 = -25.44106049637;
    constexpr double b1 = -8.47351093090;
    constexpr double b2 = 23.08336743743;
    constexpr double b3 = -21.06224101826;
    constexpr double b4 = 3.13082909833;
    constexpr double c0 = -2.78718931138;
    constexpr double c1 = -2.29796479134;
    constexpr double c2 = 4.85014127135;
    constexpr double c3 = 2.32121276858;
    constexpr double d1 = 3.54388924762;
    constexpr double d2 = 1.63706781897;

    double q = input - 0.5;

    if (std::fabs(q) <= split) {
        // Central region
        double r = q * q;
        result = q * (((a3 * r + a2) * r + a1) * r + a0) /
                 ((((b4 * r + b3) * r + b2) * r + b1) * r + 1.0);
    } else {
        // Tail regions
        double r = (q > 0.0) ? (1.0 - input) : input;
        r = std::sqrt(-std::log(r));
        result = (((c3 * r + c2) * r + c1) * r + c0) /
                 ((d2 * r + d1) * r + 1.0);
        if (q < 0.0) {
            result = -result;
        }
    }

    return result;
}

}

StatusOr<ColumnPtr>
CelonisQnorm::qnorm([[maybe_unused]] starrocks::FunctionContext* context, const starrocks::Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    DCHECK_EQ(columns.size(), 1);
    const auto [all_const, n_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer value_viewer = ColumnViewer<TYPE_DOUBLE>(columns[0]);

    ColumnBuilder<TYPE_DOUBLE> result(n_rows);
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        const double p = value_viewer.value(row);
        if (p <= 0 || p >= 1) {
            result.append_null();
            continue;
        }
        result.append(compute_qnorm(p));
    }
    return result.build(all_const);
}

} // namespace starrocks
