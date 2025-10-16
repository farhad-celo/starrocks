#include "exprs/celonis/agg/variant_stats_utils.h"
#include "modules/query/variantstats.pb.h"

namespace starrocks {

rapidjson::Value EdgeStats::to_json(rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("count", count, allocator);
    obj.AddMember("count_case", count_case, allocator);
    return obj;
}

std::string EdgeStats::debug_string() const {
    std::stringstream ss;
    ss << "count " << count << " count_case " << count_case;
    return ss.str();
}

rapidjson::Value ActivityStats::to_json(rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("count", count, allocator);
    obj.AddMember("count_case", count_case, allocator);
    obj.AddMember("count_start", count_start, allocator);
    obj.AddMember("count_end", count_end, allocator);
    return obj;
}

std::pair<int32_t, size_t> maybe_add_activity(SliceHashMap& activity_map, const Slice& activity, MemPool* mem_pool,
    size_t* memory){
    // TODO(j.kim): Reserve activity id 0 for null to be consistent with Saola.
    int32_t index = 0;
    SliceWithHash key(activity);
    size_t hash = key.hash;

    auto it = activity_map.find(key, key.hash);
    if (it == activity_map.end()) {
        // New activity - allocate memory
        char* pos = (char*)(mem_pool->allocate(key.size));
        DCHECK(pos != nullptr);
        memcpy(pos, key.data, key.size);
        *memory += phmap::item_serialize_size<SliceHashMap>::value;
        key.data = pos;
        index = activity_map.size();
        activity_map.insert(std::pair<SliceWithHash, int32_t>(key, index));
    } else {
        key.data = it->first.data;
        index = it->second;
    }
    return std::make_pair(index, hash);
}

// Note that we've been using int32_t to index activities. So it's safe to use uint32_t for activity index.
uint8_t* serialize_activity_map(uint8_t* dst, const SliceHashMap& activity_map) {
    uint32_t num_activities = activity_map.size();
    memcpy(dst, &num_activities, sizeof(uint32_t));
    dst += sizeof(uint32_t);
    for (auto it = activity_map.begin(); it != activity_map.end(); ++it) {
        uint32_t size = it->first.size;
        uint32_t idx = it->second;
        memcpy(dst, &idx, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        memcpy(dst, &size, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        memcpy(dst, it->first.data, it->first.size);
        dst += it->first.size;
    }
    return dst;
}
size_t get_serialized_size(const SliceHashMap& activity_map) {
    size_t result = 0;
    result += sizeof(uint32_t); // num_activities
    for (auto it = activity_map.begin(); it != activity_map.end(); ++it) {
        result += sizeof(uint32_t) * 2;  // idx, size
        result += it->first.size;        // data
    }
    return result;
}
const uint8_t* deserialize_activity_map_and_merge(const uint8_t* src, std::vector<std::pair<int32_t, size_t>>& index_vector,
    SliceHashMap& activity_map, MemPool* mem_pool, size_t* memory) {
    uint32_t num_activities;
    memcpy(&num_activities, src, sizeof(uint32_t));
    src += sizeof(uint32_t);
    index_vector.resize(num_activities);
    for (size_t i = 0; i < num_activities; ++i) {
        uint32_t len;
        uint32_t idx;
        memcpy(&idx, src, sizeof(uint32_t));
        src += sizeof(uint32_t);
        memcpy(&len, src, sizeof(uint32_t));
        src += sizeof(uint32_t);
        Slice s(src, len);
        src += len;
        index_vector[idx] = maybe_add_activity(activity_map, s, mem_pool, memory);
    }
    return src;
}

// Note that we've been using int32_t to index activities. So it's safe to use uint32_t for activity index.
uint8_t* serialize_variant_map(uint8_t* dst, const VariantHashMap& variant_map) {
    size_t num_variants = variant_map.size();
    memcpy(dst, &num_variants, sizeof(size_t));
    dst += sizeof(size_t);
    for (auto it = variant_map.begin(); it != variant_map.end(); ++it) {
        size_t count = it->second;
        uint32_t num_activities = it->first.data.size();

        memcpy(dst, &count, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &num_activities, sizeof(uint32_t));
        dst += sizeof(uint32_t);

        for (size_t i = 0; i < it->first.data.size(); i++) {
            uint32_t idx = it->first.data[i];
            memcpy(dst, &idx, sizeof(uint32_t));
            dst += sizeof(uint32_t);
        }
    }
    return dst;
}
size_t get_serialized_size(const VariantHashMap& variant_map) {
    size_t result = sizeof(size_t); // num_variants
    for (auto it = variant_map.begin(); it != variant_map.end(); ++it) {
        result += sizeof(size_t);                           // count
        result += sizeof(uint32_t);                         // num_activities
        result += sizeof(uint32_t) * it->first.data.size();  // activity indices
    }
    return result;
}
const uint8_t* deserialize_variant_map_and_merge(const uint8_t* src, const std::vector<std::pair<int32_t, size_t>>& index_vector,
    VariantHashMap& variant_map) {
    size_t num_variants;
    memcpy(&num_variants, src, sizeof(size_t));
    src += sizeof(size_t);

    for (size_t i = 0; i < num_variants; ++i) {
        size_t count;
        uint32_t num_activities;
        memcpy(&count, src, sizeof(size_t));
        src += sizeof(size_t);
        memcpy(&num_activities, src, sizeof(uint32_t));
        src += sizeof(uint32_t);

        Variant variant(num_activities);
        for (size_t j = 0; j < num_activities; ++j) {
            uint32_t idx;
            memcpy(&idx, src, sizeof(uint32_t));
            src += sizeof(uint32_t);
            auto pair = index_vector[idx];
            variant.add(pair.first, pair.second);
        }
        variant_map[variant] += count;
    }
    return src;
}

std::string ActivityStats::debug_string() const {
    std::stringstream ss;
    ss << "count " << count << " count_case " << count_case << " count_start " << count_start << " count_end "
       << count_end;
    return ss.str();
}

VRef compute_happy_variant(const std::vector<VRef>& sorted, const std::vector<ActivityStats>& activity_stats) {
    // Happy path
    // find top start activity
    // find top end activity which is not top start
    // find top variant with start and end from above
    // if not found use top variant
    int top_start = 0;
    size_t count_start = 0;
    for (size_t i = 0; i < activity_stats.size(); i++) {
        if (activity_stats[i].count_start > count_start) {
            count_start = activity_stats[i].count_start;
            top_start = i;
        }
    }
    // top_end activity has to be different from top start.
    int top_end = 0;
    size_t count_end = 0;
    for (size_t i = 0; i < activity_stats.size(); i++) {
        if (activity_stats[i].count_end > count_end && i != top_start) {
            count_end = activity_stats[i].count_end;
            top_end = i;
        }
    }
    // Note: if there is a single activity in the data we will get start = 0, end = 0 and will return the top variant.
    int happy = 0; // default happy is top freq variant.
    for (size_t i = 0; i < sorted.size(); i++) {
        const auto& v = sorted[i]->first.data;
        if (v.empty()) {
            continue;
        }
        if (v[0] == top_start && v[v.size() - 1] == top_end) {
            happy = i;
            break;
        }
    }
    return sorted[happy];
}


VariantAnalysisResult analyze_variants_for_explore_process(const VariantHashMap& variant_counts,
    const SliceHashMap& activity_map, const std::vector<ActivityStats>& activity_stats,
    const std::string& log_prefix) {
    DCHECK(!variant_counts.empty() && !activity_map.empty());
    VariantAnalysisResult result;

    // 1. Sort variants by count
    std::vector<VRef> v_count(variant_counts.size());
    int index = 0;
    for (auto it = variant_counts.cbegin(); it != variant_counts.cend(); ++it) {
        v_count[index++] = it;
    }
    LOG(INFO) << log_prefix << ": started variants sorting\n";
    std::sort(v_count.begin(), v_count.end(),
              [](const VRef& lhs, const VRef& rhs) { return lhs->second > rhs->second; });
    LOG(INFO) << log_prefix << ": done variants sorting (variant_map_ size = " << variant_counts.size()
              << ")\n";

    // 2. Find a happy variant
    result.happy = compute_happy_variant(v_count, activity_stats);

    // 3. Find top-10 variants for each activity
    // Make sure we get enough variants so that each activity has 10 entries
    auto& activity_top_variants = result.activity_top_variants;
    activity_top_variants.resize(activity_map.size());
    for (size_t i = 0; i < activity_top_variants.size(); i++) {
        activity_top_variants[i].reserve(10);
    }
    std::vector<int8_t> a_done(activity_map.size(), 0);
    int done_count = 0;
    for (size_t i = 0; i < v_count.size(); i++) {
        // Check activities matched by this variant.
        const auto& variant = v_count[i]->first;
        std::vector<int8_t> a_seen(activity_map.size(), 0);
        for (size_t j = 0; j < variant.data.size(); j++) {
            uint32_t idx = variant.data[j];
            if (a_done[idx] == 0 && a_seen[idx] == 0) {
                a_seen[idx] = 1;
                activity_top_variants[idx].push_back(v_count[i]);
                if (activity_top_variants[idx].size() >= 10) {
                    a_done[idx] = 1;
                    done_count++;
                }
            }
        }
        // Stop early if we have already collected 10 variants for every activity.
        if (done_count == activity_map.size()) {
            break;
        }
    }

    return result;
}

void build_variant_analysis_proto(const VariantAnalysisResult& variant_analysis_result,
    celonis::accelerator::Statistics& statistics_proto, const std::string& log_prefix) {
    const std::vector<VList>& activity_top_variants = variant_analysis_result.activity_top_variants;
    const VRef& happy = variant_analysis_result.happy;
    uint32_t total_variants = 0;
    for (size_t i = 0; i < activity_top_variants.size(); ++i) {
        celonis::accelerator::VariantEntry entry;
        entry.set_id(i);
        for (size_t j = 0; j < activity_top_variants[i].size(); ++j) {
            celonis::accelerator::VariantCountPair count_pair;
            size_t count = activity_top_variants[i][j]->second;
            count_pair.set_count(count);
            const auto& data = activity_top_variants[i][j]->first.data;
            for (const auto& activity : data) {
                count_pair.add_variant(activity);
            }
            *entry.add_top() = count_pair;
            ++total_variants;
        }
        *statistics_proto.add_top() = entry;
    }
    LOG(INFO) << log_prefix << ": total number of top variants = " << total_variants << "\n";
    celonis::accelerator::VariantCountPair& happy_variant_with_count = *statistics_proto.mutable_happy();
    happy_variant_with_count.set_count(happy->second);
    for (const auto& activity : happy->first.data) {
        happy_variant_with_count.add_variant(activity);
    }
}

void build_variant_analysis_json(const VariantAnalysisResult& variant_analysis_result,
    rapidjson::Document& d, rapidjson::Document::AllocatorType& allocator) {
    const std::vector<VList>& activity_top_variants = variant_analysis_result.activity_top_variants;
    const VRef& happy = variant_analysis_result.happy;
    rapidjson::Value topv(rapidjson::kArrayType);
    for (size_t i = 0; i < activity_top_variants.size(); ++i) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("id", i, allocator);
        rapidjson::Value a(rapidjson::kArrayType);
        for (size_t j = 0; j < activity_top_variants[i].size(); ++j) {
            rapidjson::Value var_obj(rapidjson::kObjectType);
            rapidjson::Value act = activity_top_variants[i][j]->first.to_json(allocator);
            size_t count = activity_top_variants[i][j]->second;
            var_obj.AddMember("variant", act, allocator);
            var_obj.AddMember("count", count, allocator);
            a.PushBack(var_obj, allocator);
        }
        obj.AddMember("top", a, allocator);
        topv.PushBack(obj, allocator);
    }
    d.AddMember("top", topv, allocator);
    // Happy path
    size_t happy_count = happy->second;
    rapidjson::Value happy_obj(rapidjson::kObjectType);
    rapidjson::Value happy_var = happy->first.to_json(allocator);
    happy_obj.AddMember("variant", happy_var, allocator);
    happy_obj.AddMember("count", happy_count, allocator);
    d.AddMember("happy", happy_obj, allocator);
}

} // namespace starrocks

