#include "modules/operators/process/bpmn/bpmn_graph_with_block_structure_utils.h"

#include <algorithm>
#include <vector>

namespace celonis::accelerator::operators::process::bpmn::details {

namespace {

/** Invalidates the given block and adds it to the cleaned_up_blocks */
void invalidate_block_and_add_to_cleaned_up(const bpmn_block& value, bpmn_blocks_t& cleaned_up_blocks) {
  bpmn_block block_copy{value};
  block_copy.block_type = bpmn_block_type::INVALID;
  cleaned_up_blocks.at(value.block_id) = block_copy;
}

/**
 * Right now, the parent doesn't know its children but the otherway around the child knows its parent. However, to
 * traverse the tree for the cleanup, it is much easier if the parent knows its children as well. For that, we create
 * a mapping which returns all the child block IDs for a given (parent) block ID.
 */
[[nodiscard]] std::vector<bpmn_block_ids_t> make_block_id_to_child_blocks_ids_mapping(const bpmn_blocks_t& blocks) {
  std::vector<bpmn_block_ids_t> block_id_to_child_blocks_ids_mapping{};
  block_id_to_child_blocks_ids_mapping.resize(blocks.size());
  std::ranges::for_each(blocks, [&block_id_to_child_blocks_ids_mapping](const bpmn_block& block) {
    if (const auto parent_id{block.parent_id}; parent_id >= 0) {  // The root node has no parent (negative parent ID)
      block_id_to_child_blocks_ids_mapping.at(parent_id).push_back(block.block_id);
    }
  });
  return block_id_to_child_blocks_ids_mapping;
}

/**
 * Main logic for the cleanup. If a parent block is a sequence, it calls this function on all its children:
 * - If child is sequence as well, continue; otherwise do nothing (return)
 * - Invalidate the child block
 * - Remap the child's children (i.e., 'grandchildren' of 'parent') parent IDs to 'parent' (see illustration below)
 * - Recursively repeat the above steps for all children of the child (i.e., 'grandchildren' of 'parent')
 *
 * Illustration used for comments in the function.
 *
 *   (parent; ID=0) SEQ
 *                 /   \
 * (child; ID=1) SEQ   ...
 *             /  |  \
 *         [grandchildren]
 */
void collapse_all_child_sequences_rec(const bpmn_block& child_block,
                                      std::vector<bpmn_block_ids_t>& block_id_to_child_blocks_ids_mapping,
                                      bpmn_blocks_t& cleaned_up_blocks) {
  if (is_sequence_block(child_block)) {
    invalidate_block_and_add_to_cleaned_up(child_block, cleaned_up_blocks);
    /*
     * If the given block to this function was 'child' from the figure above, all its children parent IDs must be
     * adapted after the 'child' was invalidated (see above).
     * For that, (1) we take the parent ID of 'child' (0 in the example), and, ...
     */
    const auto new_parent_id_for_grandchildren_blocks{child_block.parent_id};
    const auto& grandchildren_block_ids{block_id_to_child_blocks_ids_mapping.at(child_block.block_id)};
    for (const auto grandchild_block_id : grandchildren_block_ids) {
      auto& grandchild_block{cleaned_up_blocks.at(grandchild_block_id)};
      // (2) overwrite the 'grandchildren' parent IDs with it.
      grandchild_block.parent_id = new_parent_id_for_grandchildren_blocks;
      // Recursively call this function again on the grandchildren for the case they are sequences as well
      collapse_all_child_sequences_rec(grandchild_block, block_id_to_child_blocks_ids_mapping, cleaned_up_blocks);
    }
  }
}

[[nodiscard]] bool is_valid_block(const bpmn_block& block) { return block.block_type != bpmn_block_type::INVALID; }

struct mapping_and_count_of_valid_blocks {
  bpmn_block_ids_t old_block_ids_to_new_block_ids_mapping;
  size_t count_of_valid_blocks;
};

/** Creates a mapping from old block ID to a potentially new block ID. Also, returns the count of valid blocks */
[[nodiscard]] mapping_and_count_of_valid_blocks create_old_block_ids_to_new_block_ids_mapping(
    const bpmn_blocks_t& blocks) {
  // initially the mapping is completely filled with 'invalid' identifiers
  bpmn_block_ids_t block_ids_mapping(blocks.size(), INVALID_BLOCK_ID);
  size_t count_of_valid_blocks{0};
  for (bpmn_block_id_t current_block_id{0}; const auto& block : blocks) {
    if (is_valid_block(block)) {
      block_ids_mapping.at(block.block_id) = current_block_id++;
      ++count_of_valid_blocks;
    }
  }
  legacy_embedded_debug_assert(count_of_valid_blocks ==
               (block_ids_mapping.size() - std::ranges::count(block_ids_mapping, INVALID_BLOCK_ID)));

  return {block_ids_mapping, count_of_valid_blocks};
}

[[nodiscard]] bpmn_block block_with_new_block_and_parent_id(bpmn_block old_block, const bpmn_block_id_t new_block_id,
                                                            const bpmn_block_id_t new_parent_block_id) {
  legacy_embedded_debug_assert(is_valid_block(old_block));
  old_block.block_id = new_block_id;
  old_block.parent_id = new_parent_block_id;
  return old_block;
}

}  // anonymous namespace

bpmn_blocks_t invalidate_sequence_block_chains(const bpmn_blocks_t& blocks_to_cleanup) {
  auto block_id_to_child_blocks_ids_mapping{make_block_id_to_child_blocks_ids_mapping(blocks_to_cleanup)};
  bpmn_blocks_t cleaned_up_blocks{blocks_to_cleanup};

  for (const auto& block : cleaned_up_blocks) {
    if (is_sequence_block(block)) {
      const auto& child_blocks_ids{block_id_to_child_blocks_ids_mapping.at(block.block_id)};
      for (const auto child_block_id : child_blocks_ids) {
        const auto& child_block{blocks_to_cleanup.at(child_block_id)};
        collapse_all_child_sequences_rec(child_block, block_id_to_child_blocks_ids_mapping, cleaned_up_blocks);
      }
    }
  }

  return cleaned_up_blocks;
}

blocks_and_mapping_result remove_invalidated_blocks_and_apply_remapping(
    const bpmn_blocks_t& old_blocks, const block_id_to_contained_vertices_mapping_t& old_mapping) {
  common::runtime_assert(
      old_blocks.size() == old_mapping.size(),
      "Size mismatch. Expected the number of old blocks [{}] to be the same as the size of the old mapping [{}].",
      old_blocks.size(), old_mapping.size());
  const auto [old_to_new_block_id_mapping,
              count_of_valid_blocks]{create_old_block_ids_to_new_block_ids_mapping(old_blocks)};

  blocks_and_mapping_result result{count_of_valid_blocks};

  for (const auto& old_block : old_blocks) {
    const auto old_block_id{old_block.block_id};
    const auto old_block_parent_id{old_block.parent_id};
    if (const auto new_block_id{old_to_new_block_id_mapping.at(old_block_id)}; new_block_id != INVALID_BLOCK_ID) {
      // Either a new block ID from the mapping, or, 'NO_BLOCK_PARENT_ID' if 'old_block' is the root
      const auto new_block_parent_id{old_block_parent_id != NO_BLOCK_PARENT_ID
                                         ? old_to_new_block_id_mapping.at(old_block_parent_id)
                                         : NO_BLOCK_PARENT_ID};
      result.blocks.at(new_block_id) = block_with_new_block_and_parent_id(old_block, new_block_id, new_block_parent_id);
      result.mapping.at(new_block_id) = old_mapping.at(old_block_id);
    }
  }
  legacy_embedded_debug_assert(std::ranges::all_of(result.blocks, [&blocks = std::as_const(result.blocks)](const bpmn_block& block) {
    const auto block_idx{std::distance(blocks.data(), &block)};  // compute the index of 'block' within 'blocks'
    return block.block_id == block_idx;
  }));
  return result;
}

}  // namespace celonis::accelerator::operators::process::bpmn::details
