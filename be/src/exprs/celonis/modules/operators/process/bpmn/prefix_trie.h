#pragma once

#include <span>

#include "modules/operators/process/bpmn/a_star/replay.h"
#include "modules/operators/process/bpmn/replay_types.h"

namespace celonis::accelerator::operators::process::bpmn {

struct prefix_trie_node {
  std::map<row_id, prefix_trie_node> children;
};

class prefix_trie {
 public:
  prefix_trie() : root_(prefix_trie_node()) {}

  void add(std::span<const row_id> prefix);

  [[nodiscard]] bool contains_prefix_of(std::span<const row_id> variant) const;

 private:
  prefix_trie_node root_;
};

}  // namespace celonis::accelerator::operators::process::bpmn