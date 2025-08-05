#include "bpmn_graph_with_block_structure.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

#include <fmt/format.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/conversion.h"
#include "log/log.h"
#include "modules/common/exceptions.h"
#include "modules/operators/process/bpmn/bpmn_graph_with_block_structure_utils.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

[[nodiscard]] bpmn_graph_with_block_structure::vertex_id_to_block_id_mapping_t unflatten_mapping(
    const block_id_to_contained_vertices_mapping_t& block_to_node_mapping) {
  bpmn_graph_with_block_structure::vertex_id_to_block_id_mapping_t mapping_as_map{};
  for (bpmn_block_id_t block_id{0}; std::cmp_less(block_id, block_to_node_mapping.size()); ++block_id) {
    std::ranges::for_each(block_to_node_mapping.at(block_id),
                          [block_id, &mapping_as_map](const vertex_id_type vertex_id) {
                            // For one bpmn graph from a single process tree there will never be two vertex IDs (with
                            // different blocks). However, later on when we will overlay blocks it can happen that two
                            // (or more) vertices with the same ID have different blocks assigned (due to shared
                            // activities with corresponding blocks of different object types)
                            mapping_as_map[vertex_id].push_back(block_id);
                          });
  }
  return mapping_as_map;
}

using distinct_bpmn_block_object_ids_t = std::set<bpmn_block_object_id_t>;

template <typename T>
[[nodiscard]] auto make_set_inserter(std::set<T>& s) {
  return std::inserter(s, s.end());
}

[[nodiscard]] distinct_bpmn_block_object_ids_t extract_all_object_ids_from_graph(const bpmn_graph& graph) {
  distinct_bpmn_block_object_ids_t object_ids{};
  std::ranges::transform(graph.get_edges(), make_set_inserter(object_ids), &edge::get_object_id);
  return object_ids;
}

[[nodiscard]] distinct_bpmn_block_object_ids_t extract_all_object_ids_from_blocks(const bpmn_blocks_t& blocks) {
  distinct_bpmn_block_object_ids_t object_ids{};
  std::ranges::transform(blocks, make_set_inserter(object_ids), &bpmn_block::object_id);
  return object_ids;
}

// Returns the sorted set difference of two sorted ranges
template <std::ranges::range R1, std::ranges::range R2>
requires std::same_as<typename R1::value_type, typename R2::value_type>
[[nodiscard]] std::set<typename R1::value_type> make_set_difference(const R1& r1, const R2& r2) {
  using value_type = typename R1::value_type;
  std::set<value_type> set_difference{};
  legacy_embedded_debug_assert(std::ranges::is_sorted(r1));
  legacy_embedded_debug_assert(std::ranges::is_sorted(r2));
  std::ranges::set_difference(r1, r2, make_set_inserter(set_difference));
  return set_difference;
}

distinct_bpmn_block_object_ids_t verify_object_ids_consistency(
    const bpmn_graph_with_block_structure& graph_with_block_structure) {
  auto oids_in_graph{extract_all_object_ids_from_graph(graph_with_block_structure.graph())};
  const auto oids_in_blocks{extract_all_object_ids_from_blocks(graph_with_block_structure.blocks())};
  if (oids_in_graph != oids_in_blocks) {
    const auto oids_in_graph_but_not_in_blocks{make_set_difference(oids_in_graph, oids_in_blocks)};
    const auto oids_in_blocks_but_not_in_graph{make_set_difference(oids_in_blocks, oids_in_graph)};
    throw common::internal_exception{
        "Expected the same object IDs to be represented in the graph (G) and in the blocks (B). Found the following "
        "set differences: G\\B=[{}], B\\G=[{}].",
        fmt::join(oids_in_graph_but_not_in_blocks, ", "), fmt::join(oids_in_blocks_but_not_in_graph, ", ")};
  }
  return oids_in_graph;
}

