#include "align_model_table_config.h"

#include <utility>

namespace celonis::accelerator::cube {

align_model_table_config::align_model_table_config(memory::table_t result_join_table)
    : result_join_table_{std::move(result_join_table)} {}

}  // namespace celonis::accelerator::cube
