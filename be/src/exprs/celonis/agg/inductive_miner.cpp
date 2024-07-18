#include "inductive_miner.h"

#include <algorithm>
#include <execution>

#include "cpml_proxy/inductive_miner_proxy.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "variant.h"

using cel_int_t = int64_t;

namespace starrocks {
namespace {

std::pair<std::vector<Slice>, Variants> sort_activities_and_variants(const SliceHashMap& activity_map,
                                                                     const VariantHashMap& variant_map) {
    // Sorts activities so that it matches Saola dictionary.
    std::map<Slice, int32_t> ordered_activity_map(activity_map.begin(), activity_map.end());
    std::vector<int32_t> activity_remap(ordered_activity_map.size()); // index: old id -> value: new id
    std::vector<Slice> activities;
    // Activity ID 0 is reserved for NULL in Saola and implementations depend on it.
    activities.reserve(ordered_activity_map.size() + 1);
    activities.push_back({});
    int index = 1;
    for (auto it = ordered_activity_map.begin() ; it != ordered_activity_map.end(); ++it, ++index) {
        if (it->second >= activity_remap.size()) {
            activity_remap.resize(it->second + 1);
        }
        activity_remap[it->second] = index;
        activities.push_back(it->first);
    }

    // Sorts variants so that it matches Saola variant_trace_cache.
    Variants variants;
    for (const auto& [variant, count] : variant_map) {
        std::vector<int32_t> ids;
        ids.reserve(variant.data.size());
        for (auto& id : variant.data) {
            ids.push_back(activity_remap[id]);
        }
        variants.emplace_back(std::move(ids), count);
    }
    std::sort(std::execution::par_unseq, variants.begin(), variants.end());

    return {activities, variants};
}

} // namespace

std::string InductiveMinerFinalizer::json_string(const std::vector<Slice>& activities,
                                                 const celonis::ResultTable& vertex_table,
                                                 const celonis::ResultTable& edge_table,
                                                 const std::unordered_map<std::string, size_t>& statistics_map) {
    rapidjson::Document d;
    rapidjson::Document::AllocatorType& allocator = d.GetAllocator();
    d.SetObject();

    rapidjson::Value vertex_properties(rapidjson::kArrayType);
    const auto& process_tree_type = vertex_table.column<cel_int_t>("process_tree_type");
    const auto& activity = vertex_table.nullable_column<cel_int_t>("activity");
    const auto& object_count = vertex_table.nullable_column<cel_int_t>("object_count");
    for (int i = 0; i < vertex_table.size(); i++) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("process_tree_type", process_tree_type[i], allocator);
        if (activity.is_null(i)) {
            obj.AddMember("activity", rapidjson::Value(), allocator);
        } else {
            auto& slice = activities[activity[i]];
            obj.AddMember("activity", rapidjson::Value().SetString(slice.get_data(), slice.get_size(), allocator),
                          allocator);
        }
        if (!object_count.is_null(i)) {
            obj.AddMember("object_count", object_count[i], allocator);
        }
        vertex_properties.PushBack(obj, allocator);
    }
    d.AddMember("vertex_properties", vertex_properties, allocator);

    rapidjson::Value edge_properties(rapidjson::kArrayType);
    const auto& edge_source_id = edge_table.column<cel_int_t>("edge_source_id");
    const auto& edge_target_id = edge_table.column<cel_int_t>("edge_target_id");
    for (int i = 0; i < edge_table.size(); i++) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("edge_source_id", edge_source_id[i], allocator);
        obj.AddMember("edge_target_id", edge_target_id[i], allocator);
        edge_properties.PushBack(obj, allocator);
    }
    d.AddMember("edge_properties", edge_properties, allocator);

    rapidjson::Value statistics(rapidjson::kArrayType);
    for (const auto& [key, value] : statistics_map) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("key", rapidjson::Value().SetString(key.c_str(), key.length(), allocator), allocator);
        auto value_str = std::to_string(value);
        obj.AddMember("value", rapidjson::Value().SetString(value_str.c_str(), value_str.length(), allocator),
                      allocator);
        statistics.PushBack(obj, allocator);
    }
    d.AddMember("statistics", statistics, allocator);

    // Encode to string.
    rapidjson::StringBuffer buf;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return buf.GetString();
}

std::optional<std::string> InductiveMinerFinalizer::finalize(FunctionContext* ctx) {
    // Matches activity ids and variant order to Saola.
    const auto& [activities, variants] = sort_activities_and_variants(activity_map_, variant_map_);

    const auto [vertex_table, edge_table, statistics]{cpml_proxy::inductive_miner(variants, imfd_frequency_threshold_)};
    return json_string(activities, *vertex_table, *edge_table, statistics);
}

} // namespace starrocks