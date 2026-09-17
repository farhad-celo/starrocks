#include "object_link_extract_paths.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iterator>
#include <string_view>
#include <vector>

#include "../util.h"
#include "common/status.h"

namespace starrocks {

namespace {

constexpr std::string_view PREFIX_COUNT_MARKER = "PC";

bool is_prefix_count_token(const std::string_view part) {
    return part.starts_with(PREFIX_COUNT_MARKER);
}

bool has_upper_bound(const LinkPathConfig& config) {
    return !config.exclude_range && config.max_nodes != std::numeric_limits<int>::max();
}

bool can_prune_path(const LinkPathConfig& config, const size_t nodes) {
    return has_upper_bound(config) && nodes >= static_cast<size_t>(config.max_nodes);
}

bool is_path_length_valid(const LinkPathConfig& config, const size_t nodes) {
    return (config.exclude_range && (config.min_nodes > nodes || nodes >= config.max_nodes)) ||
           (config.min_nodes <= nodes && nodes < config.max_nodes);
}

bool is_length_comparison_op(const std::string_view part) {
    return part == "EQ" || part == "NE" || part == "GE" || part == "GT" || part == "LE" || part == "LT" || part == "BW";
}

size_t param_row_index(const Column* column, const size_t row) {
    return column->is_constant() ? 0 : row;
}

bool has_any_value(const Column* column, const size_t input_rows) {
    if (column->empty()) {
        return false;
    }
    const size_t rows = column->is_constant() ? 1 : input_rows;
    for (size_t row = 0; row < rows; row++) {
        if (!column->get(row).is_null()) {
            return true;
        }
    }
    return false;
}

Status parse_int(std::string_view text, const std::string_view context, int* value) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
    }

    int parsed = 0;
    if (const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid integer value for {}: '{}'", context, text));
    }

    *value = parsed;
    return Status::OK();
}

Status parse_prefix_count_token(const std::string_view part, int* desired_prefix_count) {
    const std::string_view count_str = part.substr(PREFIX_COUNT_MARKER.size());
    if (count_str.empty()) {
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Prefix count token '{}' is missing its value", part));
    }

    int count = 0;
    RETURN_IF_ERROR(parse_int(count_str, "prefix count", &count));

    if (count <= 0) {
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Prefix count must be positive: '{}'", part));
    }

    *desired_prefix_count = count;
    return Status::OK();
}

