#pragma once

#include <optional>
#include <string_view>

#include <fmt/core.h>  // fmt/base.h in more recent versions

#include <cpml/conformance/alignment_settings.h>
#include <cpml/model/bpmn/vertex_types.h>
#include <cpml/model/bpmn_graph_fwd.h>
#include <ctl/hash.h>

#include "modules/common/enum_indexed_array.h"
#include "modules/cube/variant_trace_cache_manager.h"
#include "modules/cube/variant_trace_cache_manager_fwd.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::operators::process::align_model {

constexpr std::string_view INTERNAL_ALIGNMENT_TABLE_NAME{"ALIGNMENT"};
constexpr std::string_view USER_VISIBLE_ALIGNMENT_TABLE_NAME{INTERNAL_ALIGNMENT_TABLE_NAME};
constexpr std::string_view INTERNAL_ASSOCIATION_TABLE_NAME{"ASSOCIATION"};
constexpr std::string_view USER_VISIBLE_ASSOCIATION_TABLE_NAME{INTERNAL_ASSOCIATION_TABLE_NAME};
constexpr std::string_view INTERNAL_EDGE_CLASS_TABLE_NAME{"EDGE_CLASS"};
constexpr std::string_view USER_VISIBLE_EDGE_CLASS_TABLE_NAME{INTERNAL_EDGE_CLASS_TABLE_NAME};
constexpr std::string_view TABLE_GROUP_NAME{"ALIGN_MODEL_GROUP"};

// alignments columns
constexpr std::string_view ALIGNMENT_MODEL_VERTEX_ID{"MODEL_VERTEX_ID"};
constexpr std::string_view ALIGNMENT_ACTIVITY_LABEL{"VERTEX_LABEL"};
constexpr std::string_view ALIGNMENT_MOVE_TYPE{"MOVE_TYPE"};
constexpr std::string_view ALIGNMENT_DEVIATION_CATEGORY{"DEVIATION_CATEGORY"};
// association columns
constexpr std::string_view ASSOCIATION_COLUMN_NAME{"EDGE_CLASS"};
// edge class columns
constexpr std::string_view EDGE_CLASS_ID{"ID"};
constexpr std::string_view EDGE_CLASS_TYPE{"TYPE"};

struct align_model_config {
  static constexpr size_t ALIGN_MODEL_GRAIN_SIZE{1u << 15};

  [[nodiscard]] static align_model_config make(std::string pruned_variant_cache_key,
                                               cube::variant_trace_cache_manager& trace_cache_manager,
                                               cpml::conformance::alignment_execution_strategy execution_strategy =
                                                   cpml::conformance::alignment_execution_strategy::DEFAULT);

  size_t grain_size{};
  std::string pruned_variant_cache_key;
  cube::variant_trace_cache_manager& variant_trace_cache_manager_instance;
  cpml::conformance::alignment_execution_strategy execution_strategy;
};

using variants = memory::cache::variant_trace_cache_t;

enum class alignment_move_type : std::uint8_t {
  GATEWAY_MOVE = 1,
  LOG_MOVE = 2,
  MODEL_MOVE = 3,
  SYNC_MOVE = 4,
  UNMAPPED_MOVE = 5,
  SIZE = 6
};

class alignment_move {
 public:
  enum class do_verify_invariant : bool { NO = false, YES = true };

  template <do_verify_invariant VERIFY_INVARIANT = do_verify_invariant::YES>
  [[nodiscard]] static alignment_move make_alignment_move(
      alignment_move_type move_type, std::optional<row_id> move_on_log,
      std::optional<cpml::model::bpmn::vertex_id_type> move_on_model);

  [[nodiscard]] static alignment_move make_gateway_move(cpml::model::bpmn::vertex_id_type vertex_id);

  // N.B.: For FE support we also require a vertex reference for a log move
  [[nodiscard]] static alignment_move make_log_move(row_id activity_id, cpml::model::bpmn::vertex_id_type vertex_id);

  [[nodiscard]] static alignment_move make_model_move(cpml::model::bpmn::vertex_id_type vertex_id);