void verify_exactly_one_root_block_per_object(const bpmn_blocks_t& root_blocks,
                                              const distinct_bpmn_block_object_ids_t& object_ids) {
  // Get all object IDs of root blocks
  std::vector<bpmn_block_object_id_t> oids_in_root_blocks{};
  oids_in_root_blocks.reserve(root_blocks.size());
  std::ranges::transform(root_blocks, std::back_inserter(oids_in_root_blocks), &bpmn_block::object_id);
  std::ranges::sort(oids_in_root_blocks);

  // Get all distinct object IDs of root blocks
  distinct_bpmn_block_object_ids_t distinct_oids_in_root_blocks{oids_in_root_blocks.begin(), oids_in_root_blocks.end()};

  legacy_embedded_debug_assert(make_set_difference(distinct_oids_in_root_blocks, object_ids).empty(),
               "Pre-condition does not hold: The passed object IDs [{}] do not contain all object IDs present in the "
               "root blocks [{}].",
               fmt::join(object_ids, ", "), fmt::join(distinct_oids_in_root_blocks, ", "));

  if (distinct_oids_in_root_blocks.size() != oids_in_root_blocks.size() || distinct_oids_in_root_blocks != object_ids) {
    // There is some inconsistency present. Find out what the exact issue is...

    const auto duplicates_in_roots{make_set_difference(oids_in_root_blocks, distinct_oids_in_root_blocks)};
    common::runtime_assert(duplicates_in_roots.empty(), "The following object IDs have multiple root blocks: [{}]",
                           fmt::join(duplicates_in_roots, ", "));

    const auto object_ids_without_root_block{make_set_difference(object_ids, distinct_oids_in_root_blocks)};
    common::runtime_assert(object_ids_without_root_block.empty(), "The following object IDs have no root block: [{}]",
                           fmt::join(object_ids_without_root_block, ", "));

    // One of the above asserts is expected to throw
    legacy_embedded_ctl::assert_unreachable();
  }
}

/**
 * Verifies the consistency of a single root block:
 * 1) A root block should not have a parent block set
 * 2) A root block should only have a single child block
 *
 * @param root_block The root block we are verifying.
 * @param blocks All blocks (including the root block).
 */
void verify_root_block_consistency(const bpmn_block& root_block, const bpmn_blocks_t& blocks) {
  common::runtime_assert(root_block.parent_id == NO_BLOCK_PARENT_ID,
                         "Expected no parent ID for root block [{}] but got [{}].", NO_BLOCK_PARENT_ID,
                         root_block.parent_id);
  const auto is_child_of_root_block{
      [root_block_id = root_block.block_id](const bpmn_block& value) { return value.parent_id == root_block_id; }};
  common::runtime_assert(std::none_of(std::next(std::ranges::find_if(blocks, is_child_of_root_block)),
                                      std::cend(blocks), is_child_of_root_block),
                         "The root block has more than one child.");
}

[[nodiscard]] bpmn_blocks_t extract_root_blocks(const bpmn_blocks_t& blocks) {
  bpmn_blocks_t root_blocks{};
  std::ranges::copy_if(blocks, std::back_inserter(root_blocks), is_root_block);
  return root_blocks;
}

/**
 * Verifies the consistency of all root blocks.
 */
void verify_root_block_consistency(const bpmn_blocks_t& blocks, const distinct_bpmn_block_object_ids_t& object_ids) {
  const auto root_blocks{extract_root_blocks(blocks)};
  common::runtime_assert(!root_blocks.empty(), "No root blocks present.");
  verify_exactly_one_root_block_per_object(root_blocks, object_ids);
  std::ranges::for_each(root_blocks,
                        [&blocks](const bpmn_block& root_block) { verify_root_block_consistency(root_block, blocks); });
}

/**
 * Verifies whether the block structure contains maximum length sequence chains. i.e. a sequence block should
 * not contain another sequence block.
 */
void verify_sequence_consistency(const bpmn_blocks_t& blocks) {
  common::runtime_assert(std::ranges::none_of(blocks,
                                              [&blocks](const bpmn_block& value) {
                                                // return whether the current block is part of a sequence of sequence(s)
                                                return is_sequence_block(value) &&
                                                       is_sequence_block(blocks.at(value.parent_id));
                                              }),
                         "Found sequence of sequence construct in block structure.");
}

/**
 * Verifies whether a block ID matches it's index into the *blocks* container.
 */
void verify_block_id_consistency(const bpmn_blocks_t& blocks) {
  common::runtime_assert(
      std::ranges::all_of(blocks,
                          [&blocks](const bpmn_block& value) {
                            const auto block_idx{
                                std::distance(blocks.data(), &value)};  // compute the index of 'block' within 'blocks'
                            return value.block_id == block_idx;
                          }),
      "In the array of blocks, each block must be located at (i.e., indexable by) its block ID.");
}

/** Verifies that within a block hierarchy, all blocks have the same object ID */
void verify_block_object_ids_in_hierarchy_consistency(const bpmn_blocks_t& blocks) {
  std::ranges::for_each(blocks, [&blocks](const bpmn_block& current_block) {
    if (!is_root_block(current_block)) {
      const auto current_block_object_id{current_block.object_id};
      const auto& parent_block_object_id{blocks.at(current_block.parent_id).object_id};
      common::runtime_assert(
          parent_block_object_id == current_block_object_id,
          "Block with ID [{}] has object ID [{}] but its parent block with ID [{}] has object ID [{}].",
          current_block.block_id, current_block_object_id, current_block.parent_id, parent_block_object_id);
    }
  });
}

