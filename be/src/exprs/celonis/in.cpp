#include "exprs/celonis/in.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

// To use SliceHashSet for TYPE_VARCHAR. Copied from ../in_const_predicate.hpp.
template<LogicalType LT, typename Enable = void>
struct LHashSet {
    using LType = HashSet<RunTimeCppType<LT>>;
};

template<LogicalType LT>
struct LHashSet<LT, std::enable_if_t<isSliceLT<LT>>> {
    using LType = SliceHashSet;
};

template<LogicalType LT>
using LHashSetType = typename LHashSet<LT>::LType;

} // namespace

template<LogicalType LT>
struct InStateFragmentLocal {
    LHashSetType<LT> match_set;
    bool match_has_null = false;
    ScalarFunction function;
};

template<LogicalType LT>
Status CelonisIn<LT>::prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new InStateFragmentLocal<LT>();
    context->set_function_state(scope, state);

    auto match_column = context->get_constant_column(1);
    // context->is_constant_column(1) must not be used to determine if the argument is Array Literal because as of
    // 2024-01-30 it returns false for Array Literal while get_constant_column(1) returns non nullptr.
    // For the same reason, columns[1]->is_constant() must not be used in celonis_in().
    if (match_column == nullptr) {
        state->function = in_non_constant_match;
        return Status::OK();
    }
    state->function = in_constant_match;

    if (match_column->is_null(0)) {
        return Status::OK();
    }

    auto match_array = match_column->get(0).get_array();
    for (const auto& element: match_array) {
        if (element.is_null()) {
            state->match_has_null = true;
        } else {
            state->match_set.insert(element.get<RunTimeCppType<LT>>());
        }
    }

    return Status::OK();
}

template<LogicalType LT>
Status CelonisIn<LT>::close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const InStateFragmentLocal<LT>*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisIn<LT>::in_non_constant_match([[maybe_unused]]FunctionContext* context,
                                                         const Columns& columns) {
    const auto& value_column = columns[0];
    const auto& match_column = columns[1];
    auto num_rows = value_column->size();
    DCHECK_EQ(match_column->size(), num_rows);

    ColumnViewer<LT> value_viewer(value_column);
    ColumnBuilder<TYPE_BOOLEAN> result(num_rows);

    for (auto row = 0; row < num_rows; ++row) {
        auto match_datum = match_column->get(row);
        if (match_datum.is_null()) {
            // This case cannot happen and is not defined in PQL IN.
            result.append_null();
            continue;
        }
        bool match = false;
        if (value_viewer.is_null(row)) {
            for (const auto& element: match_datum.get_array()) {
                if (element.is_null()) {
                    match = true;
                    break;
                }
            }
        } else {
            const auto& value = value_viewer.value(row);
            for (const auto& element: match_datum.get_array()) {
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

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisIn<LT>::in_constant_match([[maybe_unused]]FunctionContext* context, const Columns& columns) {
    const auto& value_column = columns[0];
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);

    ColumnViewer<LT> value_viewer(value_column);
    ColumnBuilder<TYPE_BOOLEAN> result(num_rows);

    const auto* state = reinterpret_cast<const InStateFragmentLocal<LT>*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    for (auto row = 0; row < num_rows; ++row) {
        if (value_viewer.is_null(row)) {
            result.append(state->match_has_null);
        } else {
            result.append(state->match_set.count(value_viewer.value(row)) > 0);
        }
    }

    return result.build(all_const);
}

template<LogicalType LT>
StatusOr<ColumnPtr> CelonisIn<LT>::in(FunctionContext* context, const Columns& columns) {
    const auto* state = reinterpret_cast<const InStateFragmentLocal<LT>*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

template
class CelonisIn<TYPE_INT>;

template
class CelonisIn<TYPE_BIGINT>;

template
class CelonisIn<TYPE_DOUBLE>;

template
class CelonisIn<TYPE_DATETIME>;

template
class CelonisIn<TYPE_VARCHAR>;

} // namespace starrocks
