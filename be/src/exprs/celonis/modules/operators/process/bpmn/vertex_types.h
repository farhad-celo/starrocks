#pragma once

#include <limits>
#include <string>
#include <variant>

#include "legacy_embedded_ctl/utility.h"
#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::operators::process::bpmn {

struct task {
  constexpr static std::string_view name{"BPMN_TASK"};
  row_id activity_id{};
  row_id count{};  // overall occurrence of respective activity, count on respective activity table
};

inline bool operator==(const task& lhs, const task& rhs) { return lhs.activity_id == rhs.activity_id; }

struct start {
  constexpr static std::string_view name{"BPMN_START"};
};

inline bool operator==(const start& /*lhs*/, const start& /*rhs*/) { return true; }

struct end {
  constexpr static std::string_view name{"BPMN_END"};
};

inline bool operator==(const end& /*lhs*/, const end& /*rhs*/) { return true; }

struct exclusive_choice {
  constexpr static std::string_view name{"BPMN_EXCLUSIVE_CHOICE"};
};

inline bool operator==(const exclusive_choice& /*lhs*/, const exclusive_choice& /*rhs*/) { return true; }

struct parallel {
  constexpr static std::string_view name{"BPMN_PARALLEL"};
};

inline bool operator==(const parallel& /*lhs*/, const parallel& /*rhs*/) { return true; }

// When a new type gets added to the variant it should also be added to convert_vertex_type_to_int
// todo(h.ashraf): maybe use boost::variant for performance?
using vertex_type = std::variant<task, start, end, exclusive_choice, parallel>;
using vertex_id_type = size_t;

/** Used to represent an invalid (e.g., non-initialized) vertex ID */
static constexpr vertex_id_type INVALID_VERTEX_ID{std::numeric_limits<vertex_id_type>::max()};

inline bool is_task(const vertex_type& type) { return std::holds_alternative<task>(type); }
inline bool is_non_null_task(const vertex_type& type) { return is_task(type) && std::get<task>(type).activity_id != 0; }
inline bool is_start(const vertex_type& type) { return std::holds_alternative<start>(type); }
inline bool is_end(const vertex_type& type) { return std::holds_alternative<end>(type); }
inline bool is_parallel(const vertex_type& type) { return std::holds_alternative<parallel>(type); }
inline bool is_exclusive_choice(const vertex_type& type) { return std::holds_alternative<exclusive_choice>(type); }

inline bool is_gateway(vertex_type type) {
  return std::holds_alternative<exclusive_choice>(type) || std::holds_alternative<parallel>(type);
}

[[nodiscard]] inline std::string to_string(const vertex_type& type) {
  return std::visit(
      legacy_embedded_ctl::overloaded{[](const task& /**/) { return "BPMN_TASK"; },
                      [](const parallel& /**/) { return "BPMN_PARALLEL"; },
                      [](const exclusive_choice& /**/) { return "BPMN_EXCLUSIVE_CHOICE"; },
                      [](const start& /**/) { return "BPMN_START"; }, [](const end& /**/) { return "BPMN_END"; }},
      type);
}

}  // namespace celonis::accelerator::operators::process::bpmn
