#include "exprs/celonis/qnorm.h"

#include "column/array_column.h"
#include "column/column_viewer.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"
#include <boost/math/distributions/normal.hpp>

namespace starrocks {

StatusOr<ColumnPtr>
CelonisQnorm::qnorm([[maybe_unused]] starrocks::FunctionContext* context, const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    ColumnViewer value_viewer = ColumnViewer<TYPE_DOUBLE>(columns[0]);
    const double mean = 0.0;
    const double std_dev = 1.0;
    boost::math::normal_distribution<double> dist(mean, std_dev);

    const size_t n_rows = columns[0]->size();
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
        result.append(boost::math::quantile(dist, p));
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
