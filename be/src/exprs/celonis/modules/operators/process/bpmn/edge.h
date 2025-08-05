#pragma once

#include <tuple>

#include "legacy_embedded_ctl/hash.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/operators/process/bpmn/vertex.h"
#include "modules/operators/process/bpmn/vertex_types.h"

namespace celonis::accelerator::operators::process::bpmn {

using object_id = uint16_t;
using count_type = cel_int_t;  // Same as original count type, do we need signed and 64 bit?

class edge {
 public:
  edge(vertex_id_type source, vertex_id_type target, object_id object_id = {}, count_type count = {})
      : source_{source}, target_{target}, object_id_{object_id}, edge_count_{count} {}
  edge(const vertex& source_vertex, const vertex& target_vertex, object_id object = {}, count_type count = {})
      : source_{source_vertex.get_vertex_id()},
        target_{target_vertex.get_vertex_id()},
        object_id_{object},
        edge_count_{count} {}
  [[nodiscard]] vertex_id_type get_source_id() const noexcept { return source_; }
  [[nodiscard]] vertex_id_type get_target_id() const noexcept { return target_; }
  [[nodiscard]] object_id get_object_id() const noexcept { return object_id_; }
  [[nodiscard]] count_type get_count() const noexcept { return edge_count_; }

  void set_source_id(vertex_id_type source) noexcept { source_ = source; }
  void set_target_id(vertex_id_type target) noexcept { target_ = target; }
  void set_count(count_type count) { edge_count_ = count; }

  [[nodiscard]] bool operator<(const edge& other) const noexcept {
    return std::tie(source_, target_, object_id_) < std::tie(other.source_, other.target_, other.object_id_);
  }

  [[nodiscard]] bool operator==(const edge& other) const noexcept {
    return source_ == other.source_ && target_ == other.target_ && object_id_ == other.object_id_;
  }

 private:
  vertex_id_type source_{};
  vertex_id_type target_{};
  object_id object_id_{};
  count_type edge_count_{};
};

struct edge_hash_ignore_count {
  size_t operator()(const edge& e) const {
    std::size_t seed{};
    legacy_embedded_ctl::hash_combine(seed, e.get_source_id());
    legacy_embedded_ctl::hash_combine(seed, e.get_target_id());
    legacy_embedded_ctl::hash_combine(seed, e.get_object_id());
    return seed;
  }
};
struct edge_equal_to_ignore_count {
  bool operator()(const edge& first, const edge& second) const {
    return first.get_source_id() == second.get_source_id() && first.get_target_id() == second.get_target_id() &&
           first.get_object_id() == second.get_object_id();
  }
};

}  // namespace celonis::accelerator::operators::process::bpmn
