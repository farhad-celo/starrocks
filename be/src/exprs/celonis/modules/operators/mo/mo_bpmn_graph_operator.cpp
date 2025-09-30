#include "mo_bpmn_graph_operator.h"

#include <cmath>
#include <mutex>

#include <cpml/model/process_tree_to_bpmn.h>
#include <cpml/model/bpmn/merge.h>
#include <cpml/model/process_tree.h>
#include <cpml/model/pt/node_to_counts_mapping.h>
#include <ctl/algorithm.h>

#include "exprs/celonis/cpml_utils/sr_context.h"

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_format/json/json.h"
#include "log/log.h"
#ifndef CELOSTAR
#include "modules/common/direct_line_optimization.h"
#include "modules/common/iterator/index_input_iterator.h"
#include "modules/common/iterator/zip_output_iterator.h"
#include "modules/cube/ccmm/ccmm_container.h"
#include "modules/cube/ccmm/ccmm_manager.h"
#include "modules/cube/execution/tracking/tracked_function.h"
#include "modules/cube/query_scope.h"
#endif
#include "modules/memory/cache/variant_trace_cache.h"
#include "modules/memory/merge_dictionaries.h"
#include "modules/memory/raw_dictionary.h"
#ifndef CELOSTAR
#include "modules/operators/mo/incremental_eventlog.h"
#endif
#include "modules/operators/process/bpmn/bpmn_graph_to_tables.h"
#include "modules/operators/process/bpmn/density.h"
#ifndef CELOSTAR
// TODO(j.kim)
#include "modules/operators/process/dot_format_helper.h"
#endif
#include "modules/operators/process/inductive_miner/inductive_miner_statistics.h"
#ifndef CELOSTAR
#include "modules/operators/process/inductive_miner/directly_follows_graph.h"
#include "modules/operators/process/inductive_miner/inductive_miner.h"
#include "modules/operators/process/inductive_miner/inductive_miner_config.h"
#include "modules/operators/process/inductive_miner/replay_eventlog_on_process_tree.h"
#include "modules/operators/process/inductive_miner/replay_variants_on_process_tree.h"
#include "modules/operators/process/inductive_miner/splittable_eventlog.h"
#include "modules/query/queries.pb.h"
#endif

namespace celonis::accelerator::operators::mo {

namespace {

/// The below transformation code can be removed once we adopt the new PT data structures in Celostar-SR
using process_tree_with_counts_t = process::process_tree;
using process_tree_ptr_t = ctl::checked_raw_ptr<const process_tree_with_counts_t>;

void generate_node_id_mapping_recurse(
    const process::process_tree& current_node, cpml::model::node_id_t& current_node_id,
    const std::function<void(process_tree_ptr_t, cpml::model::node_id_t)>& callback);

class id_mapping {
public:
  /** Generates the ID mapping for the given process tree */
  [[nodiscard]] static id_mapping generate(const process::process_tree& tree) {
    id_mapping mapping{};
    auto& internal_boost_bimap{mapping.mapping_};

    const auto callback{[&internal_boost_bimap](const process_tree_ptr_t node_ptr, const cpml::model::node_id_t node_id) {
      using bimap_value_t = bimap_t::value_type;
      internal_boost_bimap.insert(bimap_value_t{node_ptr, node_id});
    }};

    cpml::model::node_id_t initial_node_id{0};
    generate_node_id_mapping_recurse(tree, initial_node_id, callback);

    return mapping;
  }

