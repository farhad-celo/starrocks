#pragma once

#include "modules/operators/process/bpmn/vertex_types.h"

namespace celonis::accelerator::operators::process::bpmn {

class vertex {
 public:
  vertex(vertex_id_type vertex_id, vertex_type type) : vertex_id_{vertex_id}, type_{type} {}
  [[nodiscard]] vertex_id_type get_vertex_id() const noexcept { return vertex_id_; }
  [[nodiscard]] const vertex_type& get_vertex_type() const noexcept { return type_; }
  [[nodiscard]] vertex_type& get_vertex_type() noexcept { return type_; }

  [[nodiscard]] bool operator<(const vertex& other) const {
    return std::forward_as_tuple(vertex_id_, type_.index()) <
           std::forward_as_tuple(other.vertex_id_, other.type_.index());
  }

  [[nodiscard]] bool operator==(const vertex& other) const {
    return vertex_id_ == other.vertex_id_ && type_ == other.type_;
  }

 private:
  vertex_id_type vertex_id_{};
  vertex_type type_{};
};

inline bool is_task(const vertex& v) { return is_task(v.get_vertex_type()); }
inline bool is_non_null_task(const vertex& v) { return is_non_null_task(v.get_vertex_type()); }
inline bool is_start(const vertex& v) { return is_start(v.get_vertex_type()); }
inline bool is_end(const vertex& v) { return is_end(v.get_vertex_type()); }
inline bool is_parallel(const vertex& v) { return is_parallel(v.get_vertex_type()); }
inline bool is_exclusive_choice(const vertex& v) { return is_exclusive_choice(v.get_vertex_type()); }
inline bool is_gateway(const vertex& v) { return is_gateway(v.get_vertex_type()); }

}  // namespace celonis::accelerator::operators::process::bpmn
