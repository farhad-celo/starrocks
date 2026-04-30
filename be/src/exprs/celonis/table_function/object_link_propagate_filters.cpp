#include "object_link_propagate_filters.h"

#include <fmt/format.h>

#include <deque>
#include <limits>
#include <ranges>

#include "../util.h"

namespace starrocks {

[[nodiscard]] Status CelonisObjectLinkPropagateFilters::init(const TFunction& fn, TableFunctionState** state) const {
    *state = new MyState();
    return Status::OK();
}

std::pair<Columns, UInt32Column::Ptr> CelonisObjectLinkPropagateFilters::process(RuntimeState* runtime_state,
                                                                                 TableFunctionState* base_state) const {
    auto* state = down_cast<MyState*>(base_state);
    // If an error status is already set (e.g., from a previous call exceeding row limit), return early.
    if (!state->status().ok()) {
        return {};
    }

    if (state->input_rows() != 1) {
        state->set_status(
                Status::InvalidArgument("CELONIS_OBJECT_LINK_PROPAGATE_FILTERS: Operator expects only a single row"));
        return {};
    }

    // ==========================================
    // PHASE 1: COMPUTE (Run once per input row)
    // ==========================================
    if (!state->is_bfs_done) {
        auto& visited = state->visited;
        const auto outer_unnest_array_data = prepare_array_input(state->get_columns().at(0).get());
        const auto graph_unnest_array_data = prepare_array_input(outer_unnest_array_data.elements);
        const auto offsets_column = graph_unnest_array_data.offsets;
        if (graph_unnest_array_data.null_elements != nullptr) {
            state->set_status(
                    Status::InvalidArgument("CELONIS_OBJECT_LINK_PROPAGATE_FILTERS: Null values are not "
                                            "allowed in the graph array"));
            return {};
        }

        auto hop_limit = std::numeric_limits<HopLimitCppType>::max();
        if (state->get_columns().size() > 2) {
            assert(!state->get_columns().at(2)->empty());
            const auto raw_value = state->get_columns().at(2)->get(0);
            if (raw_value.is_null() || raw_value.get<HopLimitCppType>() < 0) {
                state->set_status(Status::InvalidArgument(
                        "CELONIS_OBJECT_LINK_PROPAGATE_FILTERS: The hop limit parameter cannot be null or negative"));
                return {};
            }
            hop_limit = raw_value.get<HopLimitCppType>();
        }
        std::deque<InputCppType> node_id_queue;
        // Lazy initialize the visited bitset in the TableState.
        visited.resize(offsets_column->size() - 1);
        const auto start_node_array_column = prepare_array_input(state->get_columns().at(1).get());
        if (start_node_array_column.null_elements != nullptr) {
            state->set_status(
                    Status::InvalidArgument("CELONIS_OBJECT_LINK_PROPAGATE_FILTERS: Null values are not "
                                            "allowed in the start node array"));
            return {};
        }
        for (size_t j = 0; j < start_node_array_column.elements->size(); j++) {
            const auto& raw_value = start_node_array_column.elements->get(j);
            const auto node_id = raw_value.get<InputCppType>();
            node_id_queue.push_back(node_id);
            if (node_id < 0 || node_id >= visited.size()) {
                state->set_status(Status::InvalidArgument(
                        fmt::format("CELONIS_OBJECT_LINK_PROPAGATE_FILTERS: Invalid node_id {}", node_id)));
                return {};
            }
            visited.set(node_id);
        }

        HopLimitCppType current_hop = 0;
        const auto& offsets = offsets_column->get_data();
        const auto& neighbor_ids = down_cast<const InputColumnType*>(graph_unnest_array_data.elements)->get_data();
        while (!node_id_queue.empty() && current_hop < hop_limit) {
            const auto level_size = node_id_queue.size();
            for (size_t l = 0; l < level_size; l++) {
                const auto curr_node_id = node_id_queue.front();
                node_id_queue.pop_front();
                for (auto e = offsets[curr_node_id]; e < offsets[curr_node_id + 1]; e++) {
                    const auto neighbor_id = neighbor_ids[e];
                    if (neighbor_id < 0 || neighbor_id >= visited.size()) {
                        state->set_status(Status::InvalidArgument(fmt::format(
                                "CELONIS_OBJECT_LINK_PROPAGATE_FILTERS: Invalid neighbor_id {}", neighbor_id)));
                        return {};
                    }
                    if (!visited.test_set(neighbor_id)) {
                        node_id_queue.push_back(neighbor_id);
                    }
                }
            }
            current_hop++;
        }

        state->is_bfs_done = true;
        state->current_bit_pos = state->visited.find_first();
    }

    // ==========================================
    // PHASE 2: YIELD (Output up to chunk size)
    // ==========================================
    auto res_node_id_column = OutputColumnType::create();
    auto res_offsets_column = OffsetColumnType::create();
    for (; state->current_bit_pos != boost::dynamic_bitset<>::npos &&
           res_node_id_column->size() < runtime_state->chunk_size();
         state->current_bit_pos = state->visited.find_next(state->current_bit_pos)) {
        res_node_id_column->append(state->current_bit_pos);
    }
    // The first element of the offsets column has to be 0.
    res_offsets_column->append(0);
    // Set the offset marker for this particular chunk.
    // This must be appended on every call to map the current output block to the input row,
    // even if we haven't finished yielding all elements for it yet.
    res_offsets_column->append(res_node_id_column->size());

    // ==========================================
    // PHASE 3: STATE UPDATE
    // ==========================================
    if (state->current_bit_pos == boost::dynamic_bitset<>::npos) {
        // Tell StarRocks we fully consumed this input row.
        state->set_processed_rows(1);
        // Reset our state variables so the next input chunk can be processed fresh.
        state->is_bfs_done = false;
        state->visited.reset();
    } else {
        // Tell StarRocks we consumed ZERO input rows, so it calls this function
        // again immediately with the exact same input context.
        state->set_processed_rows(0);
    }
    return {Columns{res_node_id_column}, std::move(res_offsets_column)};
}
} // namespace starrocks