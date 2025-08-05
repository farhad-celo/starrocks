#pragma once

#include <optional>
#include <scoped_allocator>
#include <vector>

#include "legacy_embedded_ctl/memory/resource_owning_allocator.h"
#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/alignment/input_output_mapper.h"
#include "modules/operators/process/alignment/petri_net/petri_net_entities.h"

namespace celonis::accelerator::operators::process::alignment {

enum class alignment_move_type { UNMAPPED, LOG, MODEL, SYNC };

class alignment_move {
  using visible_label_type = string_to_int_mapper::label_id_t;
  using transition_id_type = petri_net::petri_net_transition_id;
  using move_on_model_type = std::optional<transition_id_type>;

 public:
  [[nodiscard]] static constexpr alignment_move sync(visible_label_type label, transition_id_type transition) {
    return alignment_move{label, transition, alignment_move_type::SYNC};
  }
  [[nodiscard]] static constexpr alignment_move log(visible_label_type label) {
    return alignment_move{label, std::nullopt, alignment_move_type::LOG};
  }
  [[nodiscard]] static constexpr alignment_move model(visible_label_type label, transition_id_type transition_id) {
    return alignment_move{label, transition_id, alignment_move_type::MODEL};
  }
  [[nodiscard]] static constexpr alignment_move unmapped(visible_label_type label) {
    return alignment_move{label, std::nullopt, alignment_move_type::UNMAPPED};
  }
  // We do not provide a default constructor but we provide a dummy move for when a default constructor would be needed
  // This is a synchronous move of a tau label to the NULL transition. Use it only if you know what you are doing
  [[nodiscard]] static alignment_move dummy() {
    return alignment_move::sync(string_to_int_mapper::get_tau_transition_id(), petri_net::petri_net_transition_id{0});
  }

  [[nodiscard]] constexpr bool is_sync() const noexcept { return type_ == alignment_move_type::SYNC; }
  [[nodiscard]] constexpr bool is_model() const noexcept { return type_ == alignment_move_type::MODEL; }
  [[nodiscard]] constexpr bool is_log() const noexcept { return type_ == alignment_move_type::LOG; }
  [[nodiscard]] constexpr bool is_unmapped() const noexcept { return type_ == alignment_move_type::UNMAPPED; }
  [[nodiscard]] constexpr bool is_tau() const noexcept {
    return label_ == string_to_int_mapper::get_tau_transition_id();
  }
  [[nodiscard]] constexpr bool is_move_on_log() const noexcept { return is_sync() || is_log() || is_unmapped(); }

  [[nodiscard]] bool operator==(const alignment_move& rhs) const noexcept = default;

  [[nodiscard]] auto label() const noexcept { return label_; }
  [[nodiscard]] auto move_on_model() const noexcept { return move_on_model_; }
  [[nodiscard]] constexpr alignment_move_type type() const noexcept { return type_; }

 private:
  constexpr alignment_move(visible_label_type label, move_on_model_type move_on_model, alignment_move_type type)
      : label_{label}, move_on_model_{move_on_model}, type_{type} {}

  // If model move, then label will hold the Petri net's label.
  visible_label_type label_;
  move_on_model_type move_on_model_;
  alignment_move_type type_;
};

class trace_alignment {
 public:
  void reserve(size_t trace_size) { data_.reserve(trace_size); }
  void add(alignment_move move);

  // Maps sequences of [L]A|[M]A or [M]A|[L]A to [S]A. This is cheap, requires only a linear pass and no allocations
  void prune_simple();

  /**
   * @brief Prunes the alignment by partitioning it into fixed-sized ranges and solving a Longest Common Subsequence
   *    problem for each range.
   *
   * @param chunk_size the size of the chunks to split the trace
   * @example Given this    = [S]A [L]A [L]B [M]A [M]B [L]C [L]D [M]C [M]D [M]D
   *    this->prune_lcs(10) = [S]A [S]A [S]B [S]C [S]D [M]D
   *    this->prune_lcs(4)  = [S]A [S]A [L]B      [M]B [S]S [L]D      [M]D [M]D
   * @note The implementation uses the sequence_aligner code, which means that alignments containing unmapped moves are
   *    not supported, but this is alright because unmapped moves are rather a PQL concept.
   * @note This call is relatively expensive, so don't over-use it.
   */
  void prune_lcs(size_t chunk_size, const common::execution_context& context);

  [[nodiscard]] const auto& data() const noexcept { return data_; }
  [[nodiscard]] size_t size() const noexcept { return data_.size(); }
  [[nodiscard]] uint64_t cost() const noexcept { return cost_; }
  [[nodiscard]] uint64_t visible_model_moves_count() const noexcept { return visible_model_moves_count_; }

  // Returns the minimum cost assuming a highly successful LCS pruning
  [[nodiscard]] uint64_t cost_optimistic_lcs_pruning() const noexcept {
    const auto log_moves_count{cost_ - visible_model_moves_count_};
    return cost_ - (2 * std::min(log_moves_count, visible_model_moves_count_));
  }

 private:
  std::vector<alignment_move, legacy_embedded_ctl::resource_owning_allocator<alignment_move>> data_{};
  uint64_t cost_{0};
  uint64_t visible_model_moves_count_{0};
};

using trace_alignment_t = std::optional<trace_alignment>;

using trace_alignment_allocator_type = std::scoped_allocator_adaptor<legacy_embedded_ctl::resource_owning_allocator<trace_alignment_t>>;
using vector_of_alignments = std::vector<trace_alignment_t, trace_alignment_allocator_type>;

}  // namespace celonis::accelerator::operators::process::alignment
