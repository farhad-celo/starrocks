#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>

#include <ctl/algorithm.h>

#include "modules/operators/process/align_model/align_model_types.h"

namespace celonis::accelerator::operators::process::align_model {

class condensed_alignment_t {
 public:
  using mapping_t = boost::bimap<boost::bimaps::unordered_set_of<alignment_t::size_type>,
                                 boost::bimaps::unordered_set_of<alignment_t::size_type>>;
  condensed_alignment_t(alignment_t alignment, std::unique_ptr<mapping_t> full_to_condensed_idx_map)
      : alignment_{std::move(alignment)}, full_to_condensed_idx_map_{std::move(full_to_condensed_idx_map)} {}

  [[nodiscard]] alignment_view_t get_alignment() const { return alignment_; };
  [[nodiscard]] const mapping_t& get_full_to_condensed_idx_map() const { return *full_to_condensed_idx_map_; };

 private:
  alignment_t alignment_;
  std::unique_ptr<mapping_t> full_to_condensed_idx_map_;
};

using condensed_alignments_t = std::vector<std::optional<condensed_alignment_t>>;
using condensed_alignments_view_t = ctl::array_view<const condensed_alignments_t::value_type>;

template <typename PREDICATE>
requires(std::is_invocable_r_v<bool, PREDICATE, const alignment_move&>) condensed_alignments_t
    compute_filtered_alignments(alignments_view_t full_alignments, PREDICATE&& predicate) {
  return ctl::transform_to<condensed_alignments_t>(
      full_alignments, [&](const auto& maybe_alignment) mutable -> std::optional<condensed_alignment_t> {
        if (!maybe_alignment.has_value()) {
          return std::nullopt;
        }
        const alignment_t& alignment{maybe_alignment.value()};

        // TODO(j.kruska) Is this copy necessary or can we adjust the downstream functions to consume alignments in the
        // form of views This should also be benchmarked
        alignment_t result{};
        result.reserve(alignment.size());
        size_t condensed_idx{0};
        auto map{std::make_unique<condensed_alignment_t::mapping_t>()};
        for (size_t full_idx{0}; full_idx < alignment.size(); full_idx++) {
          const auto move{alignment.at(full_idx)};
          if (predicate(move)) {
            result.emplace_back(move);
            map->insert({full_idx, condensed_idx++});
          }
        }
        return condensed_alignment_t{std::move(result), std::move(map)};
      });
}
}  // namespace celonis::accelerator::operators::process::align_model