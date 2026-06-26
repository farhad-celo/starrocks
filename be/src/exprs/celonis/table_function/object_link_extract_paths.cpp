#include "object_link_extract_paths.h"

#include <fmt/format.h>

#include <string>
#include <string_view>
#include <vector>

#include "../util.h"

namespace starrocks {

namespace {

Status parse_link_path_config(std::string_view config_str, LinkPathConfig* config) {
    std::vector<std::string_view> parts;
    size_t next_pos = 0;
    while ((next_pos = config_str.find(':')) != std::string_view::npos) {
        parts.push_back(config_str.substr(0, next_pos));
        config_str.remove_prefix(next_pos + 1);
    }
    parts.push_back(config_str);

    if (!parts.at(0).empty()) {
        config->allow_cycles = (parts.at(0).at(0) == 'W');
    }
    if (parts.size() == 1) {
        return Status::OK();
    }
    if (parts.size() == 2) {
        return Status::InvalidArgument(fmt::format(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Operator '{}' is missing its length parameter", parts.at(1)));
    }

    auto op = parts.at(1);
    int val1 = 0;
    try {
        size_t pos = 0;
        std::string val_str = std::string(parts.at(2));
        val1 = std::stoi(val_str, &pos);

        if (pos != val_str.length()) {
            return Status::InvalidArgument(fmt::format(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid integer value (contains non-numeric characters): '{}'",
                    parts.at(2)));
        }
    } catch (const std::exception& /*exception*/) {
        return Status::InvalidArgument(fmt::format(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid integer value for length comparison: '{}'", parts.at(2)));
    }

    if (val1 < 0) {
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Length value cannot be negative: '{}'", parts.at(2)));
    }

    const bool needs_increment = (op == "EQ" || op == "NE" || op == "GT" || op == "LE" || op == "BW");
    if (needs_increment && val1 >= std::numeric_limits<int>::max() - 1) {
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Length value too large: '{}'", parts.at(2)));
    }

    if (op == "EQ") {
        config->min_nodes = val1;
        config->max_nodes = val1 + 1;
    } else if (op == "NE") {
        config->min_nodes = val1;
        config->max_nodes = val1 + 1;
        config->exclude_range = true;
    } else if (op == "GE") {
        config->min_nodes = val1;
    } else if (op == "GT") {
        config->min_nodes = val1 + 1;
    } else if (op == "LE") {
        config->max_nodes = val1 + 1;
    } else if (op == "LT") {
        config->max_nodes = val1;
    } else if (op == "BW") {
        if (parts.size() >= 4) {
            int val2 = 0;
            try {
                size_t pos = 0;
                std::string val_str = std::string(parts.at(3));
                val2 = std::stoi(val_str, &pos);

                if (pos != val_str.length()) {
                    return Status::InvalidArgument(
                            fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid integer value (contains "
                                        "non-numeric characters) for upper bound length: '{}'",
                                        parts.at(3)));
                }
            } catch (const std::exception& /*exception*/) {
                return Status::InvalidArgument(fmt::format(
                        "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid integer value for upper bound length: '{}'",
                        parts.at(3)));
            }
            if (val2 < 0) {
                return Status::InvalidArgument(fmt::format(
                        "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Length value cannot be negative: '{}'", parts.at(3)));
            }
            if (val2 < val1) {
                return Status::InvalidArgument(
                        fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Between upper bound must be >= lower "
                                    "bound ({} < {})",
                                    val2, val1));
            }
            if (val2 >= std::numeric_limits<int>::max() - 1) {
                return Status::InvalidArgument(fmt::format(
                        "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Upper bound length value too large: '{}'", parts.at(3)));
            }
            config->min_nodes = val1;
            config->max_nodes = val2 + 1;
        } else {
            return Status::InvalidArgument(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Between operator requires two length parameters");
        }
    } else {
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Unknown length comparison operator: '{}'", op));
    }

    return Status::OK();
}
} // namespace

[[nodiscard]] Status CelonisObjectLinkExtractPaths::init(const TFunction& fn, TableFunctionState** state) const {
    *state = new MyState();
    return Status::OK();
}

std::pair<Columns, UInt32Column::Ptr> CelonisObjectLinkExtractPaths::process(RuntimeState* runtime_state,
                                                                             TableFunctionState* base_state) const {
    auto* state = down_cast<MyState*>(base_state);
    if (!state->status().ok()) {
        return {};
    }
    if (state->input_rows() != 1) {
        state->set_status(
                Status::InvalidArgument("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Operator expects only a single row"));
        return {};
    }
    if (state->get_columns().size() != 5) {
        state->set_status(
                Status::InvalidArgument("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Exactly 5 arguments must be provided"));
        return {};
    }

    // 1. EXTRACT DATA & POINTERS (Evaluated safely per function call)
    const auto outer_unnested_array_data = prepare_array_input(state->get_columns().at(0).get());
    const auto graph_unnested_array_data = prepare_array_input(outer_unnested_array_data.elements);
    if (graph_unnested_array_data.null_elements != nullptr) {
        state->set_status(
                Status::InvalidArgument("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null values are not "
                                        "allowed in the graph array"));
        return {};
    }

    const auto& neighbor_ids = down_cast<const InputColumnType*>(graph_unnested_array_data.elements)->get_data();
    const auto& offsets = graph_unnested_array_data.offsets->get_data();
    const size_t num_nodes = offsets.size() - 1;
    if (state->get_columns().at(1)->get(0).is_null()) {
        state->set_processed_rows(1);
        return {Columns{ArrayColumn::create(NullableColumn::create(OutputColumnType::create(), NullColumn::create()),
                                            OffsetColumnType::create())},
                OffsetColumnType::create()};
    }

    const auto start_node_unnested_array_data = prepare_array_input(state->get_columns().at(1).get());
    if (start_node_unnested_array_data.null_elements != nullptr) {
        state->set_status(Status::InvalidArgument(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null values are not allowed in the start node array"));
        return {};
    }
    const size_t num_start_nodes = start_node_unnested_array_data.elements->size();

    bool has_constraints = false;
    const InputCppType* constrained_edge_elements_data = nullptr;
    if (!state->get_columns().at(3)->empty() && !state->get_columns().at(3)->get(0).is_null()) {
        const auto constrained_edge_outer_unnested_array_data = prepare_array_input(state->get_columns().at(3).get());
        if (constrained_edge_outer_unnested_array_data.null_elements != nullptr) {
            state->set_status(Status::InvalidArgument(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null inner arrays are not allowed in constraints"));
            return {};
        }
        const auto constrained_edge_inner_unnested_array_data =
                prepare_array_input(constrained_edge_outer_unnested_array_data.elements);
        if (constrained_edge_inner_unnested_array_data.null_elements != nullptr) {
            state->set_status(Status::InvalidArgument(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null elements are not allowed in constraint edge"));
            return {};
        }

        const auto& constrained_edge_offsets = constrained_edge_inner_unnested_array_data.offsets->get_data();
        const auto& constrained_edge_elements =
                down_cast<const InputColumnType*>(constrained_edge_inner_unnested_array_data.elements)->get_data();
        if (constrained_edge_elements.size() != neighbor_ids.size() ||
            constrained_edge_offsets.size() != offsets.size()) {
            state->set_status(
                    Status::InvalidArgument("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Constraints array must exactly mirror "
                                            "the graph adjacency matrix"));
            return {};
        }
        has_constraints = true;
        constrained_edge_elements_data = constrained_edge_elements.data();
    }

    // 2. ONE-TIME ROW INITIALIZATION
    if (!state->is_initialized) {
        state->path_visited.resize(num_nodes);
        state->path_visited.reset();
        state->is_end_node.resize(num_nodes);
        state->is_end_node.reset();
        state->current_start_node_id = 0;
        state->dfs_stack.clear();
        state->current_path.clear();

        const auto end_node_unnested_array_data = prepare_array_input(state->get_columns().at(2).get());
        if (end_node_unnested_array_data.null_elements != nullptr) {
            state->set_status(Status::InvalidArgument(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null values are not allowed in the end node array"));
            return {};
        }

        const auto& end_ids = down_cast<const InputColumnType*>(end_node_unnested_array_data.elements)->get_data();
        for (size_t i = 0; i < end_ids.size(); i++) {
            const auto node_id = end_ids[i];
            if (node_id < 0 || node_id >= num_nodes) {
                state->set_status(Status::InvalidArgument(
                        fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid end node_id {}", node_id)));
                return {};
            }
            state->is_end_node.set(node_id);
        }

        LinkPathConfig config;
        if (!state->get_columns().at(4)->empty() && !state->get_columns().at(4)->get(0).is_null()) {
            const auto slice = state->get_columns().at(4)->get(0).get_slice();
            if (const Status parse_status = parse_link_path_config(std::string_view(slice.data, slice.size), &config);
                !parse_status.ok()) {
                state->set_status(parse_status);
                return {};
            }
        }

        if (const bool is_upper_bounded =
                    (!config.exclude_range && config.max_nodes != std::numeric_limits<int>::max());
            config.allow_cycles && !is_upper_bounded) {
            state->set_status(
                    Status::InvalidArgument("CELONIS_OBJECT_LINK_EXTRACT_PATHS: allow_cycles requires an upper-bounded "
                                            "length comparison (LT/LE/EQ/BW)"));
            return {};
        }
        state->config = config;
        state->is_initialized = true;
    }

    // 3. YIELD LOOP
    auto res_path_elements = OutputColumnType::create();
    auto res_path_offsets = OffsetColumnType::create();
    res_path_offsets->append(0);

    size_t chunks_added = 0;
    const size_t chunk_limit = runtime_state->chunk_size();

    while (chunks_added < chunk_limit) {
        if (state->dfs_stack.empty()) {
            if (state->current_start_node_id >= num_start_nodes) {
                break;
            }
            auto start_node_id =
                    start_node_unnested_array_data.elements->get(state->current_start_node_id).get<InputCppType>();
            state->current_start_node_id++;

            if (start_node_id < 0 || start_node_id >= num_nodes) {
                state->set_status(Status::InvalidArgument(
                        fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid start node_id {}", start_node_id)));
                return {};
            }
            state->dfs_stack.push_back({start_node_id, offsets[start_node_id], false});
            state->current_path.push_back(start_node_id);
            state->path_visited.set(start_node_id);
        }

        while (!state->dfs_stack.empty() && chunks_added < chunk_limit) {
            auto& frame = state->dfs_stack.back();
            const InputCppType node_id = frame.node_id;
            const size_t max_offset = offsets[node_id + 1];

            if (!frame.evaluated) {
                frame.evaluated = true;
                const size_t nodes = state->current_path.size();
                const bool can_prune =
                        (!state->config.exclude_range && state->config.max_nodes != std::numeric_limits<int>::max() &&
                         nodes >= state->config.max_nodes);

                if (can_prune) {
                    frame.offset = max_offset;
                } else if (state->is_end_node.test(node_id)) {
                    const bool length_valid = (state->config.exclude_range &&
                                               (state->config.min_nodes > nodes || nodes >= state->config.max_nodes)) ||
                                              (state->config.min_nodes <= nodes && nodes < state->config.max_nodes);
                    if (length_valid) {
                        for (const auto node : state->current_path) {
                            res_path_elements->append(node);
                        }
                        res_path_offsets->append(res_path_elements->size());
                        chunks_added++;
                    }
                }
                if (chunks_added >= chunk_limit) {
                    break;
                }
            }

            bool pushed_child = false;
            while (frame.offset < max_offset) {
                const auto offset = frame.offset++;
                auto neighbor_id = neighbor_ids[offset];
                if (neighbor_id < 0 || neighbor_id >= num_nodes) {
                    state->set_status(Status::InvalidArgument(
                            fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid neighbor_id {}", neighbor_id)));
                    return {};
                }
                if (has_constraints) {
                    if (constrained_edge_elements_data[offset] != 0) {
                        continue;
                    }
                }
                if (!state->config.allow_cycles && state->path_visited.test(neighbor_id)) {
                    continue;
                }
                state->dfs_stack.push_back({neighbor_id, offsets[neighbor_id], false});
                state->current_path.push_back(neighbor_id);
                state->path_visited.set(neighbor_id);
                pushed_child = true;
                break;
            }

            if (!pushed_child) {
                state->path_visited.reset(node_id);
                state->current_path.pop_back();
                state->dfs_stack.pop_back();
            }
        }
    }

    // 4. FINALIZE CHUNK AND UPDATE STATE
    auto path_nulls = NullColumn::create(res_path_elements->size(), 0);
    auto res_array_column = ArrayColumn::create(
            NullableColumn::create(std::move(res_path_elements), std::move(path_nulls)), std::move(res_path_offsets));

    auto tf_offsets_column = OffsetColumnType::create();
    tf_offsets_column->append(0);
    tf_offsets_column->append(chunks_added);

    if (state->dfs_stack.empty() && state->current_start_node_id >= num_start_nodes) {
        state->set_processed_rows(1);
        state->is_initialized = false;
    } else {
        state->set_processed_rows(0);
    }

    return {Columns{res_array_column}, std::move(tf_offsets_column)};
}
} // namespace starrocks