#include "exprs/celonis/remap_values.h"

#include "column/array_column.h"
#include "column/column_helper.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"

namespace starrocks {

struct RemapValuesStateFragmentLocal {
    // TODO(y.zhang): Switch to use hash map.
    DatumMap value_map;
    ScalarFunction function;
};

Status CelonisRemapValues::prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new RemapValuesStateFragmentLocal();
    context->set_function_state(scope, state);

    auto old_value_column = context->get_constant_column(1);
    auto new_value_column = context->get_constant_column(2);
    // context->is_constant_column(i) must not be used to determine if the argument is Array Literal because as of
    // 2024-01-30 it returns false for Array Literal while get_constant_column(i) returns non nullptr.
    // For the same reason, columns[i]->is_constant() must not be used in celonis_remap_values().
    if (old_value_column == nullptr || new_value_column == nullptr) {
        state->function = remap_values_non_constant_value_map;
        return Status::OK();
    }
    state->function = remap_values_constant_value_map;

    if (old_value_column->is_null(0) || new_value_column->is_null(0)) {
        return Status::OK();
    }

    auto old_value_array = old_value_column->get(0).get_array();
    auto new_value_array = new_value_column->get(0).get_array();
    if (old_value_array.size() != new_value_array.size()) {
        return Status::InvalidArgument("[prepare] old value array must have the same length as new value array.");
    }
    const auto size = old_value_array.size();
    for (auto i = 0; i < size; ++i) {
        state->value_map[old_value_array[i].convert2DatumKey()] = new_value_array[i];
    }
    return Status::OK();
}

Status CelonisRemapValues::close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const RemapValuesStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr>
CelonisRemapValues::remap_values_non_constant_value_map([[maybe_unused]]FunctionContext* context,
                                                        const Columns& columns) {
    const auto& value_column = columns[0];
    const auto& old_value_column = columns[1];
    const auto& new_value_column = columns[2];
    const bool has_default = columns.size() == 4;
    auto num_rows = value_column->size();
    DCHECK_EQ(old_value_column->size(), num_rows);
    DCHECK_EQ(new_value_column->size(), num_rows);
    if (has_default) {
        DCHECK_EQ(columns[3]->size(), num_rows);
    }

    auto unfolded_value_column = ColumnHelper::unfold_const_column(
            TypeDescriptor::from_logical_type(context->get_arg_type(0)->type), columns[0]->size(), columns[0]);
    auto result = NullableColumn::wrap_if_necessary(unfolded_value_column->clone_empty());

    for (int row = 0; row < num_rows; ++row) {
        auto old_value_datum = old_value_column->get(row);
        auto new_value_datum = new_value_column->get(row);
        if (old_value_datum.is_null() || new_value_datum.is_null()) {
            result->append_nulls(1);
            continue;
        }
        const auto& old_value_array = old_value_datum.get_array();
        const auto& new_value_array = new_value_datum.get_array();
        if (old_value_array.size() != new_value_array.size()) {
            return Status::InvalidArgument("old value array must have the same length as new value array.");
        }
        const auto size = old_value_array.size();
        DatumMap value_map;
        for (auto i = 0; i < size; ++i) {
            value_map[old_value_array[i].convert2DatumKey()] = new_value_array[i];
        }
        auto value = value_column->get(row);
        auto it = value_map.find(value.convert2DatumKey());
        if (it == value_map.end()) {
            result->append_datum(has_default ? columns[3]->get(row) : value);
        } else {
            result->append_datum(it->second);
        }
    }
    return result;
}

StatusOr<ColumnPtr> CelonisRemapValues::remap_values_constant_value_map([[maybe_unused]]FunctionContext* context,
                                                                        const Columns& columns) {
    const auto& value_column = columns[0];
    const bool has_default = columns.size() == 4;
    auto unfolded_value_column = ColumnHelper::unfold_const_column(
            TypeDescriptor::from_logical_type(context->get_arg_type(0)->type), columns[0]->size(), columns[0]);
    auto result = NullableColumn::wrap_if_necessary(unfolded_value_column->clone_empty());
    const auto* state = reinterpret_cast<const RemapValuesStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    auto num_rows = value_column->size();
    if (has_default) {
        DCHECK_EQ(columns[3]->size(), num_rows);
    }
    for (int row = 0; row < num_rows; ++row) {
        auto value = value_column->get(row);
        auto it = state->value_map.find(value.convert2DatumKey());
        if (it == state->value_map.end()) {
            result->append_datum(has_default ? columns[3]->get(row) : value);
        } else {
            result->append_datum(it->second);
        }
    }
    return result;
}

StatusOr<ColumnPtr> CelonisRemapValues::remap_values(FunctionContext* context, const Columns& columns) {
    DCHECK(columns.size() == 3 || columns.size() == 4);
    const auto* state = reinterpret_cast<const RemapValuesStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

} // namespace starrocks
