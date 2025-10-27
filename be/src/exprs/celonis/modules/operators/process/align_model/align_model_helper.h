#pragma once

#include <optional>
#include <vector>

#include "common/status.h"
#include "exprs/celonis/result_table.h"

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

  Status execute(const traces_t& traces, const std::string& bpmn_model_description_json,
                 celostar_align_model_version version);

  const ResultTable& result_table() { return *result_table_; }

 private:
  std::unique_ptr<ResultTable> result_table_;
};

}  // namespace celonis::accelerator::operators::process::align_model
