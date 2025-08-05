#include "process_tree_to_bpmn.h"

#include <algorithm>
#include <numeric>
#include <variant>

#include "legacy_embedded_ctl/conversion.h"
#include "legacy_embedded_ctl/utility.h"
#include "modules/operators/process/bpmn/bpmn_graph_builder.h"
#include "modules/operators/process/bpmn/vertex_types.h"
#include "modules/operators/process/inductive_miner/process_tree.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

[[nodiscard]] bool is_looping_activity(const process_tree::redo& redo) {
  return !redo.children.empty() && std::holds_alternative<process_tree::activity>(redo.children.front().node) &&
         std::ranges::all_of(
             std::next(std::cbegin(redo.children)), std::cend(redo.children),
             [](const auto& process_tree) { return std::holds_alternative<process_tree::tau>(process_tree.node); });
}

[[nodiscard]] vertex_id_type insert_process_tree(const process_tree& process_tree, vertex_id_type source_vertex,
                                                 bpmn_graph_with_block_structure_builder& bldr, object_id object,
                                                 bpmn_block_id_t parent_block_id);

[[nodiscard]] vertex_id_type insert_looping_activity(const process_tree::activity& activity,
                                                     vertex_id_type source_vertex, cel_int_t loop_count,
                                                     bpmn_graph_with_block_structure_builder& bldr, object_id object,
                                                     bpmn_block_id_t parent_block_id) {
  const auto child_id{insert_process_tree(process_tree{activity}, source_vertex, bldr, object, parent_block_id)};
  bldr.edge(child_id, child_id, object, legacy_embedded_ctl::cast<cel_int_t>(loop_count));
  return child_id;
}

[[nodiscard]] vertex_id_type insert_process_tree(const process_tree& process_tree, vertex_id_type source_vertex,
                                                 bpmn_graph_with_block_structure_builder& bldr, object_id object,
                                                 const bpmn_block_id_t parent_block_id) {
  using enum bpmn_block_type;
  return std::visit(
      legacy_embedded_ctl::overloaded{
          [&bldr, source_vertex, object, parent_block_id]([[maybe_unused]] const process_tree::tau& tau) {
            bldr.add_block(parent_block_id, object, SEQUENCE);  // add empty sequence for tau nodes
            return source_vertex;
          },
          [&bldr, source_vertex, object, parent_block_id](const process_tree::activity& activity) {
            // Initially, we always wrap an activity node into a sequence (we will later rewrite seq of seq constructs)
            const auto new_parent_block_id{bldr.add_block(parent_block_id, object, SEQUENCE)};
            bldr.add_block(new_parent_block_id, object, ACTIVITY);
            const auto task_id{bldr.add_vertex_to_current_block(task{activity.activity_id})};
            const auto count{legacy_embedded_ctl::cast<cel_int_t>(activity.object_count)};
            bldr.edge(source_vertex, task_id, object, count);
            return task_id;
          },
          [&bldr, source_vertex, object, parent_block_id](const process_tree::exclusive& exclusive) {
            const auto next_parent_block_id{bldr.add_block(parent_block_id, object, EXCLUSIVE)};
            const auto opening_id{bldr.add_vertex_to_current_block(exclusive_choice{})};
            const auto closing_id{bldr.add_vertex_to_current_block(exclusive_choice{})};
            const auto count{std::accumulate(std::begin(exclusive.child_object_counts),
                                             std::end(exclusive.child_object_counts), cel_int_t{})};
            bldr.edge(source_vertex, opening_id, object, count);
            for (std::size_t i{0}; i != exclusive.children.size(); ++i) {
              const auto last_child_id{
                  insert_process_tree(exclusive.children[i], opening_id, bldr, object, next_parent_block_id)};
              bldr.edge(last_child_id, closing_id, object, legacy_embedded_ctl::cast<cel_int_t>(exclusive.child_object_counts[i]));
            }
            return closing_id;
          },
          [&bldr, source_vertex, object, parent_block_id](const process_tree::parallel& par) {
            const auto next_parent_block_id{bldr.add_block(parent_block_id, object, PARALLEL)};
            const auto opening_id{bldr.add_vertex_to_current_block(parallel{})};
            const auto closing_id{bldr.add_vertex_to_current_block(parallel{})};
            const auto count{legacy_embedded_ctl::cast<cel_int_t>(par.object_count)};
            bldr.edge(source_vertex, opening_id, object, count);
            for (const auto& child : par.children) {
              const auto last_child_id{insert_process_tree(child, opening_id, bldr, object, next_parent_block_id)};
              bldr.edge(last_child_id, closing_id, object, count);
            }
            return closing_id;
          },
          [&bldr, source_vertex, object, parent_block_id](const process_tree::sequence& seq) {
            const auto next_parent_block_id{bldr.add_block(parent_block_id, object, SEQUENCE)};
            auto current_source{source_vertex};
            for (const auto& child : seq.children) {
              current_source = insert_process_tree(child, current_source, bldr, object, next_parent_block_id);
            }
            return current_source;
          },
          [&bldr, source_vertex, object, parent_block_id](const process_tree::redo& redo) {
            if (redo.children.empty()) {
              return source_vertex;
            }
            if (is_looping_activity(redo)) {
              const auto loop_count{
                  redo.child_redo_counts.front() -
                  redo.object_count};  // child_redo_counts[0] = redo.object_count + child_redo_counts[1...N]
              auto loop_activity{std::get<process_tree::activity>(redo.children.front().node)};
              // From a tree of shape *(A:b, tau:c):a, where a=b-c, we output a BPMN graph of the form:
              //    SOURCE --a--> task_A
              //    task_A --c--> task_A
              //    task_A --a--> TARGET
              //  This is achieved by calling insert_process_tree(task_A). But this sets the count of SOURCE --> task_A
              //    to the count of task_A (in this case, b), which caused a bug (see CPL-7295)
              //    this is why we must set the count of task_A to the count of the loop (redo.object_count)
              //    the activity node is copied to make sure that we don't mess with the original tree
              loop_activity.object_count = redo.object_count;
              return insert_looping_activity(loop_activity, source_vertex, legacy_embedded_ctl::cast<cel_int_t>(loop_count), bldr,
                                             object, parent_block_id);
            }

            const auto next_parent_block_id{bldr.add_block(parent_block_id, object, REDO)};
            const auto opening_id{bldr.add_vertex_to_current_block(exclusive_choice{})};
            const auto closing_id{bldr.add_vertex_to_current_block(exclusive_choice{})};
            const auto object_count{legacy_embedded_ctl::cast<cel_int_t>(redo.object_count)};
            bldr.edge(source_vertex, closing_id, object, object_count);
            // do part
            const auto do_id{
                insert_process_tree(redo.children.front(), closing_id, bldr, object, next_parent_block_id)};
            bldr.edge(do_id, opening_id, object, legacy_embedded_ctl::cast<cel_int_t>(redo.child_redo_counts.front()));
            // redo part
            for (std::size_t i{1}; i != redo.children.size(); ++i) {
              const auto redo_id{insert_process_tree(redo.children[i], opening_id, bldr, object, next_parent_block_id)};
              bldr.edge(redo_id, closing_id, object, legacy_embedded_ctl::cast<cel_int_t>(redo.child_redo_counts[i]));
            }
            return opening_id;
          }},
      process_tree.node);
}