/** All consistency checks for a valid block structure definition should be put here */
void verify_block_consistency(const bpmn_graph_with_block_structure& graph_with_block_structure) {
  const auto expected_object_ids{verify_object_ids_consistency(graph_with_block_structure)};
  const auto& blocks{graph_with_block_structure.blocks()};
  verify_root_block_consistency(blocks, expected_object_ids);
  verify_sequence_consistency(blocks);
  verify_block_id_consistency(blocks);
  verify_block_object_ids_in_hierarchy_consistency(blocks);
}

[[nodiscard]] std::string to_string(const bpmn_blocks_t& blocks) {
  std::ostringstream strm{};
  std::ranges::for_each(blocks, [&strm](const bpmn_block& value) { strm << to_string(value) << '\n'; });
  return strm.str();
}

[[nodiscard]] std::string to_string(
    const bpmn_graph_with_block_structure::vertex_id_to_block_id_mapping_t& vertex_id_to_block_id_mapping) {
  std::ostringstream strm{};
  std::ranges::for_each(
      vertex_id_to_block_id_mapping, [&strm](const std::pair<vertex_id_type, bpmn_block_ids_t>& value) {
        const auto& [vertex_id, block_ids]{value};
        strm << fmt::format("vertex_id=[{}], block_ids=[{}]", vertex_id, fmt::join(block_ids, ", ")) << '\n';
      });
  return strm.str();
}

[[nodiscard]] std::string to_string(const bpmn_graph_with_block_structure& graph_with_block_structure) {
  auto graph_as_str{to_string(graph_with_block_structure.graph())};
  auto blocks_as_str{to_string(graph_with_block_structure.blocks())};
  auto mapping_as_str{to_string(graph_with_block_structure.vertex_id_to_block_id_mapping())};
  return fmt::format("-Graph:\n{}\n-Blocks:\n{}\n-Mapping:\n{}\n", std::move(graph_as_str), std::move(blocks_as_str),
                     std::move(mapping_as_str));
}

}  // anonymous namespace

bpmn_graph_with_block_structure::bpmn_graph_with_block_structure(
    bpmn_graph graph, bpmn_blocks_t blocks, vertex_id_to_block_id_mapping_t vertex_id_to_block_id_mapping)
    : bpmn_graph{std::move(graph)},
      blocks_{std::move(blocks)},
      vertex_id_to_block_id_mapping_{std::move(vertex_id_to_block_id_mapping)} {
  try {
    verify_block_consistency(*this);
    // TODO(n.weber): CPL-10394 - We might want to integrate consistency checks for the vertex to block mapping too
  } catch (const legacy_embedded_ctl::internal_error& error) {
    log::jerror("Error during 'bpmn_graph_with_block_structure' consistency checks.",  //
                {{"error_message", error.internal_message()},                          //
                 {"bpmn_graph_with_block_structure", to_string(*this)}});
    throw;
  }
  // Not a technical requirement but makes working with the output - especially for testing - much simpler. Also, the
  // ordered'ness is already a byproduct of our structures, so we do not pay (read: compute) anything extra for that.
  legacy_embedded_debug_assert(std::ranges::all_of(vertex_id_to_block_id_mapping_, [](const auto& vertex_id_to_block_ids) {
    const auto& block_ids{vertex_id_to_block_ids.second};
    return std::ranges::is_sorted(block_ids);
  }));
  // Also not a technical requirement but a byproduct if everything works as expected.
  legacy_embedded_debug_assert(std::ranges::all_of(blocks_,
                                   [object_id_of_finished_blocks = std::unordered_set<bpmn_block_object_id_t>{},
                                    current_object_id = blocks_.front().object_id](const bpmn_block& block) mutable {
                                     const auto& oid{block.object_id};
                                     if (oid == current_object_id) {
                                       return true;
                                     }
                                     if (object_id_of_finished_blocks.contains(oid)) {
                                       return false;
                                     }
                                     object_id_of_finished_blocks.insert(current_object_id);
                                     current_object_id = oid;
                                     return true;
                                   }),
               "Block is not stored next to the other blocks with the same object ID.");
}

bpmn_graph_with_block_structure::bpmn_graph_with_block_structure(
    bpmn_graph graph, bpmn_blocks_t blocks, const block_id_to_contained_vertices_mapping_t& block_to_node_mapping)
    : bpmn_graph_with_block_structure{std::move(graph), std::move(blocks), unflatten_mapping(block_to_node_mapping)} {
  common::runtime_assert(blocks_.size() == block_to_node_mapping.size(),
                         "Size mismatch between number of blocks [{}] and mapped nodes [{}].", blocks_.size(),
                         block_to_node_mapping.size());
}

const bpmn_graph& bpmn_graph_with_block_structure::graph() const { return *this; }

const bpmn_blocks_t& bpmn_graph_with_block_structure::blocks() const { return blocks_; }

const bpmn_graph_with_block_structure::vertex_id_to_block_id_mapping_t&
bpmn_graph_with_block_structure::vertex_id_to_block_id_mapping() const {
  return vertex_id_to_block_id_mapping_;
}

