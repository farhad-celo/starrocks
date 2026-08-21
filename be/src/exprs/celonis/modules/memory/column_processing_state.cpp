#include "column_processing_state.h"

#include "modules/common/exceptions.h"

namespace celonis::accelerator::memory {

column_processing_state::column_processing_state(std::string format, bool is_calculated_constant, bool is_filtered,
                                                 bool is_sorted, bool is_selectable, bool is_aggregation_result,
                                                 bool is_constant)
    : format_{std::move(format)},
      is_calculated_constant_{is_calculated_constant},
      is_filter_unstable_{is_filtered},
      is_sorted_{is_sorted},
      is_selectable_{is_selectable},
      is_aggregation_result_{is_aggregation_result},
      is_constant_{is_constant} {
  if (is_constant_ && is_selectable_) {
    throw common::internal_exception{"The state of a column cannot be both constant and selectable."};
  }
}

column_processing_state column_processing_state::create_selectable() {
  return column_processing_state{};  // default is selectable
}

column_processing_state column_processing_state::create_filter_unstable() {
  column_processing_state state{};
  state.set_filter_unstable(true);
  state.set_selectable_and_constant(false, false);
  return state;
}

column_processing_state column_processing_state::create_constant() {
  column_processing_state state{};
  state.set_selectable_and_constant(false, true);
  return state;
}

column_processing_state column_processing_state::create_aggregation_result() {
  column_processing_state state{};
  state.set_aggregation_result(true);
  state.set_selectable(false);
  state.set_filter_unstable(true);
  return state;
}

void column_processing_state::make_filter_unstable() {
  set_filter_unstable(true);
  set_selectable_and_constant(false, false);
}

void column_processing_state::make_aggregation_result() {
  set_aggregation_result(true);
  set_selectable(false);
  set_filter_unstable(true);
}

void column_processing_state::make_non_selectable() {
  set_selectable_and_constant(false, !is_filter_unstable_ ? true : is_constant_);
}

void column_processing_state::make_bounded_constant_selectable() {
  if (!is_selectable_ && is_constant_) {
    set_selectable_and_constant(true, false);
  }
}

void column_processing_state::set_selectable_and_constant(bool selectable, bool constant) {
  if ((is_constant_ && is_selectable_) || (constant && selectable)) {
    throw common::internal_exception{"The state of a column cannot be both constant and selectable."};
  }
  is_selectable_ = selectable;
  is_constant_ = constant;
}

void column_processing_state::merge_sorted_calculated_const(const column_processing_state& state) {
  if (((is_calculated_constant_ || is_constant_) && !is_sorted_) ||
      ((state.is_calculated_constant_ || state.is_constant_) && !state.is_sorted_)) {
    // if one side is a non-sorted constant, the sorted state of the result can be set to the sorted state of the
    // other side.
    is_sorted_ = is_sorted_ || state.is_sorted_;
  } else if (is_sorted_ != state.is_sorted_) {
    throw common::internal_exception{"Sorted state of columns does not match."};
  }
}

void column_processing_state::merge(const column_processing_state& state) {
  merge_sorted_calculated_const(state);

  set_format(state.format_);
  set_calculated_constant(is_calculated_constant_ && state.is_calculated_constant_);
  set_filter_unstable(is_filter_unstable_ || state.is_filter_unstable_);
  set_aggregation_result(is_aggregation_result_ || state.is_aggregation_result_);

  // The order of the next two assignments is important
  set_selectable_and_constant((is_selectable_ && state.is_selectable_) || (is_selectable_ && state.is_constant_) ||
                                  (is_constant_ && state.is_selectable_),
                              is_constant_ && state.is_constant_);
}
}  // namespace celonis::accelerator::memory