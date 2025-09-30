#pragma once

#include <ostream>
#include <vector>

#include <cpml/model/bpmn/vertex_types.h>

#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/bpmn/replay_result_source_target_fwd.h"  // IWYU pragma: export

namespace celonis::accelerator::operators::process::bpmn {

// TODO(n.weber): Revisit (get rid of unused default ctor, instead of having a ctor with filled data provide interface
// to fill, rule of zero-five)
class replay_result_source_target {
 public:
  replay_result_source_target() noexcept = default;

  replay_result_source_target(std::vector<cpml::model::bpmn::vertex_id_type> source_ids,
                              std::vector<cpml::model::bpmn::vertex_id_type> target_ids,
                              std::vector<row_id> source_ptrs, std::vector<row_id> target_ptrs,
                              std::vector<row_id> join_index);

  /*
   * Concatenates the vectors and increases the size accordingly.
   */
  replay_result_source_target& operator+=(const replay_result_source_target& rhs);

  /*
   * Clears the vectors and sets the size of the result to 0.
   */
  void clear();

  [[nodiscard]] bool operator==(const replay_result_source_target& rhs) const;

  [[nodiscard]] size_t size() const noexcept;

  [[nodiscard]] const std::vector<cpml::model::bpmn::vertex_id_type>& source_vertex_ids() const noexcept {
    return source_vertex_ids_;
  }
  [[nodiscard]] const std::vector<cpml::model::bpmn::vertex_id_type>& target_vertex_ids() const noexcept {
    return target_vertex_ids_;
  }
  [[nodiscard]] const std::vector<row_id>& source_column_ptrs() const noexcept { return source_column_ptrs_; }
  [[nodiscard]] const std::vector<row_id>& target_column_ptrs() const noexcept { return target_column_ptrs_; }
  [[nodiscard]] const std::vector<row_id>& case_table_join_index() const noexcept { return case_table_join_index_; }

 private:
  // Each combination of source and target vertex ID defines a passed edge in the BPMN model.
  std::vector<cpml::model::bpmn::vertex_id_type> source_vertex_ids_{};
  std::vector<cpml::model::bpmn::vertex_id_type> target_vertex_ids_{};

  // Column pointers to the passed input_column, possibly containing "virtual" KPIs.
  std::vector<row_id> source_column_ptrs_{};
  std::vector<row_id> target_column_ptrs_{};

  std::vector<row_id> case_table_join_index_{};  // index for joining the replay results to the case table
};

}  // namespace celonis::accelerator::operators::process::bpmn
