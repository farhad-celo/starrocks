#pragma once

#include <string>

namespace celonis::accelerator::memory {
class column_processing_state {
 public:
  column_processing_state() = default;
  column_processing_state(std::string format, bool is_calculated_constant, bool is_filtered, bool is_sorted,
                          bool is_selectable, bool is_aggregation_result, bool is_constant);

  [[nodiscard]] const std::string& get_format() const noexcept { return format_; }

  [[nodiscard]] bool is_temporary() const noexcept { return is_filter_unstable_ || is_aggregation_result_; }
  [[nodiscard]] bool is_calculated_constant() const noexcept { return is_calculated_constant_; }
  [[nodiscard]] bool is_filter_unstable() const noexcept { return is_filter_unstable_; }
  [[nodiscard]] bool is_filter_stable() const noexcept { return !is_filter_unstable_; }
  [[nodiscard]] bool is_sorted() const noexcept { return is_sorted_; }
  [[nodiscard]] bool is_selectable() const noexcept { return is_selectable_; }
  [[nodiscard]] bool is_aggregation_result() const noexcept { return is_aggregation_result_; }
  [[nodiscard]] bool is_constant() const noexcept { return is_constant_; }

  static column_processing_state create_selectable();
  static column_processing_state create_filter_unstable();
  static column_processing_state create_constant();
  static column_processing_state create_aggregation_result();

  void make_filter_unstable();
  void make_aggregation_result();
  void make_non_selectable();
  void make_bounded_constant_selectable();
  void set_sorted(bool sorted) noexcept { is_sorted_ = sorted; }
  void set_calculated_constant(bool calculated_constant) noexcept { is_calculated_constant_ = calculated_constant; }
  void set_format(const std::string& format) { format_ = format; }
  void set_filter_unstable(bool filtered) noexcept { is_filter_unstable_ = filtered; }

  void merge(const column_processing_state& state);

 private:
  void merge_sorted_calculated_const(const column_processing_state& state);

  void set_selectable(bool selectable) noexcept { is_selectable_ = selectable; }
  void set_aggregation_result(bool aggregation_result) noexcept { is_aggregation_result_ = aggregation_result; }
  void set_selectable_and_constant(bool selectable, bool constant);

  std::string format_{};
  bool is_calculated_constant_{false};
  bool is_filter_unstable_{false};  // false means filter stable, true means filter unstable
  bool is_sorted_{false};
  bool is_selectable_{true};  // Used to determine if cache_key should be cleared in the returned metadata
  bool is_aggregation_result_{false};
  bool is_constant_{false};
};
}  // namespace celonis::accelerator::memory
