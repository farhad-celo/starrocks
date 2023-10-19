#pragma once

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/builders/result_column_builder_fwd.h"
#include "modules/memory/cache/column_register_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/table_fwd.h"

namespace celonis::accelerator::memory::builders {

/**
 * Defines the functions for building a column that is either added to a table cache
 * or that is used only temporarily
 */
class result_column_builder {
 public:
  virtual ~result_column_builder() = default;

  /**
   * Builds a column and adds it to a table cache.
   *
   * @param column_register cache column is created by this column_register
   * @param processing_state state of the column
   * @return cached column
   */
  virtual memory::column_t build_cache_column(const memory::cache::column_register& column_register,
                                              const memory::column_processing_state& processing_state,
                                              table_row_limit_t table_row_limit,
                                              const common::execution_context& context) = 0;

  /**
   * Builds a temporary column which is not added to any cache.
   *
   * @param owner optional parent table
   * @param cache_key cache key
   * @param processing_state state of the column
   * @return temporary column
   */
  virtual memory::column_t build_temp_column(memory::table* owner, const col_cache_key& cache_key,
                                             const memory::column_processing_state& processing_state) = 0;
};

}  // namespace celonis::accelerator::memory::builders
