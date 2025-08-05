#include "top_constraints.h"

#include "legacy_embedded_ctl/conversion.h"

namespace celonis::accelerator::operators::process::alignment::rl_align::rl_statistics {

void top_constraints::finish_current_run() {
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
      ++statistics_[constraint];
    }
  }
  current_run_.clear();
}

constraints_config_set top_constraints::get_top(size_t n) const {
  std::vector<constraints_config> constraints{};
  constraints.reserve(statistics_.size());
  for (const auto& [constraint, _] : statistics_) {
    constraints.emplace_back(constraint);
  }
  if (n >= constraints.size()) {
    return {constraints};
  }
  const auto sorted_end{std::next(begin(constraints), legacy_embedded_ctl::cast<decltype(constraints)::difference_type>(n))};
  std::partial_sort(begin(constraints), sorted_end, end(constraints),
                    [this](const auto& lhs, const auto& rhs) { return statistics_.at(lhs) > statistics_.at(rhs); });
  constraints.erase(sorted_end, end(constraints));
  return {constraints};
}

constraints_config_set top_constraints::get_all_best() const {
  const auto max_count_it{
      std::ranges::max_element(statistics_, [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; })};

  if (max_count_it == end(statistics_)) {
    return {};
  }

  const auto max_count = max_count_it->second;

  std::vector<constraints_config> result{};
  for (const auto& [constraint, count] : statistics_) {
    if (count == max_count) {
      result.emplace_back(constraint);
    }
  }
  return {result};
}

void top_constraints::update(const top_constraints& other) {
  for (const auto& [key, val] : other.statistics_) {
    statistics_[key] += val;
  }
}

}  // namespace celonis::accelerator::operators::process::alignment::rl_align::rl_statistics
