#pragma once

#include <map>
#include <queue>
#include <vector>
#include "exprs/celonis/agg/variant_util.h"
#include "exprs/celonis/agg/variant.h"
#include "modules/query/variantstats.pb.h"
#include "rapidjson/document.h"

namespace starrocks {

using EdgeHashMap = phmap::flat_hash_map<Edge, EdgeStats, HashOnEdge, EqualOnEdge>;

template<typename ActivityMapType>
class EdgeStatsProcessor {
public:

    static std::vector<EdgeHashMap::const_iterator>
    get_sorted_edges(const EdgeHashMap& edge_stats,
                     const ActivityMapType& activity_map,
                     size_t edge_count);

    static rapidjson::Value
    build_edge_stats_json(const std::vector<EdgeHashMap::const_iterator>& sorted_edges,
                          rapidjson::Document::AllocatorType& allocator);

    static void
    build_edge_stats_proto(const std::vector<EdgeHashMap::const_iterator>& sorted_edges,
                           celonis::accelerator::Statistics& statistics_proto);

private:
    static std::vector<int32_t>
    create_activity_ordering_map(const ActivityMapType& activity_map);
};

// Template specializations for different activity map types
template<>
std::vector<int32_t>
EdgeStatsProcessor<SliceHashMap>::create_activity_ordering_map(
        const SliceHashMap& activity_map);

template<>
std::vector<int32_t>
EdgeStatsProcessor<std::vector<std::string>>::create_activity_ordering_map(
        const std::vector<std::string>& activity_array);

} // namespace starrocks