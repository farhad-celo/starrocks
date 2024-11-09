#include "exprs/celonis/match_activities.h"

#include "column/column_builder.h"
#include "column/hash_set.h"
#include "exprs/builtin_functions.h"
#include "exprs/celonis/util.h"

namespace starrocks {

namespace {

struct MatchConfig {
    // STARTING: case has to start with specified activity
    // NODE: case has to have the specified activities
    // ENDING: case has to end with specific activity
    // EXCLUDING: case must not have the specified activities (and must have at least one non-NULL activity)
    // EXCLUDING_ALL: case must not have any of the specified activities (and must have at least one non-NULL activity)
    // NODES_ANY: case has to have at least one of the specified activities
    SliceHashSet start_nodes;
    SliceHashSet nodes;
    SliceHashSet end_nodes;
    SliceHashSet excluding_nodes;
    SliceHashSet excluding_all_nodes;
    SliceHashSet any_nodes;
};

struct MatchActivitiesStateFragmentLocal {
    MatchConfig match_config;
    ScalarFunction function;
};

int64_t
_match_activities(size_t row, const UnnestedArrayData& activity_array_data,
                  const Slice* activities,
                  const unsigned int* offsets,
                  const SliceHashSet& start_nodes,
                  const SliceHashSet& nodes,
                  const SliceHashSet& end_nodes,
                  const SliceHashSet& excluding_nodes,
                  const SliceHashSet& excluding_all_nodes,
                  const SliceHashSet& any_nodes) {
    const auto& null_elements = activity_array_data.null_elements;
    SliceHashSet nodes_seen;
    nodes_seen.reserve(nodes.size());
    SliceHashSet excluding_nodes_seen;
    excluding_nodes_seen.reserve(excluding_all_nodes.size());
    // contain node in any_nodes
    bool has_any_node = false;
    bool has_non_null = false;
    const size_t offset_start = offsets[row];
    const size_t offset_end = offsets[row + 1];
    const auto length = offset_end - offset_start;
    if (length < nodes.size()) {
        return 0L;
    }
    std::optional<int64_t> first_visit_index = std::nullopt;
    std::optional<int64_t> last_visit_index = std::nullopt;
    // If true, traverse the activity array from left to right.
    bool left_to_right = true;
    if (start_nodes.empty() && !end_nodes.empty()) {
        left_to_right = false;
    } else if (!start_nodes.empty() && !end_nodes.empty()) {
        left_to_right = start_nodes.size() <= end_nodes.size();
    }
    const int64_t start = left_to_right ? static_cast<int64_t>(offset_start) : static_cast<int64_t>(offset_end) - 1;
    const int64_t end = left_to_right ? static_cast<int64_t>(offset_end) : static_cast<int64_t>(offset_start) - 1;
    const int64_t delta_index = left_to_right ? 1 : -1;
    const SliceHashSet& first_visit_nodes = left_to_right ? start_nodes : end_nodes;
    const SliceHashSet& last_visit_nodes = left_to_right ? end_nodes : start_nodes;
    for (auto index = start; index != end; index += delta_index) {
        // Nulls are ignored
        if (null_elements != nullptr && (*null_elements)[index] != 0) {
            continue;
        }
        has_non_null = true;
        if (!first_visit_index.has_value()) {
            first_visit_index = index;
            if (!first_visit_nodes.empty() && first_visit_nodes.find(activities[index]) == first_visit_nodes.end()) {
                return 0L;
            }
        }
        last_visit_index = index;
        const auto& value = activities[index];
        if (nodes.find(value) != nodes.end()) {
            nodes_seen.insert(value);
        }

        // activity array does not contain all the activities in nodes, return early.
        if (nodes_seen.size() + std::abs(end - index) - 1 < nodes.size()) {
            return 0L;
        }

        if (excluding_all_nodes.find(value) != excluding_all_nodes.end()) {
            excluding_nodes_seen.insert(value);
        }
        if (!has_any_node && any_nodes.find(value) != any_nodes.end()) {
            has_any_node = true;
        }
        if (excluding_nodes.find(value) != excluding_nodes.end()) {
            return 0L;
        }
    }
    // No non-NULL activities.
    if (!first_visit_nodes.empty() && !first_visit_index.has_value()) {
        return 0L;
    }
    if (!last_visit_nodes.empty() &&
        (!last_visit_index.has_value() ||
         last_visit_nodes.find(activities[last_visit_index.value()]) == last_visit_nodes.end())) {
        return 0L;
    }
    if (!any_nodes.empty() && !has_any_node) {
        return 0L;
    }
    if (!excluding_all_nodes.empty() && excluding_nodes_seen.size() == excluding_all_nodes.size()) {
        return 0L;
    }
    if (nodes_seen.size() == nodes.size() && (excluding_nodes.empty() || has_non_null) &&
        (excluding_all_nodes.empty() || has_non_null)) {
        return 1L;
    }
    return 0L;
}

void _populate_filter(const ColumnPtr& column, int row, SliceHashSet& filter) {
    if (!column->is_null(row)) {
        auto start_node_array = column->get(row).get_array();
        for (const auto& value: start_node_array) {
            if (!value.is_null()) {
                filter.insert(value.get_slice());
            }
        }
    }
}

} // namespace

Status CelonisMatchActivitiesFunctions::prepare(starrocks::FunctionContext* context,
                                                FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }
    auto state = new MatchActivitiesStateFragmentLocal();
    context->set_function_state(scope, state);