Status parse_length_comparison_token(const std::vector<std::string_view>& parts, size_t* index,
                                     LinkPathConfig* config) {
    const std::string_view op = parts.at(*index);
    if (!is_length_comparison_op(op)) {
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Unknown length comparison operator: '{}'", op));
    }

    const bool is_between = (op == "BW");
    const size_t num_values = is_between ? 2 : 1;
    if (*index + num_values >= parts.size()) {
        if (is_between) {
            return Status::InvalidArgument(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Between operator requires two length parameters");
        }
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Operator '{}' is missing its length parameter", op));
    }

    const std::string_view lower_str = parts.at(*index + 1);
    int val1 = 0;
    RETURN_IF_ERROR(parse_int(lower_str, "length comparison", &val1));
    if (val1 < 0) {
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Length value cannot be negative: '{}'", lower_str));
    }

    const bool needs_increment = (op == "EQ" || op == "NE" || op == "GT" || op == "LE" || op == "BW");
    if (needs_increment && val1 >= std::numeric_limits<int>::max() - 1) {
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Length value too large: '{}'", lower_str));
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
    } else {
        const std::string_view upper_str = parts.at(*index + 2);
        int val2 = 0;
        RETURN_IF_ERROR(parse_int(upper_str, "upper bound length", &val2));
        if (val2 < 0) {
            return Status::InvalidArgument(
                    fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Length value cannot be negative: '{}'", upper_str));
        }
        if (val2 < val1) {
            return Status::InvalidArgument(
                    fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Between upper bound must "
                                "be >= lower bound ({} < {})",
                                val2, val1));
        }
        if (val2 >= std::numeric_limits<int>::max() - 1) {
            return Status::InvalidArgument(fmt::format(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Upper bound length value too large: '{}'", upper_str));
        }
        config->min_nodes = val1;
        config->max_nodes = val2 + 1;
    }

    size_t next_index = *index + num_values + 1;
    if (!is_between && next_index == parts.size() - 1 && parts.at(next_index) == "0") {
        next_index++;
    }
    *index = next_index;
    return Status::OK();
}

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

    bool has_length_comparison = false;
    bool has_prefix_count = false;
    size_t index = 1;
    while (index < parts.size()) {
        if (is_prefix_count_token(parts.at(index))) {
            if (has_prefix_count) {
                return Status::InvalidArgument(fmt::format(
                        "CELONIS_OBJECT_LINK_EXTRACT_PATHS: At most one prefix count token is allowed, got '{}' twice",
                        PREFIX_COUNT_MARKER));
            }
            RETURN_IF_ERROR(parse_prefix_count_token(parts.at(index), &config->desired_prefix_count));
            has_prefix_count = true;
            index++;
            continue;
        }

        if (has_length_comparison) {
            return Status::InvalidArgument(
                    fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: At most one length comparison is allowed, got a "
                                "second one: '{}'",
                                parts.at(index)));
        }
        RETURN_IF_ERROR(parse_length_comparison_token(parts, &index, config));
        has_length_comparison = true;
    }

    return Status::OK();
}

Status reject_prefix_count_with_visited_nodes(const Column* visited_node_column, const std::string_view config_str) {
    if (visited_node_column == nullptr || config_str.empty()) {
        return Status::OK();
    }
    LinkPathConfig config;
    RETURN_IF_ERROR(parse_link_path_config(config_str, &config));
    if (config.desired_prefix_count > 0) {
        return Status::InvalidArgument(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: visited_nodes is only supported in legacy/DFS mode "
                "(no 'PC<count>' token)");
    }
    return Status::OK();
}
} // namespace

