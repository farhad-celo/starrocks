#include "exprs/celonis/agg/variant_stats_utils.h"

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

} // namespace starrocks