void insert_process_tree(const process_tree& process_tree, vertex_id_type source_vertex, vertex_id_type target_vertex,
                         bpmn_graph_with_block_structure_builder& bldr, object_id object,
                         const bpmn_block_id_t parent_block_id) {
  const auto last_vertex_id{insert_process_tree(process_tree, source_vertex, bldr, object, parent_block_id)};
  const auto count{std::visit(legacy_embedded_ctl::overloaded{[](const process_tree::exclusive& excl) {
                                                return std::accumulate(std::begin(excl.child_object_counts),
                                                                       std::end(excl.child_object_counts),
                                                                       cel_int_t{0});
                                              },
                                              [](const auto& v) { return legacy_embedded_ctl::cast<cel_int_t>(v.object_count); }},
                              process_tree.node)};
  bldr.edge(last_vertex_id, target_vertex, object, count);
}

}  // namespace

bpmn_graph convert_to_bpmn_graph(const process_tree& process_tree, object_id object) {
  return convert_to_bpmn_graph_with_block_structure(process_tree, object).graph();
}

bpmn_graph_with_block_structure convert_to_bpmn_graph_with_block_structure(const process_tree& process_tree,
                                                                           const object_id oid) {
  bpmn_graph_with_block_structure_builder bldr{};
  const auto start_id{bldr.add_vertex(start{})};
  const auto end_id{bldr.add_vertex(end{})};
  const auto root_block_id{bldr.add_root_block(oid)};
  // Add mapping from start/end vertex to root block
  bldr.add_vertex_id_to_block_id_mapping(start_id, root_block_id);
  bldr.add_vertex_id_to_block_id_mapping(end_id, root_block_id);
  insert_process_tree(process_tree, start_id, end_id, bldr, oid, root_block_id);
  return bldr.build_with_block_structure();
}

}  // namespace celonis::accelerator::operators::process::bpmn
