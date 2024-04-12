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
    bool has_exclude_node = false;
    // contain node in any_nodes
    bool has_any_node = false;
    bool has_non_null = false;
    size_t start = offsets[row];
    size_t end = offsets[row + 1];
    std::optional<size_t> start_index = std::nullopt;
    std::optional<size_t> end_index = std::nullopt;
    for (size_t index = start; index < end; ++index) {
        // Nulls are ignored
        if (null_elements != nullptr && (*null_elements)[index] != 0) {
            continue;
        }
        if (!start_index.has_value()) {
            start_index = index;
        }
        end_index = index;
        const auto& value = activities[index];
        has_non_null = true;
        if (nodes.count(value)) {
            nodes_seen.insert(value);
        }
        if (excluding_all_nodes.count(value)) {
            excluding_nodes_seen.insert(value);
        }
        if (any_nodes.count(value)) {
            has_any_node = true;
        }
        if (excluding_nodes.count(value)) {
            has_exclude_node = true;
            break;
        }
    }
    if (!start_nodes.empty() &&
        (!start_index.has_value() || !start_nodes.count(activities[start_index.value()]))) {
        return 0L;
    }
    if (!end_nodes.empty() &&
        (!end_index.has_value() || !end_nodes.count(activities[end_index.value()]))) {
        return 0L;
    }
    if (!any_nodes.empty() && !has_any_node) {
        return 0L;
    }
    if (has_exclude_node) {
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
        return Status::OK();
    }
    state->function = celonis_match_activities_constant_config;
    if (start_nodes_column->empty() || nodes_column->empty() || end_nodes_column->empty() ||
        excluding_nodes_column->empty() || excluding_all_nodes_column->empty() || any_nodes_column->empty()) {
        return Status::OK();
    }

    auto start_node_array = start_nodes_column->get(0).get_array();
    for (const auto& value: start_node_array) {
        state->match_config.start_nodes.insert(value.get_slice());
    }
    auto node_array = nodes_column->get(0).get_array();
    for (const auto& value: node_array) {
        state->match_config.nodes.insert(value.get_slice());
    }
    auto end_node_array = end_nodes_column->get(0).get_array();
    for (const auto& value: end_node_array) {
        state->match_config.end_nodes.insert(value.get_slice());
    }
    auto excluding_node_array = excluding_nodes_column->get(0).get_array();
    for (const auto& value: excluding_node_array) {
        state->match_config.excluding_nodes.insert(value.get_slice());
    }
    auto excluding_all_node_array = excluding_all_nodes_column->get(0).get_array();
    for (const auto& value: excluding_all_node_array) {
        state->match_config.excluding_all_nodes.insert(value.get_slice());
    }
    auto any_node_array = any_nodes_column->get(0).get_array();
    for (const auto& value: any_node_array) {
        state->match_config.any_nodes.insert(value.get_slice());
    }
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
    UnnestedArrayData activity_array_data = prepare_array_input(columns[0].get());
    DCHECK(activity_array_data.elements->is_binary());
    const auto& activities = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *activity_array_data.elements).get_data().data();
    const auto& activity_offsets = activity_array_data.offsets->get_data().data();
    size_t n_rows = columns[0]->size();
    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }

        auto start_node_array = columns[1]->get(row).get_array();
        SliceHashSet start_nodes;
        for (const auto& value: start_node_array) {
            start_nodes.insert(value.get_slice());
        }

        auto node_array = columns[2]->get(row).get_array();
        SliceHashSet nodes;
        for (const auto& value: node_array) {
            nodes.insert(value.get_slice());
        }

        auto end_node_array = columns[3]->get(row).get_array();
        SliceHashSet end_nodes;
        for (const auto& value: end_node_array) {
            end_nodes.insert(value.get_slice());
        }

        auto excluding_node_array = columns[4]->get(row).get_array();
        SliceHashSet excluding_nodes;
        for (const auto& value: excluding_node_array) {
            excluding_nodes.insert(value.get_slice());
        }
        auto excluding_all_node_array = columns[5]->get(row).get_array();
        SliceHashSet excluding_all_nodes;
        for (const auto& value: excluding_all_node_array) {
            excluding_all_nodes.insert(value.get_slice());
        }
        auto any_node_array = columns[6]->get(row).get_array();
        SliceHashSet any_nodes;
        for (const auto& value: any_node_array) {
            any_nodes.insert(value.get_slice());
        }
        result.append(
                _match_activities(row, activity_array_data, activities, activity_offsets, start_nodes, nodes, end_nodes,
                                  excluding_nodes, excluding_all_nodes, any_nodes));
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr>
CelonisMatchActivitiesFunctions::celonis_match_activities_constant_config(starrocks::FunctionContext* context,
                                                                          const starrocks::Columns& columns) {
    UnnestedArrayData activity_array_data = prepare_array_input(columns[0].get());
    DCHECK(activity_array_data.elements->is_binary());
    const auto& activities = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *activity_array_data.elements).get_data().data();
    const auto& activity_offsets = activity_array_data.offsets->get_data().data();
    size_t n_rows = columns[0]->size();
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
