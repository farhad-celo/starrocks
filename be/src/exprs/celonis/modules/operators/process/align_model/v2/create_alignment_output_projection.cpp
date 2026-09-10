#include "create_alignment_output_projection.h"

#include <algorithm>
#include <bit>

#include <fmt/format.h>

#include "common/status.h"
#include "modules/operators/process/align_model/align_model_types.h"

namespace celonis::accelerator::operators::process::align_model::v2 {
namespace {

using value_type = create_alignment_output_value_type;

constexpr auto OUTPUT_FIELDS{std::to_array<create_alignment_output_field>({
    {"alignment_model_vertex_id", value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY},
    {"alignment_vertex_label", value_type::STRING_ARRAY},
    {"alignment_move_type", value_type::STRING_ARRAY},
    {"alignment_activity_index", value_type::ROW_ID_ARRAY},
    {"alignment_deviation_category", value_type::STRING_ARRAY},
    {"SYNC_EDGE_model_vertex_id", value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY},
    {"SYNC_EDGE_vertex_label", value_type::STRING_ARRAY},
    {"SYNC_EDGE_move_type", value_type::STRING_ARRAY},
    {"SYNC_EDGE_deviation_category", value_type::STRING_ARRAY},
    {"SYNC_EDGE_edge_class", value_type::ROW_ID_ARRAY},
    {"SYNC_EDGE_alignment_index", value_type::ROW_ID_ARRAY},
    {"MODEL_EDGE_model_vertex_id", value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY},
    {"MODEL_EDGE_vertex_label", value_type::STRING_ARRAY},
    {"MODEL_EDGE_move_type", value_type::STRING_ARRAY},
    {"MODEL_EDGE_deviation_category", value_type::STRING_ARRAY},
    {"MODEL_EDGE_edge_class", value_type::ROW_ID_ARRAY},
    {"MODEL_EDGE_alignment_index", value_type::ROW_ID_ARRAY},
    {"SKIP_EDGE_model_vertex_id", value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY},
    {"SKIP_EDGE_vertex_label", value_type::STRING_ARRAY},
    {"SKIP_EDGE_move_type", value_type::STRING_ARRAY},
    {"SKIP_EDGE_deviation_category", value_type::STRING_ARRAY},
    {"SKIP_EDGE_edge_class", value_type::ROW_ID_ARRAY},
    {"SKIP_EDGE_alignment_index", value_type::ROW_ID_ARRAY},
    {"LOG_EDGE_model_vertex_id", value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY},
    {"LOG_EDGE_vertex_label", value_type::STRING_ARRAY},
    {"LOG_EDGE_move_type", value_type::STRING_ARRAY},
    {"LOG_EDGE_deviation_category", value_type::STRING_ARRAY},
    {"LOG_EDGE_edge_class", value_type::ROW_ID_ARRAY},
    {"LOG_EDGE_alignment_index", value_type::ROW_ID_ARRAY},
    {"UNMAPPED_EDGE_model_vertex_id", value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY},
    {"UNMAPPED_EDGE_vertex_label", value_type::STRING_ARRAY},
    {"UNMAPPED_EDGE_move_type", value_type::STRING_ARRAY},
    {"UNMAPPED_EDGE_deviation_category", value_type::STRING_ARRAY},
    {"UNMAPPED_EDGE_edge_class", value_type::ROW_ID_ARRAY},
    {"UNMAPPED_EDGE_alignment_index", value_type::ROW_ID_ARRAY},
    {"MISSING_VIOLATION_model_vertex_id", value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY},
    {"MISSING_VIOLATION_vertex_label", value_type::STRING_ARRAY},
    {"MISSING_VIOLATION_move_type", value_type::STRING_ARRAY},
    {"MISSING_VIOLATION_deviation_category", value_type::STRING_ARRAY},
    {"MISSING_VIOLATION_edge_class", value_type::ROW_ID_ARRAY},
    {"MISSING_VIOLATION_alignment_index", value_type::ROW_ID_ARRAY},
    {"EXCLUSIVE_VIOLATION_model_vertex_id", value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY},
    {"EXCLUSIVE_VIOLATION_vertex_label", value_type::STRING_ARRAY},
    {"EXCLUSIVE_VIOLATION_move_type", value_type::STRING_ARRAY},
    {"EXCLUSIVE_VIOLATION_deviation_category", value_type::STRING_ARRAY},
    {"EXCLUSIVE_VIOLATION_edge_class", value_type::ROW_ID_ARRAY},
    {"EXCLUSIVE_VIOLATION_alignment_index", value_type::ROW_ID_ARRAY},
    {"INCOMPLETE_VIOLATION_model_vertex_id", value_type::OPTIONAL_MODEL_VERTEX_ID_ARRAY},
    {"INCOMPLETE_VIOLATION_vertex_label", value_type::STRING_ARRAY},
    {"INCOMPLETE_VIOLATION_move_type", value_type::STRING_ARRAY},
    {"INCOMPLETE_VIOLATION_deviation_category", value_type::STRING_ARRAY},
    {"INCOMPLETE_VIOLATION_edge_class", value_type::ROW_ID_ARRAY},
    {"INCOMPLETE_VIOLATION_alignment_index", value_type::ROW_ID_ARRAY},
})};

constexpr size_t ALIGNMENT_FIELD_COUNT{static_cast<size_t>(alignment_output_column::FIELD_COUNT)};
constexpr size_t ASSOCIATION_FIELD_COUNT{static_cast<size_t>(association_output_column::FIELD_COUNT)};
static_assert(ALIGNMENT_FIELD_COUNT + EDGE_TYPES.size() * ASSOCIATION_FIELD_COUNT ==
              create_alignment_output_projection::FIELD_COUNT);
static_assert(OUTPUT_FIELDS.size() == create_alignment_output_projection::FIELD_COUNT);

[[nodiscard]] constexpr size_t edge_index(edge_type type) { return static_cast<size_t>(type); }

}  // namespace

starrocks::StatusOr<create_alignment_output_projection> create_alignment_output_projection::from_mask_and_return_fields(
    std::int64_t mask, const std::vector<std::string>& return_fields) {
  if (mask <= 0) {
    return starrocks::Status::InvalidArgument(
        fmt::format("celonis_create_alignment_v2: required_fields_mask must be positive, but received {}.", mask));
  }

  const auto unsigned_mask{static_cast<std::uint64_t>(mask)};
  if ((unsigned_mask & ~ALL_FIELDS_MASK) != 0) {
    return starrocks::Status::InvalidArgument(fmt::format(
        "celonis_create_alignment_v2: required_fields_mask {} contains unknown bits; allowed range is 1 through {}.",
        mask, ALL_FIELDS_MASK));
  }

  if (return_fields.size() != std::popcount(unsigned_mask)) {
    return starrocks::Status::InternalError(
        fmt::format("celonis_create_alignment_v2: FE return schema has {} fields, but required_fields_mask {} selects "
                    "{} fields.",
                    return_fields.size(), mask, std::popcount(unsigned_mask)));
  }

  size_t return_field_index{0};
  for (size_t bit{0}; bit < OUTPUT_FIELDS.size(); ++bit) {
    if ((unsigned_mask & (std::uint64_t{1} << bit)) == 0) {
      continue;
    }
    if (return_fields[return_field_index] != OUTPUT_FIELDS[bit].name) {
      return starrocks::Status::InternalError(fmt::format(
          "celonis_create_alignment_v2: FE return field {} is '{}', but mask {} requires '{}' in canonical order.",
          return_field_index, return_fields[return_field_index], mask, OUTPUT_FIELDS[bit].name));
    }
    ++return_field_index;
  }

  return create_alignment_output_projection{unsigned_mask, false};
}

create_alignment_output_projection create_alignment_output_projection::all_fields_v1() {
  return create_alignment_output_projection{ALL_FIELDS_V1_MASK, true};
}

create_alignment_output_projection create_alignment_output_projection::all_fields() {
  return create_alignment_output_projection{ALL_FIELDS_MASK, true};
}
bool create_alignment_output_projection::contains(alignment_output_column column) const {
  const auto bit{static_cast<size_t>(column)};
  return (mask_ & (std::uint64_t{1} << bit)) != 0;
}

bool create_alignment_output_projection::contains(edge_type type, association_output_column column) const {
  const auto bit{ALIGNMENT_FIELD_COUNT + edge_index(type) * ASSOCIATION_FIELD_COUNT + static_cast<size_t>(column)};
  return (mask_ & (std::uint64_t{1} << bit)) != 0;
}

bool create_alignment_output_projection::any_alignment_field() const {
  constexpr std::uint64_t alignment_fields_mask{(std::uint64_t{1} << ALIGNMENT_FIELD_COUNT) - 1};
  return (mask_ & alignment_fields_mask) != 0;
}

bool create_alignment_output_projection::any_association_field() const {
  constexpr std::uint64_t alignment_fields_mask{(std::uint64_t{1} << ALIGNMENT_FIELD_COUNT) - 1};
  return (mask_ & ~alignment_fields_mask) != 0;
}

bool create_alignment_output_projection::any_field_for_edge_type(edge_type type) const {
  const auto first_bit{ALIGNMENT_FIELD_COUNT + edge_index(type) * ASSOCIATION_FIELD_COUNT};
  constexpr std::uint64_t association_fields_mask{(std::uint64_t{1} << ASSOCIATION_FIELD_COUNT) - 1};
  return (mask_ & (association_fields_mask << first_bit)) != 0;
}

bool create_alignment_output_projection::needs_vertex_labels() const {
  if (contains(alignment_output_column::VERTEX_LABEL)) {
    return true;
  }
  return std::ranges::any_of(
      EDGE_TYPES, [this](edge_type type) { return contains(type, association_output_column::VERTEX_LABEL); });
}

bool create_alignment_output_projection::needs_deviation_categories() const {
  if (contains(alignment_output_column::DEVIATION_CATEGORY) ||
      any_field_for_edge_type(edge_type::L1_INCOMPLETE_VIOLATION)) {
    return true;
  }
  return std::ranges::any_of(
      EDGE_TYPES, [this](edge_type type) { return contains(type, association_output_column::DEVIATION_CATEGORY); });
}

size_t create_alignment_output_projection::selected_field_count() const { return std::popcount(mask_); }

const std::array<create_alignment_output_field, create_alignment_output_projection::FIELD_COUNT>&
create_alignment_output_projection::fields() {
  return OUTPUT_FIELDS;
}

const create_alignment_output_field* create_alignment_output_projection::find_field(std::string_view name) {
  const auto iter{std::ranges::find(OUTPUT_FIELDS, name, &create_alignment_output_field::name)};
  return iter == OUTPUT_FIELDS.end() ? nullptr : &*iter;
}

}  // namespace celonis::accelerator::operators::process::align_model::v2
