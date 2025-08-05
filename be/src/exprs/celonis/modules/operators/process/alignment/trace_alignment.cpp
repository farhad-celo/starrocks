#include "modules/operators/process/alignment/trace_alignment.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <tuple>
#include <vector>

#include "legacy_embedded_ctl/conversion.h"
#include "modules/common/exceptions.h"
#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/alignment/input_output_mapper.h"
#include "modules/operators/process/alignment/sequence_aligner.h"

namespace celonis::accelerator::operators::process::alignment {

namespace {

uint64_t compute_cost(const trace_alignment& alignment) {
  const auto sync_or_invisible_count{std::ranges::count_if(
      alignment.data(), [](const alignment_move& move) { return move.is_sync() || move.is_tau(); })};
  return alignment.size() - sync_or_invisible_count;
}

uint64_t count_model_moves(const trace_alignment& alignment) {
  return std::ranges::count_if(alignment.data(),
                               [](const alignment_move& move) { return move.is_model() && !move.is_tau(); });
}

}  // namespace

void trace_alignment::add(alignment_move move) {
  if (!move.is_sync() && !move.is_tau()) {
    ++cost_;
  }
  if (move.is_model() && !move.is_tau()) {
    ++visible_model_moves_count_;
  }
  data_.push_back(move);
}

void trace_alignment::prune_simple() {
  if (data_.size() <= 1) {
    return;
  }

  auto write_out_it{std::begin(data_)};
  auto curr_move_it{std::begin(data_)};

  while (curr_move_it != std::cend(data_)) {
    if (const auto next_move_it{std::next(curr_move_it)}; next_move_it != std::cend(data_)) {
      if (curr_move_it->label() == next_move_it->label()) {
        if ((curr_move_it->is_log() && next_move_it->is_model()) ||
            (curr_move_it->is_model() && next_move_it->is_log())) {
          const auto curr_move_on_model{curr_move_it->move_on_model()};
          const auto next_move_on_model{next_move_it->move_on_model()};
          auto transition_id{curr_move_on_model ? curr_move_on_model.value() : next_move_on_model.value()};
          *next_move_it = alignment_move::sync(curr_move_it->label(), transition_id);
          curr_move_it = next_move_it;
          cost_ -= 2;
          visible_model_moves_count_ -= 1;
        } else if (curr_move_it->is_log() && next_move_it->is_sync()) {
          std::swap(*curr_move_it, *next_move_it);
        } else if (curr_move_it->is_model() && next_move_it->is_sync()) {
          // We can only swap sync and model moves if the transition IDs are equal
          if (curr_move_it->move_on_model() == next_move_it->move_on_model()) {
            std::swap(*curr_move_it, *next_move_it);
          }
        }
      }
    }
    *write_out_it = *curr_move_it;
    ++write_out_it;
    ++curr_move_it;
  }
  legacy_embedded_debug_assert(write_out_it <= std::end(data_));
  data_.erase(write_out_it, std::end(data_));
}

void trace_alignment::prune_lcs(size_t chunk_size, const common::execution_context& context) {
  if (data_.size() <= 1) {
    return;
  }

  constexpr auto is_move_on_model{[](const alignment_move& move) { return !(move.is_log() || move.is_unmapped()); }};
  if (std::ranges::count_if(data_, is_move_on_model) <= legacy_embedded_ctl::cast<int64_t>(chunk_size)) {
    // If the run on the model is smaller than chunk_size, then we set chunk size to the size of the trace
    // This way we still ensure that the total runtime <= chunk_size * size_of_trace but we get better alignments
    chunk_size = data_.size();
  }

  std::vector<sequence_aligner::visible_label_type> trace_chunk;
  sequence_aligner::sequence_type model_run_chunk{};

  // TODO (goulart.e) ranges::chunk in C++23
  using alignment_span_type = std::span<const alignment_move>;
  alignment_span_type alignment_span{data_};
  auto output_it{std::begin(data_)};

  for (size_t chunk_offset{0}; chunk_offset < alignment_span.size(); chunk_offset += chunk_size) {
    trace_chunk.clear();
    model_run_chunk.clear();

    const auto chunk_end_offset{std::min(chunk_offset + chunk_size, alignment_span.size())};
    const auto chunk_count{chunk_end_offset - chunk_offset};
    const auto chunk_subspan{alignment_span.subspan(chunk_offset, chunk_count)};
    for (const auto& move : chunk_subspan) {
      switch (move.type()) {
        case alignment::alignment_move_type::SYNC:
          trace_chunk.push_back(move.label());
          model_run_chunk.emplace_back(move.move_on_model().value(), move.label());
          break;
        case alignment::alignment_move_type::MODEL:
          model_run_chunk.emplace_back(move.move_on_model().value(), move.label());
          break;
        case alignment::alignment_move_type::LOG:
          trace_chunk.push_back(move.label());
          break;
        case alignment::alignment_move_type::UNMAPPED:
          [[fallthrough]];
        default:
          // Both should not happen
          legacy_embedded_ctl::assert_unreachable();
      }
    }

    const int32_t max_iterations{legacy_embedded_ctl::cast<int32_t>(chunk_size * chunk_size)};
    const auto lcs_alignment{
        sequence_aligner::align_sequence_to_run(trace_chunk, model_run_chunk, max_iterations, context)};
    // If it didn't time out, then it has a smaller or equal cost than the sub-span and we use that,
    //  else we assign the sub-span. Notice that we assign a high-enough MAX_ITERATIONS so that it should always succeed
    const auto span_to_copy{lcs_alignment ? alignment_span_type{lcs_alignment.value().data()} : chunk_subspan};
    legacy_embedded_debug_assert(span_to_copy.size() <= chunk_size);
    if (span_to_copy.data() != std::to_address(output_it)) {
      std::ranges::copy(span_to_copy, output_it);
    }
    output_it += legacy_embedded_ctl::cast<int64_t>(span_to_copy.size());
  }

  legacy_embedded_debug_assert(output_it <= std::end(data_));
  data_.erase(output_it, std::end(data_));
  cost_ = compute_cost(*this);
  visible_model_moves_count_ = count_model_moves(*this);
}

}  // namespace celonis::accelerator::operators::process::alignment
