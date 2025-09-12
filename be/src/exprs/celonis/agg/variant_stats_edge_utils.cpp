#include "variant_stats_edge_utils.h"

namespace starrocks {
namespace {
struct EdgeOrderedID {
    int32_t ordered_src;
    int32_t ordered_dst;
    EdgeHashMap::const_iterator it;
};
struct CmpOnEdgeOrderedID {
    bool operator()(const EdgeOrderedID& x, const EdgeOrderedID& y) const {
        return std::tie(x.ordered_src, x.ordered_dst) < std::tie(y.ordered_src, y.ordered_dst);
    }
};
}

template<typename ActivityMapType>
std::vector<EdgeHashMap::const_iterator>
EdgeStatsProcessor<ActivityMapType>::get_sorted_edges(const EdgeHashMap& edge_stats,
                                                      const ActivityMapType& activity_map,
                                                      size_t edge_count) {
    std::vector<int32_t> activity_unordered_to_ordered = create_activity_ordering_map(activity_map);

    std::priority_queue<EdgeOrderedID, std::vector<EdgeOrderedID>, CmpOnEdgeOrderedID> pq;
    for (auto it = edge_stats.cbegin(); it != edge_stats.cend(); ++it) {
        pq.push({activity_unordered_to_ordered[it->first.src],
                 activity_unordered_to_ordered[it->first.dst],
                 it});
        if (pq.size() > edge_count) {
            pq.pop();
        }
    }
    // Pop first to list them in reverse sorted order
    std::vector<EdgeHashMap::const_iterator> rv(pq.size());
    for (auto it = rv.rbegin(); it != rv.rend(); ++it) {
        *it = pq.top().it;
        pq.pop();
    }
    return rv;
}

template<typename ActivityMapType>
rapidjson::Value
EdgeStatsProcessor<ActivityMapType>::build_edge_stats_json(
        const std::vector<EdgeHashMap::const_iterator>& sorted_edges,
        rapidjson::Document::AllocatorType& allocator) {

    rapidjson::Value e_stats(rapidjson::kArrayType);
    for (const auto& edge_it : sorted_edges) {
        rapidjson::Value obj = edge_it->second.to_json(allocator);
        obj.AddMember("src", edge_it->first.src, allocator);
        obj.AddMember("dst", edge_it->first.dst, allocator);
        e_stats.PushBack(obj, allocator);
    }
    return e_stats;
}

template<typename ActivityMapType>
void
EdgeStatsProcessor<ActivityMapType>::build_edge_stats_proto(
        const std::vector<EdgeHashMap::const_iterator>& sorted_edges,
        celonis::accelerator::Statistics& statistics_proto) {

    for (const auto& edge_it : sorted_edges) {
        celonis::accelerator::EdgeStatsEntry entry;
        entry.set_count(edge_it->second.count);
        entry.set_count_case(edge_it->second.count_case);
        entry.set_src(edge_it->first.src);
        entry.set_dst(edge_it->first.dst);
        *statistics_proto.add_e_stats() = entry;
    }
}

// Specialization for variant_stats.cpp (SliceHashMap activity map)
template<>
std::vector<int32_t>
EdgeStatsProcessor<SliceHashMap>::create_activity_ordering_map(
        const SliceHashMap& activity_map) {

    std::map<SliceWithHash, int32_t> ordered_activity_map(activity_map.begin(), activity_map.end());
    std::vector<int32_t> activity_unordered_to_ordered(activity_map.size());
    int index = 0;
    for (auto it = ordered_activity_map.begin(); it != ordered_activity_map.end(); ++it, ++index) {
        DCHECK_LT(it->second, activity_unordered_to_ordered.size());
        activity_unordered_to_ordered[it->second] = index;
    }
    return activity_unordered_to_ordered;
}

// Specialization for variant_stats_v2.cpp (vector-based activity array)
template<>
std::vector<int32_t>
EdgeStatsProcessor<std::vector<std::string>>::create_activity_ordering_map(
        const std::vector<std::string>& activity_array) {

    std::map<std::string, int32_t> ordered_activity_map;
    for (auto i = 0; i < activity_array.size(); ++i) {
        ordered_activity_map.insert({activity_array[i], i});
    }
    std::vector<int32_t> activity_unordered_to_ordered(activity_array.size());
    int index = 0;
    for (auto it = ordered_activity_map.begin(); it != ordered_activity_map.end(); ++it, ++index) {
        DCHECK_LT(it->second, activity_unordered_to_ordered.size());
        activity_unordered_to_ordered[it->second] = index;
    }
    return activity_unordered_to_ordered;
}

// Explicit template instantiations
template class EdgeStatsProcessor<SliceHashMap>;
template class EdgeStatsProcessor<std::vector<std::string>>;

} // namespace starrocks