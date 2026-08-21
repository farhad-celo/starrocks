#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common/statusor.h"

namespace celonis::accelerator::operators::process::align_model {

enum class edge_type : std::uint8_t;

}

namespace celonis::accelerator::operators::process::align_model::v2 {

enum class alignment_output_column : std::uint8_t {
  MODEL_VERTEX_ID,
  VERTEX_LABEL,
  MOVE_TYPE,
  ACTIVITY_INDEX,
  DEVIATION_CATEGORY,
  FIELD_COUNT,
};

enum class association_output_column : std::uint8_t {
  MODEL_VERTEX_ID,
  VERTEX_LABEL,
  MOVE_TYPE,
  DEVIATION_CATEGORY,
  EDGE_CLASS,
  ALIGNMENT_INDEX,
  FIELD_COUNT,
};

enum class create_alignment_output_value_type : std::uint8_t {
  OPTIONAL_MODEL_VERTEX_ID_ARRAY,
  STRING_ARRAY,
  ROW_ID_ARRAY,
};

struct create_alignment_output_field {
  std::string_view name;
  create_alignment_output_value_type value_type;
};

class create_alignment_output_projection {
 public:
  static constexpr size_t FIELD_COUNT{47};
  static constexpr std::uint64_t ALL_FIELDS_MASK{(std::uint64_t{1} << FIELD_COUNT) - 1};

  [[nodiscard]] static starrocks::StatusOr<create_alignment_output_projection> from_mask_and_return_fields(
      std::int64_t mask, const std::vector<std::string>& return_fields);

  // Existing full-table callers require the internal variant column in addition to all public fields.
  [[nodiscard]] static create_alignment_output_projection all_fields();

  [[nodiscard]] bool contains(alignment_output_column column) const;
  [[nodiscard]] bool contains(edge_type type, association_output_column column) const;
  [[nodiscard]] bool any_alignment_field() const;
  [[nodiscard]] bool any_association_field() const;
  [[nodiscard]] bool any_field_for_edge_type(edge_type type) const;
  [[nodiscard]] bool needs_vertex_labels() const;
  [[nodiscard]] bool needs_deviation_categories() const;
  [[nodiscard]] bool includes_internal_variant() const { return include_internal_variant_; }
  [[nodiscard]] std::uint64_t mask() const { return mask_; }
  [[nodiscard]] size_t selected_field_count() const;

  [[nodiscard]] static const std::array<create_alignment_output_field, FIELD_COUNT>& fields();
  [[nodiscard]] static const create_alignment_output_field* find_field(std::string_view name);

 private:
  explicit create_alignment_output_projection(std::uint64_t mask, bool include_internal_variant)
      : mask_{mask}, include_internal_variant_{include_internal_variant} {}

  std::uint64_t mask_;
  bool include_internal_variant_;
};

}  // namespace celonis::accelerator::operators::process::align_model::v2
