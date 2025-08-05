#include "replay_types.h"

#include <algorithm>

#include "legacy_embedded_ctl/hash.h"
#include "modules/common/exceptions.h"

namespace celonis::accelerator::operators::process::bpmn {

const marking_t& marking_with_num_fired_tasks::marking() const noexcept { return marking_; }

size_t marking_with_num_fired_tasks::num_fired_tasks() const noexcept { return num_fired_tasks_; }

bool marking_with_num_fired_tasks::operator==(const marking_with_num_fired_tasks& rhs) const noexcept {
  return marking_ == rhs.marking() && num_fired_tasks_ == rhs.num_fired_tasks();
}

namespace {

struct token_hash final {
  [[nodiscard]] size_t operator()(const token_t& key) const noexcept {
    size_t hash_value{0};
    legacy_embedded_ctl::hash_combine(hash_value, key.get_source_id());
    legacy_embedded_ctl::hash_combine(hash_value, key.get_target_id());
    return hash_value;
  }
};

}  // anonymous namespace

size_t marking_hash::operator()(const marking_t& key) const { return legacy_embedded_ctl::hash_range<marking_t, token_hash>(key); }

size_t marking_hash::operator()(const marking_with_num_fired_tasks& key) const {
  size_t hash_value{legacy_embedded_ctl::hash_range<marking_t, token_hash>(key.marking())};
  legacy_embedded_ctl::hash_combine(hash_value, key.num_fired_tasks());
  return hash_value;
}

transition transition::swap_transition_direction(transition transition_to_swap) {
  std::swap(transition_to_swap.consumed_, transition_to_swap.produced_);
  return transition_to_swap;
}

transition::transition(const vertex_id_type vertex_id, tokens_t consumed, tokens_t produced,
                       const allocator_type& alloc)
    : vertex_id_{vertex_id}, consumed_(std::move(consumed), alloc), produced_(std::move(produced), alloc) {
  // Every consumed token must have 'vertex_id_' as target vertex
  const auto invalid_consumed_token_iter{
      std::find_if(consumed_.cbegin(), consumed_.cend(),
                   [this](const token_t& token) { return token.get_target_id() != vertex_id_; })};
  if (invalid_consumed_token_iter != consumed_.cend()) {
    throw common::internal_exception{
        "Error during transition creation for vertex with ID [{}]. Invalid target vertex ID of consumed token "
        "[{}->{}].",
        vertex_id_, invalid_consumed_token_iter->get_source_id(), invalid_consumed_token_iter->get_target_id()};
  }
  // Every produced token must have 'vertex_id_' as source vertex
  const auto invalid_produced_token_iter{
      std::find_if(produced_.cbegin(), produced_.cend(),
                   [this](const token_t& token) { return token.get_source_id() != vertex_id_; })};
  if (invalid_produced_token_iter != produced_.cend()) {
    throw common::internal_exception{
        "Error during transition creation for vertex with ID [{}]. Invalid source vertex ID of produced token "
        "[{}->{}].",
        vertex_id_, invalid_produced_token_iter->get_source_id(), invalid_produced_token_iter->get_target_id()};
  }
}

vertex_id_type transition::vertex_id() const noexcept { return vertex_id_; }

const tokens_t& transition::consumed() const noexcept { return consumed_; }

const tokens_t& transition::produced() const noexcept { return produced_; }

bool transition::operator==(const transition& rhs) const noexcept {
  return vertex_id() == rhs.vertex_id() && consumed() == rhs.consumed() && produced() == rhs.produced();
}

}  // namespace celonis::accelerator::operators::process::bpmn