[[nodiscard]] Status CelonisObjectLinkExtractPaths::init(const TFunction& fn, TableFunctionState** state) const {
    *state = new MyState();
    return Status::OK();
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::extract_chunk_inputs(TableFunctionState* state,
                                                                         ChunkInputs* inputs) const {
    Columns& columns = state->get_columns();
    const size_t input_rows = state->input_rows();

    inputs->graph_column = columns.at(0).get();
    inputs->start_node_column = columns.at(1).get();
    inputs->end_node_column = columns.at(2).get();
    inputs->constraint_column = columns.at(3).get();
    inputs->config_column = columns.at(4).get();
    inputs->visited_node_column = columns.size() == 6 ? columns.at(5).get() : nullptr;

    const auto graph_rows = prepare_array_input(inputs->graph_column);
    const auto graph_nodes = prepare_array_input(graph_rows.elements);
    if (graph_nodes.null_elements != nullptr) {
        return Status::InvalidArgument(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null values are not allowed in the graph array");
    }
    inputs->graph_row_offsets = graph_rows.offsets->get_data().data();
    inputs->graph_node_offsets = graph_nodes.offsets->get_data().data();
    inputs->neighbor_ids = down_cast<const InputColumnType*>(graph_nodes.elements)->get_data().data();

    if (!_row_per_start_node) {
        const auto start_nodes = prepare_array_input(inputs->start_node_column);
        inputs->start_node_row_offsets = start_nodes.offsets->get_data().data();
        inputs->start_nodes = down_cast<const InputColumnType*>(start_nodes.elements)->get_data().data();
        inputs->start_nodes_have_nulls = start_nodes.null_elements != nullptr;
    }

    const auto end_nodes = prepare_array_input(inputs->end_node_column);
    inputs->end_node_row_offsets = end_nodes.offsets->get_data().data();
    inputs->end_nodes = down_cast<const InputColumnType*>(end_nodes.elements)->get_data().data();
    inputs->end_nodes_have_nulls = end_nodes.null_elements != nullptr;

    if (has_any_value(inputs->constraint_column, input_rows)) {
        const auto constraint_rows = prepare_array_input(inputs->constraint_column);
        if (constraint_rows.null_elements != nullptr) {
            return Status::InvalidArgument(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null inner arrays are not allowed in constraints");
        }
        const auto constraint_nodes = prepare_array_input(constraint_rows.elements);
        if (constraint_nodes.null_elements != nullptr) {
            return Status::InvalidArgument(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null elements are not allowed in constraint edge");
        }

        inputs->has_constraints = true;
        inputs->constraint_row_offsets = constraint_rows.offsets->get_data().data();
        inputs->constraint_node_offsets = constraint_nodes.offsets->get_data().data();
        inputs->constraint_edges = down_cast<const InputColumnType*>(constraint_nodes.elements)->get_data().data();
    }

    if (inputs->visited_node_column != nullptr && has_any_value(inputs->visited_node_column, input_rows)) {
        const auto visited_rows = prepare_array_input(inputs->visited_node_column);
        if (_row_per_start_node) {
            if (visited_rows.null_elements != nullptr) {
                return Status::InvalidArgument(
                        "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null elements are not allowed in visited_nodes");
            }
            inputs->visited.node_offsets = visited_rows.offsets->get_data().data();
            inputs->visited.nodes = down_cast<const InputColumnType*>(visited_rows.elements)->get_data().data();
        } else {
            if (visited_rows.null_elements != nullptr) {
                return Status::InvalidArgument(
                        "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null inner arrays are not allowed in visited_nodes");
            }
            const auto visited_start_nodes = prepare_array_input(visited_rows.elements);
            if (visited_start_nodes.null_elements != nullptr) {
                return Status::InvalidArgument(
                        "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null elements are not allowed in visited_nodes");
            }
            inputs->visited.row_offsets = visited_rows.offsets->get_data().data();
            inputs->visited.node_offsets = visited_start_nodes.offsets->get_data().data();
            inputs->visited.nodes = down_cast<const InputColumnType*>(visited_start_nodes.elements)->get_data().data();
        }
        inputs->visited.present = true;
    }

    return Status::OK();
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::resolve_constrained_edges(const ChunkInputs& inputs,
                                                                              const size_t row, const size_t node_base,
                                                                              const size_t num_nodes,
                                                                              const size_t edge_base,
                                                                              const InputCppType** constrained_edges) {
    *constrained_edges = nullptr;
    if (!inputs.has_constraints) {
        return Status::OK();
    }

    const size_t constraint_row = param_row_index(inputs.constraint_column, row);
    if (inputs.constraint_column->get(constraint_row).is_null()) {
        return Status::OK();
    }

    const size_t constraint_node_base = inputs.constraint_row_offsets[constraint_row];
    const size_t constraint_num_nodes = inputs.constraint_row_offsets[constraint_row + 1] - constraint_node_base;
    const size_t constraint_edge_base = inputs.constraint_node_offsets[constraint_node_base];
    if (const size_t constraint_num_edges =
                inputs.constraint_node_offsets[constraint_node_base + constraint_num_nodes] - constraint_edge_base;
        constraint_num_nodes != num_nodes ||
        constraint_num_edges != inputs.graph_node_offsets[node_base + num_nodes] - edge_base) {
        return Status::InvalidArgument(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Constraints array must exactly mirror "
                "the graph adjacency matrix");
    }

    *constrained_edges = inputs.constraint_edges + constraint_edge_base;
    return Status::OK();
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::resolve_visited_nodes(const ChunkInputs& inputs, const size_t row,
                                                                          RowInputs* row_inputs) const {
    if (!inputs.visited.present) {
        return Status::OK();
    }

    const size_t visited_row = param_row_index(inputs.visited_node_column, row);
    if (inputs.visited_node_column->get(visited_row).is_null()) {
        return Status::OK();
    }

    if (_row_per_start_node) {
        row_inputs->visited_offsets = inputs.visited.node_offsets + visited_row;
        row_inputs->visited_nodes = inputs.visited.nodes;
        return Status::OK();
    }

    const size_t visited_node_base = inputs.visited.row_offsets[visited_row];
    if (const size_t visited_count = inputs.visited.row_offsets[visited_row + 1] - visited_node_base;
        visited_count != row_inputs->num_start_nodes) {
        return Status::InvalidArgument(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: visited_nodes array must exactly mirror start_nodes, "
                "one entry per start node");
    }

    row_inputs->visited_offsets = inputs.visited.node_offsets + visited_node_base;
    row_inputs->visited_nodes = inputs.visited.nodes;
    return Status::OK();
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::prepare_row(const ChunkInputs& inputs, const size_t row,
                                                                RowInputs* row_inputs) const {
    if (inputs.graph_row_offsets == nullptr) {
        return Status::InternalError("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Missing graph row offsets");
    }

    const size_t graph_row = param_row_index(inputs.graph_column, row);
    const size_t node_base = inputs.graph_row_offsets[graph_row];
    const size_t num_nodes = inputs.graph_row_offsets[graph_row + 1] - node_base;
    const size_t edge_base = inputs.graph_node_offsets[node_base];

    const InputCppType* constrained_edges = nullptr;
    RETURN_IF_ERROR(resolve_constrained_edges(inputs, row, node_base, num_nodes, edge_base, &constrained_edges));

    row_inputs->graph = GraphView{inputs.neighbor_ids, inputs.graph_node_offsets + node_base, num_nodes,
                                  constrained_edges, edge_base};

    const size_t start_node_row = param_row_index(inputs.start_node_column, row);
    if (_row_per_start_node) {
        const Datum start_node = inputs.start_node_column->get(start_node_row);
        row_inputs->scalar_start_node = true;
        row_inputs->start_nodes_null = start_node.is_null();
        if (!row_inputs->start_nodes_null) {
            row_inputs->single_start_node = start_node.get<InputCppType>();
            row_inputs->num_start_nodes = 1;
        }
    } else {
        row_inputs->start_nodes_null = inputs.start_node_column->get(start_node_row).is_null();
        if (!row_inputs->start_nodes_null) {
            if (inputs.start_nodes_have_nulls) {
                return Status::InvalidArgument(
                        "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null values are not allowed in the start node array");
            }
            const size_t start_base = inputs.start_node_row_offsets[start_node_row];
            row_inputs->start_nodes = inputs.start_nodes + start_base;
            row_inputs->num_start_nodes = inputs.start_node_row_offsets[start_node_row + 1] - start_base;
        }
    }

    if (!inputs.config_column->empty()) {
        if (const Datum config = inputs.config_column->get(param_row_index(inputs.config_column, row));
            !config.is_null()) {
            const auto slice = config.get_slice();
            row_inputs->config_str = std::string_view(slice.data, slice.size);
        }
    }

    if (row_inputs->start_nodes_null) {
        return Status::OK();
    }

    const size_t end_node_row = param_row_index(inputs.end_node_column, row);
    const size_t end_node_base = inputs.end_node_row_offsets[end_node_row];
    row_inputs->end_nodes = inputs.end_nodes + end_node_base;
    row_inputs->num_end_nodes = inputs.end_node_row_offsets[end_node_row + 1] - end_node_base;

    return resolve_visited_nodes(inputs, row, row_inputs);
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::begin_row(MyState* state, const ChunkInputs& inputs,
                                                              const RowInputs& row_inputs) const {
    const size_t num_nodes = row_inputs.graph.num_nodes;
    clear_node_bitset(state->path_visited, num_nodes, state->current_path);
    state->current_path.clear();
    clear_node_bitset(state->pre_visited, num_nodes, state->pre_visited_nodes);
    state->pre_visited_nodes.clear();
    state->current_start_node_id = 0;
    state->dfs_stack.clear();
    state->search_done = false;
    state->frontier.clear();
    state->terminal_paths.clear();
    state->materialized_paths.clear();
    state->emit_cursor = 0;
    state->complete_count = 0;

    if (inputs.end_nodes_have_nulls) {
        return Status::InvalidArgument(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Null values are not allowed in the end node array");
    }
    RETURN_IF_ERROR(resolve_end_nodes(state, row_inputs, num_nodes));
    RETURN_IF_ERROR(resolve_config(state, row_inputs.config_str));

    const LinkPathConfig& config = state->config;
    if (config.allow_cycles && !has_upper_bound(config) && config.desired_prefix_count == 0) {
        return Status::InvalidArgument(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: allow_cycles requires an upper-bounded "
                "length comparison (LT/LE/EQ/BW) or a prefix count ('PC<count>')");
    }
    if (config.desired_prefix_count > 0 && inputs.visited_node_column != nullptr) {
        return Status::InvalidArgument(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: visited_nodes is only supported in legacy/DFS mode "
                "(no 'PC<count>' token)");
    }

    state->is_row_initialized = true;
    return Status::OK();
}

void CelonisObjectLinkExtractPaths::clear_node_bitset(boost::dynamic_bitset<>& bitset, const size_t num_nodes,
                                                      const std::vector<InputCppType>& set_nodes) {
    if (bitset.size() != num_nodes) {
        bitset.resize(num_nodes);
        bitset.reset();
        return;
    }
    for (const auto node_id : set_nodes) {
        bitset.reset(node_id);
    }
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::resolve_end_nodes(MyState* state, const RowInputs& row_inputs,
                                                                      const size_t num_nodes) {
    EndNodeCache& cache = state->end_node_cache;
    if (cache.valid && cache.graph_num_nodes == num_nodes && cache.num_nodes == row_inputs.num_end_nodes &&
        (cache.nodes == row_inputs.end_nodes ||
         std::equal(row_inputs.end_nodes, row_inputs.end_nodes + row_inputs.num_end_nodes, cache.nodes))) {
        return Status::OK();
    }

    cache.valid = false;
    state->is_end_node.resize(num_nodes);
    state->is_end_node.reset();
    for (size_t i = 0; i < row_inputs.num_end_nodes; i++) {
        const auto node_id = row_inputs.end_nodes[i];
        if (node_id < 0 || node_id >= num_nodes) {
            return Status::InvalidArgument(
                    fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid end node_id {}", node_id));
        }
        state->is_end_node.set(node_id);
    }

    cache.nodes = row_inputs.end_nodes;
    cache.num_nodes = row_inputs.num_end_nodes;
    cache.graph_num_nodes = num_nodes;
    cache.valid = true;
    return Status::OK();
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::resolve_config(MyState* state, const std::string_view config_str) {
    if (state->has_cached_config && state->cached_config_str == config_str) {
        return Status::OK();
    }

    state->has_cached_config = false;
    state->config = LinkPathConfig{};
    if (!config_str.empty()) {
        RETURN_IF_ERROR(parse_link_path_config(config_str, &state->config));
    }

    state->cached_config_str.assign(config_str);
    state->has_cached_config = true;
    return Status::OK();
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::expand_one_hop(
        const std::vector<std::vector<InputCppType>>& frontier, const LinkPathConfig& config, const GraphView& graph,
        const boost::dynamic_bitset<>& is_end_node, std::vector<std::vector<InputCppType>>& next_frontier,
        std::vector<std::vector<InputCppType>>& terminal) {
    for (const auto& path : frontier) {
        const size_t nodes = path.size();
        const InputCppType node_id = path.back();

        if (can_prune_path(config, nodes)) {
            continue;
        }

        if (is_end_node.test(node_id) && is_path_length_valid(config, nodes)) {
            terminal.push_back(path);
        }

        if (has_upper_bound(config) && nodes + 1 >= static_cast<size_t>(config.max_nodes)) {
            continue;
        }

        for (size_t offset = graph.offsets[node_id]; offset < graph.offsets[node_id + 1]; offset++) {
            const auto neighbor_id = graph.neighbor_ids[offset];
            if (neighbor_id < 0 || neighbor_id >= graph.num_nodes) {
                return Status::InvalidArgument(
                        fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid neighbor_id {}", neighbor_id));
            }
            if (graph.constrained_edges != nullptr && graph.constrained_edges[offset - graph.edge_base] != 0) {
                continue;
            }
            if (!config.allow_cycles && std::find(path.begin(), path.end(), neighbor_id) != path.end()) {
                continue;
            }
            auto& extended_path = next_frontier.emplace_back(path);
            extended_path.push_back(neighbor_id);
        }
    }

    return Status::OK();
}

// Deliberately not gated by an EmitContext::chunk_limit: this loop produces no output rows, it
// only grows state->frontier/state->terminal_paths, so there is nothing to compare against a
// chunk limit until the search below has already decided it's done. It is bounded instead by
// desired_row_count and safety_cap, and stays cancellation-aware between rounds.
[[nodiscard]] Status CelonisObjectLinkExtractPaths::run_prefix_count_search(const RuntimeState* runtime_state,
                                                                            MyState* state, const RowInputs& row_inputs,
                                                                            const GraphView& graph,
                                                                            const size_t num_nodes) {
    state->frontier.reserve(row_inputs.num_start_nodes);
    for (size_t i = 0; i < row_inputs.num_start_nodes; i++) {
        const auto start_node_id = row_inputs.start_node(i);
        if (start_node_id < 0 || start_node_id >= num_nodes) {
            return Status::InvalidArgument(
                    fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid start node_id {}", start_node_id));
        }
        state->frontier.push_back({start_node_id});
    }

    const size_t safety_cap = has_upper_bound(state->config) ? std::max<size_t>(num_nodes, state->config.max_nodes)
                                                             : std::max<size_t>(num_nodes, 1);
    const auto desired_row_count = static_cast<size_t>(state->config.desired_prefix_count);
    size_t round = 0;
    while (true) {
        if (runtime_state->is_cancelled()) {
            return Status::Cancelled(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Cancelled because the runtime state is cancelled");
        }

        std::vector<std::vector<InputCppType>> next_frontier;
        RETURN_IF_ERROR(expand_one_hop(state->frontier, state->config, graph, state->is_end_node, next_frontier,
                                       state->terminal_paths));
        state->frontier = std::move(next_frontier);

        if (state->terminal_paths.size() + state->frontier.size() >= desired_row_count) {
            break;
        }
        if (state->frontier.empty()) {
            break;
        }
        if (++round > safety_cap) {
            break;
        }
    }

    state->materialized_paths = std::move(state->terminal_paths);
    state->complete_count = state->materialized_paths.size();
    state->terminal_paths.clear();
    state->materialized_paths.insert(state->materialized_paths.end(), std::make_move_iterator(state->frontier.begin()),
                                     std::make_move_iterator(state->frontier.end()));
    state->frontier.clear();
    state->frontier.shrink_to_fit();
    state->search_done = true;
    state->emit_cursor = 0;
    return Status::OK();
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::run_prefix_count_mode(RuntimeState* runtime_state, MyState* state,
                                                                          const RowInputs& row_inputs,
                                                                          const GraphView& graph, EmitContext& emit) {
    if (!state->search_done) {
        RETURN_IF_ERROR(run_prefix_count_search(runtime_state, state, row_inputs, graph, graph.num_nodes));
    }

    while (emit.chunks_added < emit.chunk_limit && state->emit_cursor < state->materialized_paths.size()) {
        const auto& path = state->materialized_paths[state->emit_cursor];
        emit.writer.append(path, state->emit_cursor < state->complete_count);
        state->emit_cursor++;
        emit.chunks_added++;
    }

    emit.row_done = (state->emit_cursor >= state->materialized_paths.size());
    return Status::OK();
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::start_next_dfs_frame(MyState* state, const RowInputs& row_inputs,
                                                                         const GraphView& graph,
                                                                         const size_t num_nodes) {
    const size_t start_node_position = state->current_start_node_id;
    const auto start_node_id = row_inputs.start_node(start_node_position);
    state->current_start_node_id++;

    if (start_node_id < 0 || start_node_id >= num_nodes) {
        return Status::InvalidArgument(
                fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid start node_id {}", start_node_id));
    }

    state->dfs_stack.emplace_back(start_node_id, graph.offsets[start_node_id], false);
    state->current_path.push_back(start_node_id);
    state->path_visited.set(start_node_id);

    clear_node_bitset(state->pre_visited, num_nodes, state->pre_visited_nodes);
    state->pre_visited_nodes.clear();
    if (row_inputs.visited_nodes == nullptr) {
        return Status::OK();
    }

    for (size_t offset = row_inputs.visited_offsets[start_node_position];
         offset < row_inputs.visited_offsets[start_node_position + 1]; offset++) {
        const auto visited_node_id = row_inputs.visited_nodes[offset];
        if (visited_node_id < 0 || visited_node_id >= num_nodes) {
            return Status::InvalidArgument(fmt::format(
                    "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid visited_nodes node_id {}", visited_node_id));
        }
        state->pre_visited.set(visited_node_id);
        state->pre_visited_nodes.push_back(visited_node_id);
    }
    return Status::OK();
}

void CelonisObjectLinkExtractPaths::evaluate_dfs_frame(MyState* state, const GraphView& graph,
                                                       const PathOutputWriter& writer, size_t& chunks_added) {
    auto& frame = state->dfs_stack.back();
    frame.evaluated = true;
    if (const size_t nodes = state->current_path.size(); can_prune_path(state->config, nodes)) {
        frame.offset = graph.offsets[frame.node_id + 1];
    } else if (state->is_end_node.test(frame.node_id) && is_path_length_valid(state->config, nodes)) {
        writer.append(state->current_path, true);
        chunks_added++;
    }
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::advance_or_pop_dfs_frame(MyState* state, const GraphView& graph,
                                                                             const size_t num_nodes) {
    auto& frame = state->dfs_stack.back();
    const InputCppType node_id = frame.node_id;
    const size_t max_offset = graph.offsets[node_id + 1];

    while (frame.offset < max_offset) {
        const auto offset = frame.offset++;
        const auto neighbor_id = graph.neighbor_ids[offset];
        if (neighbor_id < 0 || neighbor_id >= num_nodes) {
            return Status::InvalidArgument(
                    fmt::format("CELONIS_OBJECT_LINK_EXTRACT_PATHS: Invalid neighbor_id {}", neighbor_id));
        }
        if (graph.constrained_edges != nullptr && graph.constrained_edges[offset - graph.edge_base] != 0) {
            continue;
        }
        if (!state->config.allow_cycles &&
            (state->path_visited.test(neighbor_id) || state->pre_visited.test(neighbor_id))) {
            continue;
        }

        state->dfs_stack.emplace_back(neighbor_id, graph.offsets[neighbor_id], false);
        state->current_path.push_back(neighbor_id);
        state->path_visited.set(neighbor_id);
        return Status::OK();
    }

    state->path_visited.reset(node_id);
    state->current_path.pop_back();
    state->dfs_stack.pop_back();
    return Status::OK();
}

[[nodiscard]] Status CelonisObjectLinkExtractPaths::run_dfs_mode(MyState* state, const RowInputs& row_inputs,
                                                                 const GraphView& graph, const size_t num_start_nodes,
                                                                 EmitContext& emit) {
    while (emit.chunks_added < emit.chunk_limit) {
        if (state->dfs_stack.empty()) {
            if (state->current_start_node_id >= num_start_nodes) {
                break;
            }
            RETURN_IF_ERROR(start_next_dfs_frame(state, row_inputs, graph, graph.num_nodes));
        }

        while (!state->dfs_stack.empty() && emit.chunks_added < emit.chunk_limit) {
            if (state->dfs_stack.back().evaluated) {
                RETURN_IF_ERROR(advance_or_pop_dfs_frame(state, graph, graph.num_nodes));
                continue;
            }
            evaluate_dfs_frame(state, graph, emit.writer, emit.chunks_added);
        }
    }

    emit.row_done = (state->dfs_stack.empty() && state->current_start_node_id >= num_start_nodes);
    return Status::OK();
}

std::pair<Columns, UInt32Column::Ptr> CelonisObjectLinkExtractPaths::process(RuntimeState* runtime_state,
                                                                             TableFunctionState* base_state) const {
    auto* state = down_cast<MyState*>(base_state);
    if (!state->status().ok()) {
        return {};
    }
    if (state->get_columns().size() != 5 && state->get_columns().size() != 6) {
        state->set_status(Status::InvalidArgument(
                "CELONIS_OBJECT_LINK_EXTRACT_PATHS: Exactly 5 or 6 arguments must be provided"));
        return {};
    }

    ChunkInputs inputs;
    if (const Status status = extract_chunk_inputs(state, &inputs); !status.ok()) {
        state->set_status(status);
        return {};
    }

    auto res_path_elements = OutputColumnType::create();
    auto res_path_offsets = OffsetColumnType::create();
    res_path_offsets->append(0);

    Int64Column::MutablePtr res_path_length_elements;
    Int64Column::MutablePtr res_resume_node_elements;
    NullColumn::MutablePtr res_resume_node_nulls;
    if (_expanded_output) {
        res_path_length_elements = Int64Column::create();
        res_resume_node_elements = Int64Column::create();
        res_resume_node_nulls = NullColumn::create();
    }
    const PathOutputWriter writer{res_path_elements.get(), res_path_offsets.get(), res_path_length_elements.get(),
                                  res_resume_node_elements.get(), res_resume_node_nulls.get()};

    auto tf_offsets_column = OffsetColumnType::create();
    tf_offsets_column->append(0);

    size_t chunks_added = 0;
    const size_t chunk_limit = runtime_state->chunk_size();
    const size_t input_rows = state->input_rows();
    size_t row = state->processed_rows();

    while (row < input_rows && chunks_added < chunk_limit) {
        RowInputs row_inputs;
        if (const Status status = prepare_row(inputs, row, &row_inputs); !status.ok()) {
            state->set_status(status);
            return {};
        }
        if (row_inputs.start_nodes_null) {
            if (const Status status =
                        reject_prefix_count_with_visited_nodes(inputs.visited_node_column, row_inputs.config_str);
                !status.ok()) {
                state->set_status(status);
                return {};
            }
            tf_offsets_column->append(static_cast<OffsetCppType>(chunks_added));
            row++;
            state->is_row_initialized = false;
            continue;
        }

        const GraphView& graph = row_inputs.graph;
        const size_t num_start_nodes = row_inputs.num_start_nodes;

        if (!state->is_row_initialized) {
            if (const Status status = begin_row(state, inputs, row_inputs); !status.ok()) {
                state->set_status(status);
                return {};
            }
        }

        Status mode_status;
        EmitContext emit{chunk_limit, chunks_added, writer, true};
        if (state->config.desired_prefix_count > 0) {
            mode_status = run_prefix_count_mode(runtime_state, state, row_inputs, graph, emit);
        } else {
            mode_status = run_dfs_mode(state, row_inputs, graph, num_start_nodes, emit);
        }
        if (!mode_status.ok()) {
            state->set_status(mode_status);
            return {};
        }
        chunks_added = emit.chunks_added;

        tf_offsets_column->append(static_cast<OffsetCppType>(chunks_added));
        if (!emit.row_done) {
            break;
        }
        row++;
        state->is_row_initialized = false;
    }

    auto path_nulls = NullColumn::create(res_path_elements->size(), 0);
    auto res_array_column = ArrayColumn::create(
            NullableColumn::create(std::move(res_path_elements), std::move(path_nulls)), std::move(res_path_offsets));

    state->set_processed_rows(row);

    Columns output_columns{res_array_column};
    if (_expanded_output) {
        auto res_resume_node_column =
                NullableColumn::create(std::move(res_resume_node_elements), std::move(res_resume_node_nulls));
        output_columns.push_back(std::move(res_path_length_elements));
        output_columns.push_back(std::move(res_resume_node_column));
    }
    return {std::move(output_columns), std::move(tf_offsets_column)};
}
} // namespace starrocks