    auto start_nodes_column = context->get_constant_column(1);
    auto nodes_column = context->get_constant_column(2);
    auto end_nodes_column = context->get_constant_column(3);
    auto excluding_nodes_column = context->get_constant_column(4);
    auto excluding_all_nodes_column = context->get_constant_column(5);
    auto any_nodes_column = context->get_constant_column(6);

    if (start_nodes_column == nullptr || nodes_column == nullptr || end_nodes_column == nullptr ||
        excluding_nodes_column == nullptr || excluding_all_nodes_column == nullptr || any_nodes_column == nullptr) {
        state->function = celonis_match_activities_non_constant_config;
        if (config::fail_query_when_expensive_non_const_impl_is_called) {
            return Status::InvalidArgument("The non-const version of CELONIS_MATCH_ACTIVITIES should not be called.");
        }
        return Status::OK();
    }
    state->function = celonis_match_activities_constant_config;
    if (start_nodes_column->empty() || nodes_column->empty() || end_nodes_column->empty() ||
        excluding_nodes_column->empty() || excluding_all_nodes_column->empty() || any_nodes_column->empty()) {
        return Status::OK();
    }

    _populate_filter(start_nodes_column, 0, state->match_config.start_nodes);
    _populate_filter(nodes_column, 0, state->match_config.nodes);
    _populate_filter(end_nodes_column, 0, state->match_config.end_nodes);
    _populate_filter(excluding_nodes_column, 0, state->match_config.excluding_nodes);
    _populate_filter(excluding_all_nodes_column, 0, state->match_config.excluding_all_nodes);
    _populate_filter(any_nodes_column, 0, state->match_config.any_nodes);
    return Status::OK();
}

Status CelonisMatchActivitiesFunctions::close(FunctionContext* context,
                                              FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const MatchActivitiesStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr>
CelonisMatchActivitiesFunctions::celonis_match_activities_non_constant_config(starrocks::FunctionContext* context,
                                                                              const starrocks::Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[0] });
    size_t n_rows = columns[0]->size();
    ColumnPtr activity_array_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
    UnnestedArrayData activity_array_data = prepare_array_input(activity_array_column.get());
    DCHECK(activity_array_data.elements->is_binary());
    const auto& activities = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *activity_array_data.elements).get_data().data();
    const auto& activity_offsets = activity_array_data.offsets->get_data().data();
    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }

        SliceHashSet start_nodes;
        _populate_filter(columns[1], row, start_nodes);
        SliceHashSet nodes;
        _populate_filter(columns[2], row, nodes);
        SliceHashSet end_nodes;
        _populate_filter(columns[3], row, end_nodes);
        SliceHashSet excluding_nodes;
        _populate_filter(columns[4], row, excluding_nodes);
        SliceHashSet excluding_all_nodes;
        _populate_filter(columns[5], row, excluding_all_nodes);
        SliceHashSet any_nodes;
        _populate_filter(columns[6], row, any_nodes);
        result.append(
                _match_activities(row, activity_array_data, activities, activity_offsets, start_nodes, nodes, end_nodes,
                                  excluding_nodes, excluding_all_nodes, any_nodes));
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisMatchActivitiesFunctions::celonis_match_activities_constant_config(starrocks::FunctionContext* context,
                                                                          const starrocks::Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[0] });
    size_t n_rows = columns[0]->size();
    ColumnPtr activity_array_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
    UnnestedArrayData activity_array_data = prepare_array_input(activity_array_column.get());
    DCHECK(activity_array_data.elements->is_binary());
    const auto& activities = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *activity_array_data.elements).get_data().data();
    const auto& activity_offsets = activity_array_data.offsets->get_data().data();
    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    const auto* state = reinterpret_cast<const MatchActivitiesStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    for (size_t row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        result.append(
                _match_activities(row, activity_array_data, activities, activity_offsets,
                                  state->match_config.start_nodes, state->match_config.nodes,
                                  state->match_config.end_nodes,
                                  state->match_config.excluding_nodes, state->match_config.excluding_all_nodes,
                                  state->match_config.any_nodes));
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisMatchActivitiesFunctions::celonis_match_activities(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 7);
    const auto* state = reinterpret_cast<const MatchActivitiesStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

} // namespace starrocks
