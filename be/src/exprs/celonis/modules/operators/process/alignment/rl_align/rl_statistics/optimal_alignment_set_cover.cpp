#include "optimal_alignment_set_cover.h"

namespace celonis::accelerator::operators::process::alignment::rl_align::rl_statistics {

void optimal_alignment_set_cover::finish_current_run() {
  // only keep the constraints with the best performance
  const auto min_cost_iterator{
      std::min_element(begin(current_run_), end(current_run_),
                       [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; })};
  if (min_cost_iterator == end(current_run_)) {
    return;
  }
  const auto min_cost{min_cost_iterator->second};
  for (const auto& [constraint, cost] : current_run_) {
    if (cost == min_cost) {
      statistics_[constraint].emplace(current_run_index_);
    }
  }
  current_run_.clear();
  ++current_run_index_;
}

constraints_config_set optimal_alignment_set_cover::get_approximate_cover() const {
  const auto less_size{[](const auto& lhs, const auto& rhs) { return lhs.second.size() < rhs.second.size(); }};
  std::vector<constraints_config> chosen_constraints{};
  auto statistics_copy{statistics_};

  for (auto it{std::max_element(begin(statistics_copy), end(statistics_copy), less_size)};
       it != end(statistics_copy) && !it->second.empty();
       it = std::max_element(begin(statistics_copy), end(statistics_copy), less_size)) {
    const auto max_set{std::move(it->second)};
    chosen_constraints.emplace_back(it->first);
    statistics_copy.erase(it);

    for (auto rit{begin(statistics_copy)}; rit != end(statistics_copy); /*update in body*/) {
      std::set<size_t> replacement{};
      std::set_difference(begin(rit->second), end(rit->second), begin(max_set), end(max_set),
                          std::inserter(replacement, end(replacement)));
      if (replacement.empty()) {
        rit = statistics_copy.erase(rit);
      } else {
        rit->second = std::move(replacement);
        ++rit;
      }
    }
  }
  return {chosen_constraints};
}

void optimal_alignment_set_cover::update(const optimal_alignment_set_cover& other) {
  for (const auto& [constraint, run_indices] : other.statistics_) {
    if (run_indices.empty()) {
      continue;
    }
    auto& hitting_set{statistics_[constraint]};
    for (const auto run_index : run_indices) {
      hitting_set.emplace(run_index + current_run_index_);
    }
  }
  current_run_index_ += other.current_run_index_;
}

}  // namespace celonis::accelerator::operators::process::alignment::rl_align::rl_statistics