bpmn_blocks_t bpmn_graph_with_block_structure::blocks_for_vertex_id(const vertex_id_type vertex_id) const {
  bpmn_blocks_t blocks_for_vertex_id{};
  const auto& block_ids_for_vertex_id{vertex_id_to_block_id_mapping_.at(vertex_id)};
  blocks_for_vertex_id.reserve(block_ids_for_vertex_id.size());
  std::ranges::transform(block_ids_for_vertex_id, std::back_inserter(blocks_for_vertex_id),
                         [this](const bpmn_block_id_t block_id) { return blocks().at(block_id); });
  return blocks_for_vertex_id;
}

bpmn_graph_with_block_structure_builder& bpmn_graph_with_block_structure_builder::block(bpmn_block value) {
  common::runtime_assert(next_block_index() == value.block_id,
                         "Pre condition not met: We require the blocks to be indexed by their block ID. However, the "
                         "block to add has ID [{}] but the next expected block ID is [{}].",
                         value.block_id, next_block_index());
  blocks_.push_back(value);
  block_to_node_mapping_.emplace_back();
  return *this;
}

bpmn_graph_with_block_structure_builder& bpmn_graph_with_block_structure_builder::blocks(const bpmn_blocks_t& values) {
  std::ranges::for_each(values, [this](const bpmn_block& value) { block(value); });
  return *this;
}

bpmn_block_id_t bpmn_graph_with_block_structure_builder::add_block(const bpmn_block_id_t parent_id,
                                                                   const bpmn_block_object_id_t oid,
                                                                   const bpmn_block_type type) {
  const bpmn_block_id_t block_id{next_block_index()};
  block({.block_id = block_id, .parent_id = parent_id, .object_id = oid, .block_type = type});
  return block_id;
}

bpmn_block_id_t bpmn_graph_with_block_structure_builder::add_root_block(const bpmn_block_object_id_t oid) {
  return add_block(NO_BLOCK_PARENT_ID, oid, bpmn_block_type::ROOT);
}

bpmn_graph_with_block_structure_builder& bpmn_graph_with_block_structure_builder::add_vertex_id_to_block_id_mapping(
    const vertex_id_type vertex_id, const bpmn_block_id_t block_id) {
  block_to_node_mapping_.at(block_id).push_back(vertex_id);
  return *this;
}

bpmn_graph_with_block_structure_builder& bpmn_graph_with_block_structure_builder::mappings(
    const bpmn_graph_with_block_structure::vertex_id_to_block_id_mapping_t& vertex_id_to_block_id_mapping) {
  std::ranges::for_each(vertex_id_to_block_id_mapping, [this](const auto& vertex_id_to_block_ids) {
    const auto& [vertex_id, block_ids]{vertex_id_to_block_ids};
    std::ranges::for_each(block_ids, [this, vid = vertex_id](const bpmn_block_id_t block_id) {
      add_vertex_id_to_block_id_mapping(vid, block_id);
    });
  });
  return *this;
}

bpmn_graph_with_block_structure_builder& bpmn_graph_with_block_structure_builder::mappings(
    const block_id_to_contained_vertices_mapping_t& block_to_node_mapping) {
  for (bpmn_block_id_t block_id{0}; std::cmp_less(block_id, block_to_node_mapping.size()); ++block_id) {
    const auto& vertex_ids{block_to_node_mapping.at(block_id)};
    std::ranges::for_each(vertex_ids, [this, block_id](const vertex_id_type vertex_id) {
      add_vertex_id_to_block_id_mapping(vertex_id, block_id);
    });
  }
  return *this;
}

vertex_id_type bpmn_graph_with_block_structure_builder::add_vertex_to_current_block(vertex_type type) {
  const auto vertex_id{add_vertex(type)};
  const bpmn_block_id_t current_block_id{next_block_index() - 1};
  common::runtime_assert(current_block_id >= 0, "Trying to add vertex [{}] to current block before any block exists.",
                         to_string(type));
  add_vertex_id_to_block_id_mapping(vertex_id, current_block_id);
  return vertex_id;
}

bpmn_graph_with_block_structure bpmn_graph_with_block_structure_builder::build_with_block_structure() const {
  auto invalidated_blocks{details::invalidate_sequence_block_chains(blocks_)};
  auto [blocks,
        mapping]{details::remove_invalidated_blocks_and_apply_remapping(invalidated_blocks, block_to_node_mapping_)};
  return {bpmn_graph_builder::build(), std::move(blocks), mapping};
}

bpmn_block_id_t bpmn_graph_with_block_structure_builder::next_block_index() const {
  return legacy_embedded_ctl::cast<bpmn_block_id_t>(blocks_.size());
}

}  // namespace celonis::accelerator::operators::process::bpmn
