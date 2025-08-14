#include "align_model_types.h"

#include <fmt/format.h>

namespace celonis::accelerator::operators::process::align_model {

template <alignment_move::do_verify_invariant VERIFY_INVARIANT>
alignment_move alignment_move::make_alignment_move(
    const alignment_move_type move_type, const std::optional<row_id> move_on_log,
    const std::optional<cpml::model::bpmn::vertex_id_type> move_on_model) {
  if constexpr (ctl::as_bool(VERIFY_INVARIANT)) {
    switch (move_type) {
      using enum alignment_move_type;
      case GATEWAY_MOVE:
        common::runtime_assert(!move_on_log.has_value(),
                               "A move of type GATEWAY_MOVE should not have a move_on_log set.");
        common::runtime_assert(move_on_model.has_value(), "A move of type GATEWAY_MOVE must have a move_on_model set.");
        break;
      case LOG_MOVE:
        common::runtime_assert(move_on_log.has_value(), "A move of type LOG_MOVE must have a move_on_log set.");
        // N.B.: Pre-condition for FE support
        common::runtime_assert(move_on_model.has_value(), "A move of type LOG_MOVE must have a move_on_model set.");
        break;
      case MODEL_MOVE:
        common::runtime_assert(!move_on_log.has_value(),
                               "A move of type MODEL_MOVE should not have a move_on_log set.");
        common::runtime_assert(move_on_model.has_value(), "A move of type MODEL_MOVE must have a move_on_model set.");
        break;
      case SYNC_MOVE:
        common::runtime_assert(move_on_log.has_value(), "A move of type SYNC_MOVE must have a move_on_log set.");
        common::runtime_assert(move_on_model.has_value(), "A move of type SYNC_MOVE must have a move_on_model set.");
        break;
      case UNMAPPED_MOVE:
        common::runtime_assert(move_on_log.has_value(), "A move of type UNMAPPED_MOVE must have a move_on_log set.");
        common::runtime_assert(!move_on_model.has_value(),
                               "A move of type UNMAPPED_MOVE should not have a move_on_model set.");
        break;
      default:
        ctl::assert_unreachable();
    }
  }
  return alignment_move{move_type, move_on_log, move_on_model};
}

alignment_move alignment_move::make_gateway_move(const cpml::model::bpmn::vertex_id_type vertex_id) {
  return make_alignment_move<do_verify_invariant::NO>(alignment_move_type::GATEWAY_MOVE, std::nullopt, vertex_id);
}

alignment_move alignment_move::make_log_move(const row_id activity_id,
                                             const cpml::model::bpmn::vertex_id_type vertex_id) {
  return make_alignment_move<do_verify_invariant::NO>(alignment_move_type::LOG_MOVE, activity_id, vertex_id);
}

alignment_move alignment_move::make_model_move(const cpml::model::bpmn::vertex_id_type vertex_id) {
  return make_alignment_move<do_verify_invariant::NO>(alignment_move_type::MODEL_MOVE, std::nullopt, vertex_id);
}

alignment_move alignment_move::make_sync_move(const row_id activity_id,
                                              const cpml::model::bpmn::vertex_id_type vertex_id) {
  return make_alignment_move<do_verify_invariant::NO>(alignment_move_type::SYNC_MOVE, activity_id, vertex_id);
}

alignment_move alignment_move::make_unmapped_move(const row_id activity_id) {
  return make_alignment_move<do_verify_invariant::NO>(alignment_move_type::UNMAPPED_MOVE, activity_id, std::nullopt);
}

bool alignment_move::is_gateway_move() const { return move_type() == alignment_move_type::GATEWAY_MOVE; }

bool alignment_move::is_log_move() const { return move_type() == alignment_move_type::LOG_MOVE; }

bool alignment_move::is_model_move() const { return move_type() == alignment_move_type::MODEL_MOVE; }

bool alignment_move::is_sync_move() const { return move_type() == alignment_move_type::SYNC_MOVE; }

bool alignment_move::is_unmapped_move() const { return move_type() == alignment_move_type::UNMAPPED_MOVE; }

alignment_move::alignment_move(const alignment_move_type move_type, const std::optional<row_id> move_on_log,
                               const std::optional<cpml::model::bpmn::vertex_id_type> move_on_model)
    : move_type_{move_type}, move_on_log_{move_on_log}, move_on_model_{move_on_model} {}

template alignment_move alignment_move::make_alignment_move<alignment_move::do_verify_invariant::YES>(
    alignment_move_type, std::optional<row_id>, std::optional<cpml::model::bpmn::vertex_id_type>);
template alignment_move alignment_move::make_alignment_move<alignment_move::do_verify_invariant::NO>(
    alignment_move_type, std::optional<row_id>, std::optional<cpml::model::bpmn::vertex_id_type>);

void verify_alignment_constraints(const alignment_view_t value) {
  common::runtime_assert(
      value.size() >= 2 && value.front().is_gateway_move() && value.back().is_gateway_move(),
      "Alignment must contain at least two gateway moves for the start and end gateways respectively");
}

// TODO(n.weber): Temporary using decls until code is migrated to CPML
using cpml::model::bpmn::vertex_id_type;

align_model_config align_model_config::make(std::string pruned_variant_cache_key,
                                            cube::variant_trace_cache_manager& trace_cache_manager,
                                            cpml::conformance::alignment_execution_strategy execution_strategy) {
  return {ALIGN_MODEL_GRAIN_SIZE, std::move(pruned_variant_cache_key), trace_cache_manager, execution_strategy};
}

}  // namespace celonis::accelerator::operators::process::align_model

auto fmt::formatter<celonis::accelerator::operators::process::align_model::alignment_move>::format(
    const celonis::accelerator::operators::process::align_model::alignment_move& value, format_context& ctx) const
    -> format_context::iterator {
  // TODO(n.weber): newer fmt has support for std::optional
  static const auto format_optional{[]<ctl::standard_integer T>(const std::optional<T>& optional_value) -> std::string {
    if (optional_value.has_value()) {
      return std::to_string(*optional_value);
    }
    return "std::nullopt";
  }};

  const auto move_type_str{alignment_move_to_string(value.move_type())};
  const auto move_on_log_str{format_optional(value.move_on_log())};
  const auto move_on_model_str{format_optional(value.move_on_model())};

  return formatter<std::string>::format(fmt::format("move_type=[{}], move_on_log=[{}], move_on_model=[{}]",
                                                    move_type_str, move_on_log_str, move_on_model_str),
                                        ctx);
}
