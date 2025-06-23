#include "exprs/celonis/patindex.h"

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_viewer.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"
#include "exprs/celonis/match_pattern_util.h"

namespace starrocks {

struct PatindexStateFragmentLocal {
    std::optional<std::string> pattern = std::nullopt;
    ScalarFunction function;
};

Status CelonisPatindex::prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new PatindexStateFragmentLocal();
    context->set_function_state(scope, state);

    if (context->get_constant_column(1) == nullptr) {
        state->function = patindex_non_constant_pattern;
        return Status::OK();
    }

    state->function = patindex_constant_pattern;

    auto pattern_column = context->get_constant_column(1);
    if (pattern_column->empty()) {
        return Status::OK();
    }

    if (pattern_column->is_null(0)) {
        return Status::OK();
    }

    state->pattern = pattern_column->get(0).get_slice().to_string();
    return Status::OK();
}

Status CelonisPatindex::close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const PatindexStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr>
CelonisPatindex::patindex_non_constant_pattern([[maybe_unused]]FunctionContext* context,
                                               const Columns& columns) {
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    ColumnViewer pattern_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);
    const bool has_occurrence = columns.size() == 3;
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (size_t row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row) || (has_occurrence && columns[2]->is_null(row))) {
            result.append_null();
            continue;
        }
        const auto input_string = input_string_viewer.value(row).to_string();
        const auto pattern = pattern_viewer.value(row).to_string();
        result.append(pattern_index(input_string.data(), pattern.data(),
                                    (has_occurrence ? columns[2]->get(row).get_int64() : 1)));
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisPatindex::patindex_constant_pattern([[maybe_unused]]FunctionContext* context,
                                                               const Columns& columns) {
    ColumnViewer input_string_viewer = ColumnViewer<TYPE_VARCHAR>(columns[0]);
    const bool has_occurrence = columns.size() == 3;
    const auto* state = reinterpret_cast<const PatindexStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    const std::optional<std::string>& pattern = state->pattern;
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (size_t row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || !pattern.has_value() || (has_occurrence && columns[2]->is_null(row))) {
            result.append_null();
            continue;
        }
        const std::string input_string = input_string_viewer.value(row).to_string();
        result.append(pattern_index(input_string.data(), pattern->data(),
                                    (has_occurrence ? columns[2]->get(row).get_int64() : 1)));
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisPatindex::patindex(FunctionContext* context, const Columns& columns) {
    DCHECK(columns.size() == 2 || columns.size() == 3);
    const auto* state = reinterpret_cast<const PatindexStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

} // namespace starrocks
