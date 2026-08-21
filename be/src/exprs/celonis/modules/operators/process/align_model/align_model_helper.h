#pragma once

#include <optional>
#include <vector>

#include "common/status.h"
#include "common/statusor.h"
#include "exprs/celonis/modules/operators/process/align_model/v2/create_alignment_output_projection.h"
#include "exprs/celonis/result_table.h"
#include "exprs/celonis/utils/proto_utils.h"

using starrocks::Status;
using starrocks::celonis::ResultTable;

namespace celonis::accelerator::operators::process::align_model {

class AlignModelHelper {
 public:
  using activity_name_t = std::string;
  using trace_t = std::vector<std::optional<activity_name_t>>;
  using traces_t = std::vector<trace_t>;

  enum class celostar_align_model_version { V1, V2 };

  AlignModelHelper() = default;

  static starrocks::StatusOr<starrocks::celonis::bpmn_model_description> parse_bpmn_model_description(
      const std::string& bpmn_model_description_json);

  Status execute(const traces_t& traces, const std::string& bpmn_model_description_json,
                 celostar_align_model_version version);
  Status execute(const traces_t& traces, const starrocks::celonis::bpmn_model_description& bpmn_model_description,
                 celostar_align_model_version version);
  Status execute(const traces_t& traces, const starrocks::celonis::bpmn_model_description& bpmn_model_description,
                 celostar_align_model_version version, const v2::create_alignment_output_projection& output_projection);

  const ResultTable& result_table() { return *result_table_; }

 private:
  std::unique_ptr<ResultTable> result_table_;
};

}  // namespace celonis::accelerator::operators::process::align_model
