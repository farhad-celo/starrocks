#include "exprs/celonis/in.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

// To use SliceHashSet for TYPE_VARCHAR. Copied from ../in_const_predicate.hpp.

template <LogicalType Type, typename Enable = void>
struct LHashSet {
    using LType = HashSet<RunTimeCppType<Type>>;
};

template <LogicalType Type>
struct LHashSet<Type, std::enable_if_t<isSliceLT<Type>>> {
    using LType = SliceHashSet;
};

template <LogicalType Type>
using LHashSetType = typename LHashSet<Type>::LType;

} // namespace

template <LogicalType Type>
ColumnPtr CelonisIn::_celonis_in_impl(ColumnPtr input_column, const DatumArray& match_array) {
    bool match_has_null = false;

    LHashSetType<Type> hash_set;

    for (const auto& element : match_array) {
        if (element.is_null()) {
            match_has_null = true;
        } else {
            hash_set.insert(element.get<RunTimeCppType<Type>>());
        }
    }

    ColumnViewer<Type> data_column(input_column);
    auto size = input_column->size();
    ColumnBuilder<TYPE_BOOLEAN> result(size);

    for (int row = 0; row < size; ++row) {
        if (data_column.is_null(row)) {
            result.append(match_has_null);
        } else {
            result.append(hash_set.count(data_column.value(row)) > 0);
        }
    }

    return result.build(/*is_const=*/false);
}

StatusOr<ColumnPtr> CelonisIn::celonis_in(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    DCHECK(columns[1]->is_array());
    DCHECK_EQ(columns[1]->size(), 1);

    auto match_array = columns[1]->get(0).get_array();

    switch (context->get_arg_type(0)->type) {
    case TYPE_VARCHAR:
        return _celonis_in_impl<TYPE_VARCHAR>(columns[0], match_array);
    case TYPE_INT:
        return _celonis_in_impl<TYPE_INT>(columns[0], match_array);
    case TYPE_BIGINT:
        return _celonis_in_impl<TYPE_BIGINT>(columns[0], match_array);
    case TYPE_DOUBLE:
        return _celonis_in_impl<TYPE_DOUBLE>(columns[0], match_array);
    case TYPE_DATETIME:
        return _celonis_in_impl<TYPE_DATETIME>(columns[0], match_array);
    default:
        std::stringstream error_msq;
        error_msq << "unhandled input type " << logical_type_to_string(context->get_arg_type(0)->type);
        throw std::runtime_error(error_msq.str());
    }
}

} // namespace starrocks