  [[nodiscard]] cpml::model::node_id_t node_id(const process::process_tree& node) const {
    const process_tree_ptr_t node_ptr{std::addressof(node)};
    return mapping_.left.at(node_ptr);
  }

private:
  using bimap_t = boost::bimap<process_tree_ptr_t, cpml::model::node_id_t>;
  bimap_t mapping_;
};

void generate_node_id_mapping_recurse(
    const process::process_tree& current_node, cpml::model::node_id_t& current_node_id,
    const std::function<void(process_tree_ptr_t, cpml::model::node_id_t)>& callback) {
  callback(std::addressof(current_node), current_node_id++);
  for (const auto& child_node : current_node.get_children()) {
    generate_node_id_mapping_recurse(child_node, current_node_id, callback);
  }
}

[[nodiscard]] cpml::model::process_tree do_transform_to_tree_without_counts(
    const id_mapping& node_id_mapping, cpml::model::pt::node_to_counts_mapping::builder& node_counts_bldr,
    const process::process_tree& tree_with_counts) {
  using node_without_counts_t = cpml::model::process_tree;
  using nodes_without_counts_t = std::vector<node_without_counts_t>;

  const auto node_id{node_id_mapping.node_id(tree_with_counts)};
  const auto node_count{tree_with_counts.get_object_count()};
  node_counts_bldr.add_count(node_id, node_count);

  const auto do_transform{
      std::bind_front(do_transform_to_tree_without_counts, std::cref(node_id_mapping), std::ref(node_counts_bldr))};

  return std::visit(
      ctl::overloaded{
          []([[maybe_unused]] const process_tree_with_counts_t::tau& tau_node_with_counts) {
            return node_without_counts_t{node_without_counts_t::tau{}};
          },
          [](const process_tree_with_counts_t::activity& activity_node_with_counts) {
            return node_without_counts_t{node_without_counts_t::activity{activity_node_with_counts.activity_id}};
          },
          [&do_transform](const process_tree_with_counts_t::exclusive& exclusive_node_with_counts) {
            auto child_nodes_without_count{
                ctl::transform_to<nodes_without_counts_t>(exclusive_node_with_counts.children, do_transform)};
            return node_without_counts_t{node_without_counts_t::exclusive{{std::move(child_nodes_without_count)}}};
          },
          [&do_transform](const process_tree_with_counts_t::sequence& sequence_node_with_counts) {
            auto child_nodes_without_count{
                ctl::transform_to<nodes_without_counts_t>(sequence_node_with_counts.children, do_transform)};
            return node_without_counts_t{node_without_counts_t::sequence{{std::move(child_nodes_without_count)}}};
          },
          [&do_transform](const process_tree_with_counts_t::parallel& parallel_node_with_counts) {
            auto child_nodes_without_count{
                ctl::transform_to<nodes_without_counts_t>(parallel_node_with_counts.children, do_transform)};
            return node_without_counts_t{node_without_counts_t::parallel{{std::move(child_nodes_without_count)}}};
          },
          [&do_transform](const process_tree_with_counts_t::redo& redo_node_with_counts) {
            auto child_nodes_without_count{
                ctl::transform_to<nodes_without_counts_t>(redo_node_with_counts.children, do_transform)};
            return node_without_counts_t{node_without_counts_t::redo{{std::move(child_nodes_without_count)}}};
          }},
      tree_with_counts.node);
}

[[nodiscard]] cpml::model::pt::tree_and_counts_mapping transform(const process_tree_with_counts_t& pt) {
  const auto node_id_mapping{id_mapping::generate(pt)};
  cpml::model::pt::node_to_counts_mapping::builder node_counts_bldr{};
  auto tree_without_counts_ptr{ctl::make_checked_unique<cpml::model::process_tree>(
      do_transform_to_tree_without_counts(node_id_mapping, node_counts_bldr, pt))};
  const auto ctx{starrocks::celonis::cpml_utils::make_sr_function_context()};
  auto node_without_counts_ptr_to_node_id_mapping{cpml::model::pt::id_mapping::generate(*tree_without_counts_ptr, ctx)};
  auto node_id_to_counts{std::move(node_counts_bldr).build()};
  cpml::model::pt::tree_and_counts_mapping tree_and_counts{std::move(tree_without_counts_ptr),
                                                     std::move(node_without_counts_ptr_to_node_id_mapping),
                                                     std::move(node_id_to_counts)};
  return tree_and_counts;
}

#ifndef CELOSTAR
constexpr std::chrono::milliseconds INDUCTIVE_MINER_TIME_LIMIT_MS{10_minutes};

/**
 * Given an event-log and an additional filter column, discover the corresponding BPMN graph
 * @param mo_bpmn_graph_query_expression contains the activity and object columns, as well as the happy path mode and
 * threshold
 * @param filter the filter for the activity/case columns
 * @param context the execution context
 * @param statistics object to register the inductive miner statistics to
 * @return the BPMN model that was discovered
 */
process::bpmn::bpmn_graph_with_block_structure filtered_eventlog_to_bpmn(
    const mo_bpmn_graph_computation_input& computation_input, cube::filter_bitset_t filter,
    const common::execution_context& parent_context, size_t grain_size, process::inductive_miner_statistics& statistics,
    cube::query_scope& scope) {
  const auto context{parent_context.create_sub_context("filtered_eventlog_to_bpmn", {})};
  const auto& activity_column{computation_input.activity_column()};
  const auto& case_column{computation_input.get_eventlog_input().case_column};

  auto config{process::make_inductive_miner_config_for_eventlog_based_approach(activity_column, case_column,
                                                                               filter.flip(), context, grain_size)};

  auto dfg{process::dfg::initialize_dfg(config.eventlog(), context, grain_size)};
  const auto dict{activity_column->get_string_dict(context)->get_const_data(context)};

  cube::execution::tracking::stop_token stop_token{mo_bpmn_graph_operator::get_user_visible_operator_name(),
                                                   INDUCTIVE_MINER_TIME_LIMIT_MS};
  cube::execution::tracking::tracked_function registered_function(stop_token, scope.get_active_operators_registry());

  auto [tree, is_valid]{process::invoke_verbose_inductive_miner(
      config, dfg, statistics, stop_token, mo_bpmn_graph_operator::get_user_visible_operator_name(), dict.get())};
  if (!is_valid) {
    if (auto replayed_result{replay_eventlog_on_process_tree(tree, activity_column, case_column, filter, context)};
        is_valid_tree(replayed_result)) {
      tree = replayed_result;
    } else {  // replay failed, so return the original (inconsistent) inductive miner result
      legacy_embedded_warning_assert(false, fmt::format("Replay generated an inconsistent process tree {}", pt2dot(tree, nullptr)));
    }
  }
  return process::bpmn::convert_to_bpmn_graph_with_block_structure(tree);
}

/**
 * Given the pre-computed variants, discover the corresponding BPMN graph (analogous to the above eventlog approach)
 *
 * Here we skip almost all intermediate steps of 'eventlog_to_bpmn' and directly call the BPMN graph computation.
 * As we assume (for now) that the variant-based path is only invoked for the case of no filter / use all variants we
 * skip the computation of the 'incremental_eventlog' and do not call the 'compute_bpmn_cached' proxy.
 *
 * @param mo_bpmn_graph_query_expression contains the activity column and pre-computed variants
 * @param statistics object to register the inductive miner statistics to
 * @return the BPMN model that was discovered
 * TODO(n.weber): In the future we can relax the requirements when to allow the variant path (i.e., support filtering)
 */
[[nodiscard]] process::bpmn::bpmn_graph_with_block_structure non_filtered_variants_to_bpmn(
    const mo_bpmn_graph_computation_input& computation_input, process::inductive_miner_statistics& statistics,
    cube::query_scope& scope, const common::execution_context& parent_context) {
  legacy_embedded_debug_assert(
      details::does_strategy_and_threshold_allow_for_variant_based_approach(computation_input.selection_strategy()),
      "At this moment, the variant path is not supported for [{}]", to_string(computation_input.selection_strategy()));
  legacy_embedded_debug_assert(computation_input.has_variants_input());
  const auto context{parent_context.create_sub_context("non_filtered_variants_to_bpmn", {})};

  const auto& variant_entries{computation_input.get_variants_input().variant_entries};
  auto variant_based_inductive_miner_config{
      process::make_inductive_miner_config_for_variant_based_approach(variant_entries.underlying(), context)};
  // Just for some initial monitoring that in case we see issues we know it might be related to this change
  log::info("{}: uses variant based IM with count correction.",
            mo_bpmn_graph_operator::get_user_visible_operator_name());

  auto dfg{process::dfg::initialize_dfg(variant_based_inductive_miner_config.eventlog(), context,
                                        process::VARIANT_UTILIZATION_GRAIN_SIZE)};
  const auto dict{computation_input.activity_column()->get_string_dict(context)->get_const_data(context)};

  cube::execution::tracking::stop_token stop_token{mo_bpmn_graph_operator::get_user_visible_operator_name(),
                                                   INDUCTIVE_MINER_TIME_LIMIT_MS};
  cube::execution::tracking::tracked_function registered_function(stop_token, scope.get_active_operators_registry());
  auto [tree, is_valid]{
      process::inductive_miner(variant_based_inductive_miner_config, dfg, statistics, stop_token, dict.get())};

  common::runtime_assert(is_valid, "Invalid process tree produced in variant based IM. Tree: {}",
                         pt2dot(tree, dict.get()));

  replay_variants_on_process_tree(*variant_entries, tree, context);

  return process::bpmn::convert_to_bpmn_graph_with_block_structure(tree);
}

/**
 * Computes a measure of complexity of a BPMN graph.
 *
 * Complexity is measured here as the number of parallel and exclusive gateways divided by the total number of nodes.
 * The complexity of an empty BPMN graph is 0.0.
 * @param graph the BPMN graph to compute the complexity of
 * @return a floating-point number between 0.0 and 1.0, measuring the complexity of the BPMN model
 */
double cost_of(const process::bpmn::bpmn_graph& graph) {
  const auto num_nodes{graph.get_vertices().size()};
  if (num_nodes == 0) {
    return 0.0;
  }
  const auto num_gateways{
      std::accumulate(begin(graph.get_vertices()), end(graph.get_vertices()), 0., [](double acc, const auto& pair) {
        return acc + std::visit(legacy_embedded_ctl::overloaded{[](const process::bpmn::parallel& /*unused*/) { return 1; },
                                                [](const process::bpmn::exclusive_choice& /*unused*/) { return 1; },
                                                [](const auto& /*fallback*/) { return 0; }},
                                pair.second.get_vertex_type());
      })};
  return num_gateways / static_cast<double>(num_nodes);
}

// TODO(n.weber): can be removed as part of clean-up. Only kept for utility value computation in eventlog_to_bpmn
struct graph_with_retained_object_count : private std::pair<process::bpmn::bpmn_graph_with_block_structure, row_id> {
  // NOLINTNEXTLINE(modernize-use-equals-default)
  using std::pair<process::bpmn::bpmn_graph_with_block_structure, row_id>::pair;
  [[nodiscard]] constexpr const process::bpmn::bpmn_graph_with_block_structure& graph() const { return this->first; }
  [[nodiscard]] constexpr process::bpmn::bpmn_graph_with_block_structure&& extract_graph() && {
    return std::move(this->first);
  }
  [[nodiscard]] constexpr row_id count() const { return this->second; }
};

/**
 * Given an incremental_eventlog and a pair of activity and case columns, expose a method to compute the BPMN graph of
 * the N most frequent variants (with the help of the incremental_eventlog), and save the result to a cache.
 *
 * NB We originally had a lambda for this, but clang-tidy raised a false-positive warning, and this is arguably nicer
 * than silencing the warning
 */
struct compute_bpmn_cached {
  const incremental_eventlog& increments;
  const mo_bpmn_graph_computation_input& computation_input;
  size_t grain_size{};
  process::inductive_miner_statistics& statistics;
  const common::execution_context& context;
  cube::query_scope& scope;
  std::map<row_id, graph_with_retained_object_count> computed_graphs{};
  /**
   * Retrieve the BPMN model from the cache, or compute it if it is not there yet.
   * @param num_retained the number of variants to retain for process discovery
   * @return a BPMN graph, the result of the inductive miner on the num_retained most frequent variants
   */
  auto operator()(row_id num_retained) {
    if (const auto it{computed_graphs.find(num_retained)}; it != end(computed_graphs)) {
      return it->second;
    }
    auto [filter, count]{increments.retain_most_frequent(num_retained, context)};
    auto discovered_bpmn_graph{
        filtered_eventlog_to_bpmn(computation_input, filter, context, grain_size, statistics, scope)};
    return computed_graphs.try_emplace(num_retained, std::move(discovered_bpmn_graph), count).first->second;
  }
};

std::pair<incremental_eventlog, size_t> get_incremental_eventlog(
    const mo_bpmn_graph_data_selection_strategy::happy_path_strategy& mode, const memory::column_t& activity_column,
    const memory::column_t& case_column, const common::execution_context& parent_context) {
  auto context{parent_context.create_sub_context("get_incremental_eventlog", {})};
  switch (mode) {
    default:
      context.add_warning("{}: Invalid happy path mode. Defaulting to frequency mode.",
                          mo_bpmn_graph_operator::get_user_visible_operator_name());
      [[fallthrough]];
      // The set-cover strategy is temporarily mapped to the variant filter strategy for testing
      // TODO(j.tai): CPL-8167 - enable again after VARIANT_FILTER available in frontend
      //    case mo_bpmn_graph_operator::happy_path_strategy::set_cover: {
      //      std::vector<row_id> activities_to_cover(activity_column->get_domain_count(context));
      //      std::iota(begin(activities_to_cover), end(activities_to_cover), row_id{0});
      //      return incremental_eventlog::activity_cover(
      //          activities_to_cover, activity_column, case_column,
      //          cube::filter_bitset_t{legacy_embedded_ctl::cast<size_t>(activity_column->get_row_count(context)), false}, context);
      //    }
    case mo_bpmn_graph_data_selection_strategy::happy_path_strategy::set_cover:
    case mo_bpmn_graph_data_selection_strategy::happy_path_strategy::frequency:
    case mo_bpmn_graph_data_selection_strategy::happy_path_strategy::object_coverage:
    case mo_bpmn_graph_data_selection_strategy::happy_path_strategy::variant_filter:
      return std::pair{
          incremental_eventlog::frequency_decreasing(
              activity_column, case_column,
              cube::filter_bitset_t{legacy_embedded_ctl::cast<size_t>(activity_column->get_row_count(context)), false}, context),
          size_t{1}};
  }
}

/**
 * Given an event-log (a pair of activity and case column), compute a bpmn graph from it.
 *
 * The last argument gives an upper limit on the "complexity" of the resulting BPMN graph. It is the number of exclusive
 * and parallel gateways divided by the total number of nodes in the BPMN graph, so it is always between 0.0 and 1.0.
 *
 * The basic idea is the following: We build a new event-log by greedily adding the most frequent variants, until the
 * discovered process model exceeds the complexity limit
 * @param graph_computation_input contains the event-log or variant input together with the respective strategy
 * @param context the execution context
 * @param statistics where the statistics from the inductive miner calls are registered
 * @return a BPMN graph of a subset of the event-log such that the complexity does not exceed the specified upper limit
 */
[[nodiscard]] process::bpmn::bpmn_graph_with_block_structure eventlog_to_bpmn(
    const mo_bpmn_graph_computation_input& computation_input, const common::execution_context& parent_context,
    size_t grain_size, process::inductive_miner_statistics& statistics, cube::query_scope& scope) {
  const auto context{parent_context.create_sub_context("eventlog_to_bpmn", {})};
  const auto& activity_column{computation_input.activity_column()};
  const auto& case_column{computation_input.get_eventlog_input().case_column};
  const auto strategy{computation_input.selection_strategy().strategy()};
  const auto [eventlog_increments,
              min_increments]{get_incremental_eventlog(strategy, activity_column, case_column, context)};
  // We want to do a binary search on the incremental_event-log. Since the computation of the BPMN graph is expensive,
  // we store all intermediately computed BPMN graphs here (also for future slider functionality):
  compute_bpmn_cached compute_bpmn{eventlog_increments, computation_input, grain_size, statistics, context, scope};
  if (eventlog_increments.variant_count() == 0) {
    return compute_bpmn(legacy_embedded_ctl::cast<row_id>(min_increments)).extract_graph();
  }

  const auto complexity_threshold{computation_input.selection_strategy().complexity_threshold()};
  row_id num_variants{};
  switch (strategy) {
    using enum mo_bpmn_graph_data_selection_strategy::happy_path_strategy;
    case object_coverage:
      num_variants = eventlog_increments.retain_percentage_of_objects(complexity_threshold);
      break;
    case variant_filter:
      num_variants = complexity_threshold > 1.0 ? static_cast<row_id>(std::floor(complexity_threshold)) : 1;
      break;
    case set_cover:
      if (complexity_threshold <= 0.0) {
        num_variants = 1;
      } else if (complexity_threshold >= 1.0) {
        // Map 1 (last point in frontend slider) to all variants
        num_variants = eventlog_increments.variant_count();
      } else {
        // 0 -> 1 variant, 0.05 -> 2 variants, ..., 1 -> 20 variants
        // Complexity threshold divided by 0.05 rounded to next integer
        num_variants = static_cast<row_id>(std::round(complexity_threshold / 0.05) + 1);
      }
      break;
    default:
      // NB: In general, the incremental_eventlog won't be sorted by complexity of the process model discovered from it.
      // However, we assume that the utility function as a function of the number of retained variants behaves
      // reasonably. With this assumption, we can look for the argmax of the utility function.
      std::vector<std::pair<row_id, double>> num_variants_utility_values_pairs{};

      num_variants = common::extended_line_search(
                         common::iterator::index_input_iterator{legacy_embedded_ctl::cast<row_id>(min_increments)},
                         common::iterator::index_input_iterator<row_id>{eventlog_increments.variant_count() + 1},
                         [&, max_count = static_cast<double>(eventlog_increments.object_count())](auto num_retained) {
                           const auto result{compute_bpmn(num_retained)};
                           const auto count_as_double{static_cast<double>(result.count())};
                           const double utility_function_value{complexity_threshold * count_as_double / max_count -
                                                               (1.0 - complexity_threshold) * cost_of(result.graph())};
                           num_variants_utility_values_pairs.emplace_back(num_retained, utility_function_value);
                           return utility_function_value;
                         })
                         .i;
      break;
  }
  return compute_bpmn(num_variants).extract_graph();
}

/** Simple proxy which triggers either the eventlog-based path or the variant-based path */
// #lizard forgives
[[nodiscard]] process::bpmn::bpmn_graph_with_block_structure eventlog_or_variants_to_bpmn(
    const mo_bpmn_graph_computation_input& computation_input, process::inductive_miner_statistics& statistics,
    cube::query_scope& scope, const common::execution_context& parent_context) {
  const auto& activity_column{computation_input.activity_column()};
  const auto subcontext{
      parent_context.create_sub_context("compute_and_reduce_single_object_bpmn",
                                        {{"object", activity_column->get_user_visible_owner_name(parent_context)}})};

  // eventlog-based path
  if (computation_input.has_eventlog_input()) {
    const size_t grain_size{
        scope.get_execution_configuration().get_chunk_sizes().get_aggregation_grain_size_2_power_17()};
    try {
      return eventlog_to_bpmn(computation_input, subcontext, grain_size, statistics, scope);
    } catch (const common::internal_exception& e) {
      log::jwarn("Failed to create BPMN graph from event-log.", {{"exception", e.internal_message()}});
      const auto& case_column{computation_input.get_eventlog_input().case_column};
      throw common::cpm_exception{
          R"===({}: Failed to create BPMN graph from event-log. Do the activity column ("{}") and case column
              ("{}") match?")===",
          mo_bpmn_graph_operator::get_user_visible_operator_name(), activity_column->get_name(),
          case_column->get_name()};
    }
  }

  // variant-based path
  if (computation_input.has_variants_input()) {
    try {
      return non_filtered_variants_to_bpmn(computation_input, statistics, scope, subcontext);
    } catch (const common::internal_exception& e) {
      log::jerror("Failed to create BPMN graph from variants.", {{"exception", e.internal_message()}});
      throw;
    }
  }

  legacy_embedded_ctl::assert_unreachable();
}
#endif

}  // namespace

#ifdef CELOSTAR
mo_bpmn_graph_operator::mo_bpmn_graph_operator(std::vector<process::process_tree> process_trees,
                                               std::vector<process::inductive_miner_statistics> statistics,
                                               std::vector<memory::column_t> activity_columns,
                                               const common::execution_context& parent_context)
    : process_trees_(std::move(process_trees)),
      statistics_(std::move(statistics)),
      activity_columns_(std::move(activity_columns)),
      operator_context_{parent_context.create_sub_context(
          get_user_visible_operator_name(), {{"computation_inputs", process_trees_.size()}})} {}
#else
mo_bpmn_graph_operator::mo_bpmn_graph_operator(const mo_bpmn_graph_query_expressions_t& mo_bpmn_graph_query_expressions,
                                               cube::query_scope& scope,
                                               const common::execution_context& parent_context)
    : mo_bpmn_graph_computation_inputs_{mo_bpmn_graph_computation_input::from_query_inputs(
          mo_bpmn_graph_query_expressions, parent_context)},
      scope_{scope},
      operator_context_{parent_context.create_sub_context(
          get_user_visible_operator_name(), {{"computation_inputs", mo_bpmn_graph_computation_inputs_.size()}})} {}
#endif

mo_bpmn_graph_operator::result_type mo_bpmn_graph_operator::compute() const {
  const auto [graph, dictionary, statistics] = compute_graph();
#ifdef CELOSTAR
  // TODO(j.kim): Review 32bit row_id and its limit.
  return {process::bpmn::create_bpmn_tables_from_bpmn_graph(graph, dictionary, memory::MAX_TABLE_ROW_LIMIT, operator_context_),
          statistics};
#else
  return {create_bpmn_tables_from_bpmn_graph(graph, dictionary, scope_.get_table_row_limit(), operator_context_),
          statistics};
#endif
}

mo_bpmn_graph_operator::compute_graph_result mo_bpmn_graph_operator::compute_graph() const {
#ifdef CELOSTAR
  const auto transform_func{[oid = 0](const process::process_tree& process_tree) mutable {
    const auto pt_and_counts{transform(process_tree)};
    return cpml::model::convert_to_bpmn_graph_with_block_structure(pt_and_counts, oid++);
  }};
  const auto graphs{ctl::transform_to<std::vector<cpml::model::bpmn_graph_with_block_structure>>(process_trees_, transform_func)};

  std::vector<process::inductive_miner_statistics> statistics{statistics_};
  std::vector<std::pair<memory::dictionary_t, std::string>> dictionaries(activity_columns_.size());
  std::transform(begin(activity_columns_), end(activity_columns_), begin(dictionaries),
                 [this](const auto& activity_column) {
                     return std::pair{activity_column->get_dict(operator_context_), activity_column->get_name()};
                 });
#else
  // transform the activity columns to BPMN graphs (one each)
  const auto [graphs, statistics]{compute_single_object_graphs()};

  // get dictionaries
  std::vector<std::pair<memory::dictionary_t, std::string>> dictionaries(mo_bpmn_graph_computation_inputs_.size());
  std::transform(begin(mo_bpmn_graph_computation_inputs_), end(mo_bpmn_graph_computation_inputs_), begin(dictionaries),
                 [this](const auto& computation_input) {
                   const auto& activity_column{computation_input.activity_column()};
                   return std::pair{activity_column->get_dict(operator_context_), activity_column->get_name()};
                 });
#endif
  // merge bpmns and create output tables
  auto [graph, dictionary]{details::merge_bpmn_graphs(graphs, dictionaries, operator_context_)};
  // update the graph with the event counts from the CCMM manager.
  const auto& string_dict{dynamic_cast<memory::typed_dictionary<cel_string_t>&>(*dictionary)};

  // TODO(bluppes): CPL-7544 remove eventually
  const auto dictionary_data{string_dict.get_const_data(operator_context_)};
#ifndef CELOSTAR
  legacy_embedded_format::json::json_object_t log_details{{"Merged graph", process::bpmn2dot(graph, dictionary_data.get())}};
  log::jinfo("Merged graph information", log_details);
#endif

  return {graph, dictionary, statistics};
}

#ifndef CELOSTAR
// log_message is an output variable
mo_bpmn_graph_operator::compute_single_object_graphs_return_type mo_bpmn_graph_operator::compute_single_object_graphs()
    const {
  const auto num_objects{mo_bpmn_graph_computation_inputs_.size()};

  std::vector<process::bpmn::bpmn_graph_with_block_structure> graphs(num_objects);
  std::vector<process::inductive_miner_statistics> statistics(num_objects);

  tbb::parallel_for(tbb::blocked_range<size_t>{0, num_objects}, [&](const auto r) {
    std::transform(
        std::next(begin(mo_bpmn_graph_computation_inputs_), r.begin()),
        std::next(begin(mo_bpmn_graph_computation_inputs_), r.end()), std::next(begin(statistics), r.begin()),
        std::next(begin(graphs), r.begin()),
        [this](const mo_bpmn_graph_computation_input& computation_input, auto& stats) {
          auto discovered_graph{eventlog_or_variants_to_bpmn(computation_input, stats, scope_, operator_context_)};
          log::jinfo(fmt::format("{} graph statistics", get_user_visible_operator_name()),
                     {{"num_vertices", discovered_graph.get_vertices().size()},
                      {"num_edges", discovered_graph.get_edges().size()},
                      {"density", calculate_density(discovered_graph)}});

          return discovered_graph;
        });
  });

  return {graphs, statistics};
}
#endif

namespace details {

details::bpmn_graph_with_dict merge_bpmn_graphs(
    std::vector<cpml::model::bpmn_graph_with_block_structure> graphs,
    const std::vector<std::pair<memory::dictionary_t, std::string>>& dictionaries,
    const common::execution_context& parent_context) {
  legacy_embedded_debug_assert(graphs.size() == dictionaries.size());
  legacy_embedded_debug_assert(!graphs.empty());

  const auto context{parent_context.create_sub_context("merge_bpmn_models", {})};

  if (graphs.size() == 1) {
    return std::make_pair(std::move(graphs[0]), dictionaries[0].first);
  }

  auto merge_result{memory::merge_n_dictionaries_raw(
      dictionaries, mo_bpmn_graph_operator::get_user_visible_operator_name(), context)};
  auto dict{std::visit(legacy_embedded_ctl::overloaded{[](memory::raw_dictionary_t&& raw_dict) {
                                         return raw_dict->convert_to_dictionary_t_release_data(
                                             "", memory::management::no_swap(), "");
                                       },
                                       [](memory::dictionary_t&& dict) { return std::move(dict); }},
                       std::move(merge_result.dictionary_variant))};

  std::vector<cpml::model::bpmn::graph_and_activity_remapping> merge_input{};
  merge_input.reserve(graphs.size());
  for (size_t idx{0}; idx < graphs.size(); ++idx) {
    const auto& mapping{merge_result.mappings.at(idx)};
    const auto remapping_func{[&mapping](const cpml::activity_id_t activity_id) { return mapping.at(activity_id); }};
    merge_input.emplace_back(std::move(graphs.at(idx)), remapping_func);
  }

  const auto function_ctx{starrocks::celonis::cpml_utils::make_sr_function_context()};

  auto overlaid_graph{cpml::model::bpmn::merge(merge_input, function_ctx)};

  return std::make_pair(std::move(overlaid_graph), std::move(dict));
}

}  // namespace details

}  // namespace celonis::accelerator::operators::mo
