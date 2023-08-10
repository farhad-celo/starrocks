#include "exprs/celonis/variant_stats.h"

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "runtime/mem_pool.h"

namespace starrocks {

rapidjson::Value Edge::to_json(rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("src", src, allocator);
    obj.AddMember("dst", dst, allocator);
    return obj;
}

std::string Edge::debug_string() const {
    std::stringstream ss;
    ss << "src " << src << " dst " << dst;
    return ss.str();
}

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

std::string ActivityStats::debug_string() const {
    std::stringstream ss;
    ss << "count " << count << " count_case " << count_case << " count_start " << count_start << " count_end "
       << count_end;
    return ss.str();
}

int VariantStatsFinalizer::compute_happy_variant(const std::vector<VRef>& sorted) const {
    // Happy path
    // find top start activity
    // find top end activity
    // find top variant with start end from above
    // if not found use top variant

    int top_start = 0;
    size_t count_start = 0;
    for (int i = 0; i < activity_stats_.size(); i++) {
        if (activity_stats_[i].count_start > count_start) {
            count_start = activity_stats_[i].count_start;
            top_start = i;
        }
    }

    // top_end activity has to be different from top start.
    uint32_t top_end = 0;
    size_t count_end = 0;
    for (int i = 0; i < activity_stats_.size(); i++) {
        if (activity_stats_[i].count_end > count_end && i != top_start) {
            count_end = activity_stats_[i].count_end;
            top_end = i;
        }
    }

    // Note: if there is a single activity in the data we will get
    // start = 0, end = 0 and will return the top variant.

    int happy = 0; // default happy is top freq variant.
    for (int i = 0; i < sorted.size(); i++) {
        const auto& v = sorted[i]->first.data;
        if (v.empty()) {
            continue;
        }
        if (v[0] == top_start && v[v.size() - 1] == top_end) {
            happy = i;
            break;
        }
    }

    return happy;
}

int VariantStatsFinalizer::compute_top_variants(std::vector<VList>& activity_top_variants, VRef& happy) const {
    if (variant_map_.empty() || activity_map_.empty()) {
        // No data.
        return 0;
    }

    // 1. sort variants by count
    std::vector<VRef> v_count(variant_map_.size());
    int i = 0;
    for (auto it = variant_map_.cbegin(); it != variant_map_.cend(); it++) {
        v_count[i++] = it;
    }
    std::sort(v_count.begin(), v_count.end(),
              [](const VRef& lhs, const VRef& rhs) { return lhs->second > rhs->second; });

    // 2. find top-10 variants for each activity
    // Make sure we get enough variants so that each activity has 10 entries

    activity_top_variants.resize(activity_map_.size());
    for (int i = 0; i < activity_top_variants.size(); i++) {
        activity_top_variants[i].reserve(10);
    }

    std::vector<int8_t> a_done(activity_map_.size(), 0);
    int done_count = 0;

    for (int i = 0; i < v_count.size(); i++) {
        // Check activities matched by this variant.
        const auto& variant = v_count[i]->first;
        std::vector<int8_t> a_seen(activity_map_.size(), 0);
        for (int j = 0; j < variant.data.size(); j++) {
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
        if (done_count == activity_map_.size()) {
            break;
        }
    }
    int happy_v = compute_happy_variant(v_count);
    happy = v_count[happy_v];
    return variant_map_.size();
}

std::string VariantStatsFinalizer::json_string(std::vector<VList>& activity_top_variants, VRef& happy) const {
    rapidjson::Document d;
    rapidjson::Document::AllocatorType& allocator = d.GetAllocator();
    d.SetObject();

    // Dictionary
    rapidjson::Value dict(rapidjson::kArrayType);
    for (auto it = activity_map_.begin(); it != activity_map_.end(); it++) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("id", it->second, allocator);
        rapidjson::Value s(rapidjson::kObjectType);
        obj.AddMember("name", rapidjson::Value().SetString(it->first.data, it->first.size, allocator), allocator);
        dict.PushBack(obj, allocator);
    }
    d.AddMember("dict", dict, allocator);

    // Activity stats
    rapidjson::Value a_stats(rapidjson::kArrayType);
    for (int i = 0; i < activity_stats_.size(); i++) {
        rapidjson::Value obj = activity_stats_[i].to_json(allocator);
        obj.AddMember("id", i, allocator);
        a_stats.PushBack(obj, allocator);
    }
    d.AddMember("a_stats", a_stats, allocator);

    // Edge stats
    rapidjson::Value e_stats(rapidjson::kArrayType);
    for (auto it = edge_map_.begin(); it != edge_map_.end(); it++) {
        rapidjson::Value obj = it->second.to_json(allocator);
        obj.AddMember("src", it->first.src, allocator);
        obj.AddMember("dst", it->first.dst, allocator);
        e_stats.PushBack(obj, allocator);
    }
    d.AddMember("e_stats", e_stats, allocator);

    // Variants
    rapidjson::Value topv(rapidjson::kArrayType);
    for (int i = 0; i < activity_top_variants.size(); i++) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("id", i, allocator);
        rapidjson::Value a(rapidjson::kArrayType);
        for (int j = 0; j < activity_top_variants[i].size(); j++) {
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

    // Encode to string.
    rapidjson::StringBuffer buf;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return buf.GetString();
}

std::string VariantStatsFinalizer::finalize() {
    std::vector<VList> activity_top_variants;
    std::vector<size_t> a_lastseen(activity_map_.size());
    std::map<std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>> edge_stats;
    for (const auto& [variant, count] : variant_map_) {
        EdgeHashSet e_seen;
        for (int i = 0; i < variant.data.size(); i++) {
            auto activity_id = variant.data[i];
            ActivityStats& a_stats = activity_stats_[activity_id];
            a_stats.count += count;
            if (a_lastseen[activity_id] != variant.hash) {
                a_stats.count_case += count;
                a_lastseen[activity_id] = variant.hash;
            }
            if (i == 0) {
                a_stats.count_start += count;
            }
            if (i == variant.data.size() - 1) {
                a_stats.count_end += count;
            }
            if (i > 0) {
                Edge e(variant.data[i - 1], activity_id);
                auto& e_stats = edge_map_[e];
                e_stats.count += count;
                auto e_it = e_seen.find(e, e.hash);
                if (e_it == e_seen.end()) {
                    e_stats.count_case += count;
                    e_seen.insert(e);
                }
            }
        }
    }
    // Add the variant into the variant_map.
    VRef happy;
    int num_variants = compute_top_variants(activity_top_variants, happy);
    if (num_variants == 0) {
        return "{}";
    }
    return json_string(activity_top_variants, happy);
}

} // namespace starrocks
