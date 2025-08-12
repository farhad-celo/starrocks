#include "exprs/celonis/in.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
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
ColumnPtr CelonisIn::celonis_in_non_constant_match(const Columns& columns) {
    const auto& value_column = columns[0];
    const auto& match_column = columns[1];
    auto num_rows = value_column->size();
    DCHECK_EQ(match_column->size(), num_rows);

    ColumnViewer<Type> value_viewer(value_column);
    ColumnBuilder<TYPE_BOOLEAN> result(num_rows);

    for (int row = 0; row < num_rows; ++row) {
        auto match_datum = match_column->get(row);
        if (match_datum.is_null()) {
            // This case cannot happen and is not defined in PQL IN.
            result.append_null();
            continue;
        }
        bool match = false;
        if (value_viewer.is_null(row)) {
            for (const auto& element : match_datum.get_array()) {
                if (element.is_null()) {
                    match = true;
                    break;
                }
            }
        } else {
            const auto& value = value_viewer.value(row);
            for (const auto& element : match_datum.get_array()) {
                if (!element.is_null() && element.is_equal(value)) {
                    match = true;
                    break;
                }
            }
        }
        result.append(match);
    }
    return result.build(/*is_const=*/false);
}

template <LogicalType Type>
ColumnPtr CelonisIn::celonis_in_constant_match(const Columns& columns) {
    const auto& value_column = columns[0];
    const auto& match_column = columns[1];
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);

    ColumnViewer<Type> value_viewer(value_column);
    ColumnBuilder<TYPE_BOOLEAN> result(num_rows);

    auto match_array = match_column->get(0).get_array();
    bool match_has_null = false;
    LHashSetType<Type> hash_set;

    for (const auto& element : match_array) {
        if (element.is_null()) {
            match_has_null = true;
        } else {
            hash_set.insert(element.get<RunTimeCppType<Type>>());
        }
    }

    for (int row = 0; row < num_rows; ++row) {
        if (value_viewer.is_null(row)) {
            result.append(match_has_null);
        } else {
            result.append(hash_set.count(value_viewer.value(row)) > 0);
        }
    }

    return result.build(all_const);
}

template <LogicalType Type>
ColumnPtr CelonisIn::celonis_in_impl(const Columns& columns) {
    if (columns[1]->is_constant()) {
        return celonis_in_constant_match<Type>(columns);
    } else {
        return celonis_in_non_constant_match<Type>(columns);
    }
}

StatusOr<ColumnPtr> CelonisIn::celonis_in(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    DCHECK_EQ(context->get_arg_type(1)->type, TYPE_ARRAY);
    DCHECK(ColumnHelper::get_data_column(columns[1].get())->is_array());

    switch (context->get_arg_type(0)->type) {
    case TYPE_VARCHAR:
        return celonis_in_impl<TYPE_VARCHAR>(columns);
    case TYPE_INT:
        return celonis_in_impl<TYPE_INT>(columns);
    case TYPE_BIGINT:
        return celonis_in_impl<TYPE_BIGINT>(columns);
    case TYPE_DOUBLE:
        return celonis_in_impl<TYPE_DOUBLE>(columns);
    case TYPE_DATETIME:
        return celonis_in_impl<TYPE_DATETIME>(columns);
    default:
        std::stringstream error_msq;
        error_msq << "unhandled input type " << logical_type_to_string(context->get_arg_type(0)->type);
        throw std::runtime_error(error_msq.str());
    }
}

} // namespace starrocks
