#pragma once

#include <scoped_allocator>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "legacy_embedded_ctl/memory/resource_owning_allocator.h"
#include "legacy_embedded_ctl/named_type.h"
#include "modules/common/int_types.h"
#include "modules/operators/process/bpmn/edge.h"
#include "modules/operators/process/bpmn/vertex_types.h"

/** Types and type alias definitions useful for token-based replay (https://en.wikipedia.org/wiki/Token-based_replay) */
namespace celonis::accelerator::operators::process::bpmn {

// A strongly typed trace of activity ids, i.e. a variant
using activity_trace_t = legacy_embedded_ctl::named_type<std::vector<row_id>, struct strong_variant_type, legacy_embedded_ctl::strongly_typed_vector>;
// A strongly typed trace of arbitrary row_ids, e.g. a sequence of timestamp column row_ids
using trace_t = legacy_embedded_ctl::named_type<std::vector<row_id>, struct strong_trace_type, legacy_embedded_ctl::strongly_typed_vector>;

/**
 * A token represents an enabled vertex in the marking of a BPMN model. To uniquely identify the source of a token in a
 * marking, it can simply be defined as an edge.
 */
using token_t = edge;
using tokens_t = std::vector<token_t, legacy_embedded_ctl::resource_owning_allocator<token_t>>;

/** A marking represents one unique state of tokens on the BPMN graph during replay execution. */
using marking_t = std::multiset<token_t, std::less<>, legacy_embedded_ctl::resource_owning_allocator<token_t>>;

class marking_with_num_fired_tasks {
 public:
  marking_with_num_fired_tasks(marking_t marking, size_t num_fired_tasks)
      : marking_{std::move(marking)}, num_fired_tasks_{num_fired_tasks} {};

  [[nodiscard]] const marking_t& marking() const noexcept;
  [[nodiscard]] size_t num_fired_tasks() const noexcept;
  [[nodiscard]] bool operator==(const marking_with_num_fired_tasks& rhs) const noexcept;

 private:
  marking_t marking_{};
  size_t num_fired_tasks_{};
};

/** Custom hash functor for a marking_t and marking_with_num_fired_tasks */
struct marking_hash {
  [[nodiscard]] size_t operator()(const marking_t& key) const;
  [[nodiscard]] size_t operator()(const marking_with_num_fired_tasks& key) const;
};

/**
 * A collection of markings (found during replaying)
 * A std::scoped_allocator_adaptor is used such that the legacy_embedded_ctl::resource_owning_allocator gets passed down into
 * marking_t.
 */
using set_allocator_type = std::scoped_allocator_adaptor<legacy_embedded_ctl::resource_owning_allocator<marking_t>>;
using markings_t = std::unordered_set<marking_t, marking_hash, std::equal_to<>, set_allocator_type>;

/** Represents a token transition which contains the consumed (enabled) tokens and the produced tokens thereof */
class transition final {
 public:
  // Allocator used for the consumed_ and produced_ tokens.
  using allocator_type = legacy_embedded_ctl::resource_owning_allocator<token_t>;

  /** Allows to skip the transition invariant validation when the transition is used for inverse firing */
  [[nodiscard]] static transition swap_transition_direction(transition transition_to_swap);
  /** Constructs a transition and takes care of verifying the invariant: consumed[_,id] -> produced[id, _] */
  transition(vertex_id_type vertex_id, tokens_t consumed, tokens_t produced,
             const allocator_type& alloc = allocator_type{});

  transition(const transition& other, const allocator_type& alloc)
      : vertex_id_{other.vertex_id_}, consumed_(other.consumed_, alloc), produced_(other.produced_, alloc) {}

  transition(transition&& other, const allocator_type& alloc)
      : vertex_id_{other.vertex_id_},
        consumed_(std::move(other.consumed_), alloc),
        produced_(std::move(other.produced_), alloc) {}

  [[nodiscard]] vertex_id_type vertex_id() const noexcept;
  [[nodiscard]] const tokens_t& consumed() const noexcept;
  [[nodiscard]] const tokens_t& produced() const noexcept;
  [[nodiscard]] bool operator==(const transition& rhs) const noexcept;
  [[nodiscard]] auto operator<=>(const transition& rhs) const noexcept { return vertex_id_ <=> rhs.vertex_id_; }

 private:
  // vertex_id_ is duplicated data, as it is also stored in each of the consumed and produced edges.
  vertex_id_type vertex_id_{INVALID_VERTEX_ID};
  // Except for parallel gateway transitions, these vectors will contain only one token
  tokens_t consumed_{};
  tokens_t produced_{};
};
using transitions_t = std::vector<transition>;

using transitions_map_t = std::unordered_map<marking_t, transitions_t, marking_hash>;

}  // namespace celonis::accelerator::operators::process::bpmn
