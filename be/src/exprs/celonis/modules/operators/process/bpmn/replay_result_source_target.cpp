#include "replay_result_source_target.h"

#include <string_view>
#include <tuple>

#include "modules/common/exceptions.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

/** Used to validate that a replay result contains data with a consistent size */
void check_size_invariant(const size_t expected_size, const size_t actual_size, const std::string_view name) {
  if (expected_size != actual_size) {
    throw common::internal_exception{
        "[REPLAY_RESULT] Size invariant does not hold: Expected {} with size [{}] but got [{}].", name, expected_size,
        actual_size};
  }
}

}  // anonymous namespace

replay_result_source_target::replay_result_source_target(std::vector<cpml::model::bpmn::vertex_id_type> source_ids,
                                                         std::vector<cpml::model::bpmn::vertex_id_type> target_ids,
                                                         std::vector<row_id> source_ptrs,
                                                         std::vector<row_id> target_ptrs,
                                                         std::vector<row_id> join_index)
    : source_vertex_ids_{std::move(source_ids)},
      target_vertex_ids_{std::move(target_ids)},
      source_column_ptrs_{std::move(source_ptrs)},
      target_column_ptrs_{std::move(target_ptrs)},
      case_table_join_index_{std::move(join_index)} {
  const auto expected_size{source_vertex_ids().size()};
  check_size_invariant(expected_size, target_vertex_ids_.size(), "target vertex IDs");
  check_size_invariant(expected_size, source_column_ptrs_.size(), "source column pointers");
  check_size_invariant(expected_size, target_column_ptrs_.size(), "target column pointers");
  check_size_invariant(expected_size, case_table_join_index_.size(), "case table join index");
}

// TODO(bluppes): find a better way of recording the trace results instead of adding them
replay_result_source_target& replay_result_source_target::operator+=(const replay_result_source_target& rhs) {
  source_vertex_ids_.insert(source_vertex_ids_.end(), rhs.source_vertex_ids_.begin(), rhs.source_vertex_ids_.end());
  target_vertex_ids_.insert(target_vertex_ids_.end(), rhs.target_vertex_ids_.begin(), rhs.target_vertex_ids_.end());
  source_column_ptrs_.insert(source_column_ptrs_.end(), rhs.source_column_ptrs_.begin(), rhs.source_column_ptrs_.end());
  target_column_ptrs_.insert(target_column_ptrs_.end(), rhs.target_column_ptrs_.begin(), rhs.target_column_ptrs_.end());
  case_table_join_index_.insert(case_table_join_index_.end(), rhs.case_table_join_index_.begin(),
                                rhs.case_table_join_index_.end());

  return *this;
}

void replay_result_source_target::clear() {
  source_vertex_ids_.clear();
  target_vertex_ids_.clear();
  source_column_ptrs_.clear();
  target_column_ptrs_.clear();
  case_table_join_index_.clear();
}

bool replay_result_source_target::operator==(const replay_result_source_target& rhs) const {
  const auto lhs_tuple{std::tie(source_vertex_ids_, target_vertex_ids_, source_column_ptrs_, target_column_ptrs_,
                                case_table_join_index_)};
  const auto rhs_tuple{std::tie(rhs.source_vertex_ids_, rhs.target_vertex_ids_, rhs.source_column_ptrs_,
                                rhs.target_column_ptrs_, rhs.case_table_join_index_)};
  return lhs_tuple == rhs_tuple;
}

size_t replay_result_source_target::size() const noexcept {
  // size invariant (all container must have the same size) is ensured at construction time
  return source_vertex_ids_.size();
}

}  // namespace celonis::accelerator::operators::process::bpmn