#pragma once

#include <vector>

#include <boost/graph/adjacency_list.hpp>
#ifndef CELOSTAR
#include <bytell_hash_map.hpp>
#endif

#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"
#ifdef CELOSTAR
#include "util/phmap/phmap.h"
#endif

namespace celonis::accelerator::operators::process {

/**
 * Boost graphs are veeery cumbersome. However, since there are many advanced
 * algorithms operating on them, part of them parallelized out-of-the-box, I chose
 * them anyway. Here are some links I found especially useful:
 * 1. http://www.boost.org/doc/libs/1_65_1/libs/graph/doc/bundles.html
 */

struct dfg_vertex_properties {
  // Maybe this ca be replaced by a smaller data type, since variantify forbids
  // more than 1000 distinct activities.
  row_id activity_id{0};
  size_t count{0};
  // Once these values are initialized, they will never change. Therefore, store them here.

  // allow list initialization:
  constexpr dfg_vertex_properties() noexcept = default;
  // Implicit conversion is heavily used
  constexpr dfg_vertex_properties(row_id activity_id) noexcept  // NOLINT(google-explicit-constructor)
      : activity_id{activity_id} {}
  constexpr dfg_vertex_properties(row_id activity_id, size_t count) noexcept : activity_id{activity_id}, count{count} {}
};

struct dfg_edge_properties {
  size_t count{0};

  // allow list initialization:
  constexpr dfg_edge_properties() noexcept = default;
  // Implicit conversion is heavily used
  constexpr dfg_edge_properties(size_t count) noexcept  // NOLINT(google-explicit-constructor)
      : count{count} {};
};

struct dfg_log_properties {
  size_t trace_count{0};
  size_t contains_empty_trace{0};
  /**
   * If there are any activities running in parallel to the ones of the current
   * (partial) log, sub traces are interwoven by them. Thus, any interferences
   * with them must be ignored.
   */
  std::vector<row_id> parallel_activities;
};

struct dfg_graph_properties {
  // The activities are stored in the vertex scale. The conversion from vertices
  // to activity ids (of the column dictionary) depends on the current recursion
  // depth of the algorithm, thus every vertex stores its corresponding activity
  // id.
#ifdef CELOSTAR
  phmap::flat_hash_map<size_t, size_t> start_vertices;
  phmap::flat_hash_map<size_t, size_t> end_vertices;
#else
  ska::bytell_hash_map<size_t, size_t> start_vertices;
  ska::bytell_hash_map<size_t, size_t> end_vertices;
#endif
  // I expect O(1) many start and end vertices (even though there might be n
  // vertices in the graph), therefore the above counts are stored here and not
  // inside their corresponding vertices.
  dfg_log_properties log;
};

/**
 * In the case you want to create a different container for the directly-follows
 * graph (dfg) using boost graph, it would be best to template (templatize?) all
 * functions receiving a dfg as an argument as follows:
 * 1. prepend `template<typename DFG>`
 * 2. change `directly_follows_graph dfg` to `DFG dfg`
 * Don't forget the tests!
 *
 * Doing so, all functions should work on both the old and the new implementation of
 * a directly-follows graph (as long as the new one uses boost graph, too).
 *
 * Maybe the access to the graph properties needs to be adjusted using property maps.
 */
using directly_follows_graph = boost::adjacency_list<boost::hash_setS  // OutEdgeList
                                                     ,
                                                     boost::vecS  // VertexList
                                                     ,
                                                     boost::bidirectionalS  // Directed
                                                     ,
                                                     dfg_vertex_properties  // VertexProperties
                                                     ,
                                                     dfg_edge_properties  // EdgeProperties
                                                     ,
                                                     dfg_graph_properties  // GraphProperties
                                                     >;

}  // namespace celonis::accelerator::operators::process
