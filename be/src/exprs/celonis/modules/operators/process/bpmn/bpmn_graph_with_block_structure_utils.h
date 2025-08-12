#pragma once

#include "modules/operators/process/bpmn/bpmn_graph_with_block_structure_types.h"

namespace celonis::accelerator::operators::process::bpmn::details {

/** Due to how we handle the build-up of bpmn graphs with blocks, there might be sub-process trees with a structure like
 * the following: SEQ ( SEQ ( X ), SEQ ( Y ) ). As part of this 'cleanup' function, such structures will be reduced to a
 * more intuitive format like SEQ ( X, Y ) for the example before.
 * @return the same number of blocks as given to this function but with sequence chains invalidated
 */
[[nodiscard]] bpmn_blocks_t invalidate_sequence_block_chains(const bpmn_blocks_t& blocks_to_cleanup);

struct blocks_and_mapping_result {
  explicit blocks_and_mapping_result(const size_t number_of_blocks)
      : blocks(number_of_blocks), mapping(number_of_blocks) {}
  blocks_and_mapping_result(bpmn_blocks_t blocks, block_id_to_contained_vertices_mapping_t mapping)
      : blocks{std::move(blocks)}, mapping{std::move(mapping)} {}
  bpmn_blocks_t blocks;
  block_id_to_contained_vertices_mapping_t mapping;
};

/**
 * Traverses the given blocks and assigns a (potentially new) block ID to each. This basically makes a renumbering of
 * blocks such that there are no gaps in the IDs due to invalid blocks anymore. Also, applies the new block IDs to the
 * previous block to vertex mapping.
 * A dense numbering is not strictly necessary but makes reasoning about the blocks and writing tests much simpler.
 */
[[nodiscard]] blocks_and_mapping_result remove_invalidated_blocks_and_apply_remapping(
    const bpmn_blocks_t& old_blocks, const block_id_to_contained_vertices_mapping_t& old_mapping);

}  // namespace celonis::accelerator::operators::process::bpmn::details
