#include "exprs/celonis/array_end_finder.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "exprs/celonis/util.h"

namespace starrocks {

template<LogicalType LT>
StatusOr<ColumnPtr>
CelonisArrayEndFinder<LT>::array_first([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(1, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});
    const auto num_rows = columns[0]->size();
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& elements = down_cast<const RunTimeColumnType<LT>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();
    ColumnBuilder<LT> result(num_rows);
    for (auto row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        bool found = false;
        for (auto i = start; i < end; ++i) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[i] != 0) {
                continue;
            }
            found = true;
            result.append(elements[i]);
            break;
        }
        if (not found) {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

template<LogicalType LT>
StatusOr<ColumnPtr>
CelonisArrayEndFinder<LT>::array_last([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(1, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});
    const auto num_rows = columns[0]->size();
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& elements = down_cast<const RunTimeColumnType<LT>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();
    ColumnBuilder<LT> result(num_rows);
    for (auto row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        const auto start = static_cast<int64_t>(offsets[row]);
        const auto end = static_cast<int64_t>(offsets[row + 1]);
        DCHECK(end >= start);
        bool found = false;
        for (auto i = end - 1; i >= start; --i) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[i] != 0) {
                continue;
            }
            found = true;
            result.append(elements[i]);
            break;
        }
        if (not found) {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

template
class CelonisArrayEndFinder<TYPE_INT>;

template
class CelonisArrayEndFinder<TYPE_BIGINT>;

template
class CelonisArrayEndFinder<TYPE_DOUBLE>;

template
class CelonisArrayEndFinder<TYPE_DATETIME>;

template
class CelonisArrayEndFinder<TYPE_VARCHAR>;

} // namespace starrocks
