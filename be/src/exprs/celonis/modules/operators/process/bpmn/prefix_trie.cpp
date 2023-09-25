#include "prefix_trie.h"

namespace celonis::accelerator::operators::process::bpmn {

void prefix_trie::add(std::span<const row_id> prefix) {
  if (prefix.empty()) {
    return;
  }

  prefix_trie_node* current_node = &root_;

  // Step 1: Find the longest already existing prefix of the prefix
  auto it = prefix.begin();
  while (it != prefix.end() && current_node->children.contains(*it)) {
    current_node = &current_node->children[*it];
    ++it;
  }

  // Step 2: Add the remaining elements of the new prefix, if we aren't already at the end of an existing prefix
  if (current_node == &root_ || !current_node->children.empty()) {
    while (it != prefix.end()) {
      current_node = &current_node->children.try_emplace(*it).first->second;
      ++it;
    }
  }
  // Ensure the current node is end of a prefix
  current_node->children.clear();
}

bool prefix_trie::contains_prefix_of(std::span<const row_id> variant) const {
  if (root_.children.empty()) {
    return false;
  }

  const prefix_trie_node* current_node = &root_;
  for (auto num : variant) {
    if (!current_node->children.contains(num)) {
      return current_node->children.empty();
    }
    current_node = &current_node->children.at(num);
  }
  return current_node->children.empty();
}

}  // namespace celonis::accelerator::operators::process::bpmn