  [[nodiscard]] static alignment_move make_sync_move(row_id activity_id, cpml::model::bpmn::vertex_id_type vertex_id);

  [[nodiscard]] static alignment_move make_unmapped_move(row_id activity_id);

  [[nodiscard]] auto operator<=>(const alignment_move&) const noexcept = default;  // NOLINT(modernize-use-nullptr)

  // N.B.: A gateway move is a special case of a model move which we distinguish here.
  // TODO(n.weber): For explicitness, it might make sense to introduce a 'is_task_node_move()' and define
  //   'is_model_move()' as 'is_gateway_move() || is_task_node_move()' since there is already code doing the following
  //    const auto is_model_move{is_gateway_move() || is_model_move()};
  //    which might be confusing if one is not familiar with this types invariants and naming
  [[nodiscard]] bool is_gateway_move() const;
  [[nodiscard]] bool is_log_move() const;
  [[nodiscard]] bool is_model_move() const;
  [[nodiscard]] bool is_sync_move() const;
  [[nodiscard]] bool is_unmapped_move() const;

  [[nodiscard]] alignment_move_type move_type() const { return move_type_; }
  [[nodiscard]] std::optional<row_id> move_on_log() const { return move_on_log_; }
  [[nodiscard]] std::optional<cpml::model::bpmn::vertex_id_type> move_on_model() const { return move_on_model_; }

 private:
  alignment_move(alignment_move_type move_type, std::optional<row_id> move_on_log,
                 std::optional<cpml::model::bpmn::vertex_id_type> move_on_model);

  alignment_move_type move_type_{alignment_move_type::UNMAPPED_MOVE};
  std::optional<row_id> move_on_log_{std::nullopt};
  std::optional<cpml::model::bpmn::vertex_id_type> move_on_model_{std::nullopt};
};

using alignment_t = std::vector<alignment_move>;
using alignment_view_t = ctl::array_view<const alignment_t::value_type>;
using alignments_t = std::vector<std::optional<alignment_t>>;
using alignments_view_t = ctl::array_view<const alignments_t::value_type>;

void verify_alignment_constraints(alignment_view_t value);

using edge_class_id_t = row_id;
enum class edge_type : std::uint8_t {
  SYNC,
  MODEL,
  SKIP,
  LOG,
  UNMAPPED,
  L1_MISSING,
  L1_EXCLUSIVE_VIOLATION,
  SIZE  // Not an actual edge type, exists to encode the size of the enum at compile time
};

template <typename T>
using edge_type_array = common::enum_indexed_array<edge_type, T>;

constexpr std::array<edge_type, ctl::enum_to_underlying_type(edge_type::SIZE)> EDGE_TYPES{
    edge_type::SYNC,
    edge_type::MODEL,
    edge_type::SKIP,
    edge_type::LOG,
    edge_type::UNMAPPED,
    edge_type::L1_MISSING,
    edge_type::L1_EXCLUSIVE_VIOLATION};

template <typename T, typename F>
requires(std::is_invocable_r_v<T, F, edge_type>) [[nodiscard]] edge_type_array<T> for_each_edge_type(F&& functor) {
  static_assert(EDGE_TYPES[0] == edge_type::SYNC, "Edge type must be at the position of their int representation.");
  static_assert(EDGE_TYPES[1] == edge_type::MODEL, "Edge type must be at the position of their int representation.");
  static_assert(EDGE_TYPES[2] == edge_type::SKIP, "Edge type must be at the position of their int representation.");
  static_assert(EDGE_TYPES[3] == edge_type::LOG, "Edge type must be at the position of their int representation.");
  static_assert(EDGE_TYPES[4] == edge_type::UNMAPPED, "Edge type must be at the position of their int representation.");
  static_assert(EDGE_TYPES[5] == edge_type::L1_MISSING,
                "Edge type must be at the position of their int representation.");
  static_assert(EDGE_TYPES[6] == edge_type::L1_EXCLUSIVE_VIOLATION,
                "Edge type must be at the position of their int representation.");
  static_assert(ctl::enum_to_underlying_type(edge_type::SIZE) == 7,
                "This function must be adjusted if edge types are added or removed");
  return edge_type_array<T>{functor(EDGE_TYPES[0]), functor(EDGE_TYPES[1]), functor(EDGE_TYPES[2]),
                            functor(EDGE_TYPES[3]), functor(EDGE_TYPES[4]), functor(EDGE_TYPES[5]),
                            functor(EDGE_TYPES[6])};
}

