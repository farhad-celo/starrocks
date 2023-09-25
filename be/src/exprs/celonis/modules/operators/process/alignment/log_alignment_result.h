#pragma once

#include <memory>
#include <string>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/builders/result_column_builder_fwd.h"
#include "modules/memory/cache/cache_entry.h"
#include "modules/memory/cache/column_register.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/row_id.h"
#include "modules/memory/table_fwd.h"
#include "modules/operators/process/alignment/input_output_mapper.h"
#include "modules/operators/process/alignment/log_alignment_result_fwd.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment {

/** Holds per-variant alignments. Only computes alignment table columns if needed. */
class log_alignment_result {
 public:
  log_alignment_result(vector_of_alignments alignments, string_to_int_mapper str_mapper,
                       memory::join_projection_vector_t projection_vector)
      : alignments_{std::move(alignments)},
        str_mapper_{std::move(str_mapper)},
        projection_vector_{std::move(projection_vector)} {}

  [[nodiscard]] memory::builders::result_column_builder_t align_activity(row_id output_table_size,
                                                                         row_id activity_table_size,
                                                                         const common::execution_context& context,
                                                                         const memory::column_t& variant_column) const;

  [[nodiscard]] memory::builders::result_column_builder_t align_move(row_id output_table_size,
                                                                     row_id activity_table_size,
                                                                     const common::execution_context& context,
                                                                     const memory::column_t& variant_column) const;

  [[nodiscard]] const auto& get_alignments() const { return alignments_; }

 private:
  /// Alignments and maps to retrieve string id
  vector_of_alignments alignments_{};
  string_to_int_mapper str_mapper_{};
  memory::join_projection_vector_t projection_vector_{};
};

}  // namespace celonis::accelerator::operators::process::alignment
