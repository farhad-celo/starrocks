#include "exprs/celonis/array_end_finder.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "exprs/celonis/util.h"

namespace starrocks {

enum class Direction { FORWARD, BACKWARD };

template<LogicalType LT, Direction DIR>
StatusOr<ColumnPtr> find_element(FunctionContext* context,
                                 const Columns& columns) {
    DCHECK_EQ(1, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());

    const auto& elements = down_cast<const RunTimeColumnType<LT>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();
    const auto& null_elements = array_data.null_elements;
    const bool has_null_elements = null_elements != nullptr;

    ColumnBuilder<LT> result(num_rows);
    for (auto row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        const int64_t start = static_cast<int64_t>(offsets[row]);
        const int64_t end = static_cast<int64_t>(offsets[row + 1]);
        bool processed = false;
        if constexpr (DIR == Direction::FORWARD) {
            for (auto i = start; i < end; ++i) {
                if (has_null_elements && (*null_elements)[i] != 0) {
                    continue;
                }
                processed = true;
                result.append(elements[i]);
                break;
            }
        } else {
            for (auto i = end - 1; i >= start; --i) {
                if (has_null_elements && (*null_elements)[i] != 0) {
                    continue;
                }
                processed = true;
                result.append(elements[i]);
                break;
            }
        }
        if (!processed) {
            result.append_null();
        }
    }
    return result.build(all_const);
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisArrayEndFinder<LT>::array_first(FunctionContext* context, const Columns& columns) {
    return find_element<LT, Direction::FORWARD>(context, columns);
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisArrayEndFinder<LT>::array_last(FunctionContext* context, const Columns& columns) {
    return find_element<LT, Direction::BACKWARD>(context, columns);
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