struct alignment_move_type_strings {
  constexpr static std::string_view GATEWAY{"GATEWAY_MOVE"};
  constexpr static std::string_view SYNC{"SYNC_MOVE"};
  constexpr static std::string_view MODEL{"MODEL_MOVE"};
  constexpr static std::string_view LOG{"LOG_MOVE"};
  constexpr static std::string_view UNMAPPED{"UNMAPPED_MOVE"};
};

[[nodiscard]] constexpr static std::string_view alignment_move_to_string(alignment_move_type move_type) {
  switch (move_type) {
    case alignment_move_type::GATEWAY_MOVE:
      return alignment_move_type_strings::GATEWAY;
    case alignment_move_type::UNMAPPED_MOVE:
      return alignment_move_type_strings::UNMAPPED;
    case alignment_move_type::LOG_MOVE:
      return alignment_move_type_strings::LOG;
    case alignment_move_type::MODEL_MOVE:
      return alignment_move_type_strings::MODEL;
    case alignment_move_type::SYNC_MOVE:
      return alignment_move_type_strings::SYNC;
    default:
      ctl::assert_unreachable();
  }
}

struct edge_type_strings {
  constexpr static std::string_view SYNC{"SYNC_EDGE"};
  constexpr static std::string_view MODEL{"MODEL_EDGE"};
  constexpr static std::string_view SKIP{"SKIP_EDGE"};
  constexpr static std::string_view LOG{"LOG_EDGE"};
  constexpr static std::string_view UNMAPPED{"UNMAPPED_EDGE"};
  constexpr static std::string_view L1_MISSING{"L1_MISSING"};
  constexpr static std::string_view L1_EXCLUSIVE_VIOLATION{"L1_EXCLUSIVE_VIOLATION"};
};

[[nodiscard]] constexpr std::string_view edge_type_to_string(edge_type edge) {
  switch (edge) {
    case edge_type::SYNC:
      return edge_type_strings::SYNC;
    case edge_type::MODEL:
      return edge_type_strings::MODEL;
    case edge_type::LOG:
      return edge_type_strings::LOG;
    case edge_type::SKIP:
      return edge_type_strings::SKIP;
    case edge_type::UNMAPPED:
      return edge_type_strings::UNMAPPED;
    case edge_type::L1_MISSING:
      return edge_type_strings::L1_MISSING;
    case edge_type::L1_EXCLUSIVE_VIOLATION:
      return edge_type_strings::L1_EXCLUSIVE_VIOLATION;
    default:
      ctl::assert_unreachable();
  }
}

// TODO(j.kruska) For the streamlined operator we adjusted the names slightly, once the old operator is remove we can
// remove the above edge_type_to_string function
[[nodiscard]] constexpr std::string_view edge_type_to_string_v2(edge_type edge) {
  switch (edge) {
    case edge_type::SYNC:
      return edge_type_strings::SYNC;
    case edge_type::MODEL:
      return edge_type_strings::MODEL;
    case edge_type::LOG:
      return edge_type_strings::LOG;
    case edge_type::SKIP:
      return edge_type_strings::SKIP;
    case edge_type::UNMAPPED:
      return edge_type_strings::UNMAPPED;
    case edge_type::L1_MISSING:
      return "MISSING_VIOLATION";
    case edge_type::L1_EXCLUSIVE_VIOLATION:
      return "EXCLUSIVE_VIOLATION";
    default:
      ctl::assert_unreachable();
  }
}

}  // namespace celonis::accelerator::operators::process::align_model

template <>
struct fmt::formatter<celonis::accelerator::operators::process::align_model::alignment_move> : formatter<std::string> {
  auto format(const celonis::accelerator::operators::process::align_model::alignment_move& value,
              format_context& ctx) const -> format_context::iterator;
};
