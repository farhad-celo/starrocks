#include "exprs/celonis/inductive_miner.h"

#include "column/column_helper.h"
#include "exprs/celonis/modules/operators/process/inductive_miner/inductive_miner_helper.h"
#include "exprs/celonis/result_table.h"
#include "exprs/celonis/variant.h"
#include "exprs/celonis/variant_agg.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

using celonis::accelerator::operators::process::InductiveMinerHelper;
using cel_int_t = int64_t;

namespace starrocks {
namespace {

SliceHashMap increment_id(const SliceHashMap& input_activity_map) {
    SliceHashMap activity_map;
    for (auto it = input_activity_map.begin(); it != input_activity_map.end(); it++) {
        activity_map.insert(std::pair<SliceWithHash, int32_t>(it->first, it->second + 1));
    }
    return activity_map;
}

VariantHashMap increment_id(const VariantHashMap& input_variant_map) {
    VariantHashMap variant_map;
    for (auto it = input_variant_map.begin(); it != input_variant_map.end(); it++) {
        Variant variant = it->first;
        for (auto& id : variant.data) {
            id++;
        }
        variant_map.insert(std::pair(variant, it->second));
    }
    return variant_map;
}

} // namespace

std::string InductiveMinerFinalizer::json_string(const SliceHashMap& activity_map,
                                                 const celonis::ResultTable& vertex_table,
                                                 const celonis::ResultTable& edge_table,
                                                 const std::unordered_map<std::string, size_t>& statistics_map) {
    rapidjson::Document d;
    rapidjson::Document::AllocatorType& allocator = d.GetAllocator();
    d.SetObject();

    std::vector<Slice> activities;
    activities.reserve(activity_map.size() + 1);
    for (auto it = activity_map.begin(); it != activity_map.end(); it++) {
        if (it->second >= activities.size()) {
            activities.resize(it->second + 1);
        }
        activities[it->second] = it->first;
    }

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

std::string InductiveMinerFinalizer::finalize() {
    // Activity ID 0 is reserved for NULL in Saola and implementations depend on it.
    auto activity_map = increment_id(activity_map_);
    auto variant_map = increment_id(variant_map_);

    double imfd_frequency_threshold = 0.0;
    if (ctx_->is_constant_column(2)) {
        imfd_frequency_threshold = ColumnHelper::get_const_value<TYPE_DOUBLE>(ctx_->get_constant_column(2));
    }

    InductiveMinerHelper helper(variant_map, imfd_frequency_threshold);
    return json_string(activity_map, helper.vertex_table(), helper.edge_table(), helper.statistics());
}

} // namespace starrocks