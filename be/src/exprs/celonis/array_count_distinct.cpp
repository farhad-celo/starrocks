#include "exprs/celonis/array_count_distinct.h"

#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/hash_set.h"
#include "exprs/builtin_functions.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

// To use SliceHashSet for TYPE_VARCHAR. Copied from ../in_const_predicate.hpp.
template <LogicalType LT, typename Enable = void>
struct LHashSet {
    using LType = HashSet<RunTimeCppType<LT>>;
};

template <LogicalType LT>
struct LHashSet<LT, std::enable_if_t<isSliceLT<LT>>> {
    using LType = SliceHashSet;
};

template <LogicalType LT>
using LHashSetType = typename LHashSet<LT>::LType;

} // namespace

template <LogicalType LT>
StatusOr<ColumnPtr> CelonisArrayCountDistinct<LT>::array_count_distinct(
        [[maybe_unused]] starrocks::FunctionContext* context, const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const auto [all_const, n_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& elements = down_cast<const RunTimeColumnType<LT>&>(*array_data.elements).get_data().data();
    const auto& offsets = array_data.offsets->get_data().data();
    const auto* null_elements = array_data.null_elements;

    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    LHashSetType<LT> elements_seen;
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        elements_seen.clear();
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        for (auto i = start; i < end; ++i) {
            if (null_elements != nullptr && (*null_elements)[i] != 0) {
                continue;
            }
            elements_seen.insert(elements[i]);
        }
        result.append(elements_seen.size());
    }
    return result.build(all_const);
}

template class CelonisArrayCountDistinct<TYPE_INT>;

template class CelonisArrayCountDistinct<TYPE_BIGINT>;

template class CelonisArrayCountDistinct<TYPE_DOUBLE>;

template class CelonisArrayCountDistinct<TYPE_DATETIME>;

template class CelonisArrayCountDistinct<TYPE_VARCHAR>;

} // namespace starrocks
