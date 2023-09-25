#pragma once

#include "modules/memory/table_fwd.h"

namespace celonis::accelerator::cube {

struct align_model_table_config {
 public:
  explicit align_model_table_config(memory::table_t result_join_table);
  [[nodiscard]] memory::table_t get_result_join_table() const { return result_join_table_; }

 private:
  memory::table_t result_join_table_{nullptr};
};

}  // namespace celonis::accelerator::cube
