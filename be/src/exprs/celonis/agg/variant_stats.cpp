#include "variant_stats.h"

#include <queue>

#include "column/column_helper.h"
#include "exprs/celonis/agg/util.h"
#include "modules/query/variantstats.pb.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"

namespace starrocks {

int VariantStatsFinalizer::compute_happy_variant(const std::vector<VRef>& sorted) const {
    // Happy path
    // find top start activity
    // find top end activity which is not top start
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

void VariantStatsFinalizer::compute_top_variants(std::vector<VList>& activity_top_variants, VRef& happy,
                                                 const std::string& query_id) const {
    if (variant_map_.empty() || activity_map_.empty()) {
        // No data.
        return;
    }

    // 1. sort variants by count
    std::vector<VRef> v_count(variant_map_.size());
    int index = 0;
    for (auto it = variant_map_.cbegin(); it != variant_map_.cend(); it++) {
        v_count[index++] = it;
    }
    LOG(INFO) << get_log_prefix(query_id) << ": started variants sorting\n";
    std::sort(v_count.begin(), v_count.end(),
              [](const VRef& lhs, const VRef& rhs) { return lhs->second > rhs->second; });
    LOG(INFO) << get_log_prefix(query_id) << ": done variants sorting (variant_map_ size = " << variant_map_.size()
              << ")\n";

    // 2. find a happy variant
    int happy_v = compute_happy_variant(v_count);
    happy = v_count[happy_v];

    if (disable_top_variant_stats_) {
        return;
    }

    // 3. find top-10 variants for each activity
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
}

std::optional<std::string>
VariantStatsFinalizer::json_string(const std::vector<VList>& activity_top_variants, const VRef& happy) const {
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
        auto it = edge_map_.find({i, i});
        if (it != edge_map_.end()) {
            obj.AddMember("self_loop_count_case", it->second.count_case, allocator);
        }
        a_stats.PushBack(obj, allocator);
    }
    d.AddMember("a_stats", a_stats, allocator);

    // Edge stats
    rapidjson::Value e_stats(rapidjson::kArrayType);
    if (edge_count_ >= 0) {
        d.AddMember("e_count", edge_map_.size(), allocator);
        std::map<Slice, int32_t> ordered_activity_map(activity_map_.begin(), activity_map_.end());
        std::vector<int32_t> activity_unorderd_to_ordered(activity_map_.size());
        int index = 0;
        for (auto it = ordered_activity_map.begin(); it != ordered_activity_map.end(); ++it, ++index) {
            DCHECK_LT(it->second, activity_unorderd_to_ordered.size());
            activity_unorderd_to_ordered[it->second] = index;
        }
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
        std::priority_queue<EdgeOrderedID, std::vector<EdgeOrderedID>, CmpOnEdgeOrderedID> pq;
        for (auto it = edge_map_.cbegin(); it != edge_map_.cend(); ++it) {
            pq.push({activity_unorderd_to_ordered[it->first.src], activity_unorderd_to_ordered[it->first.dst], it});
            if (pq.size() > edge_count_) {
                pq.pop();
            }
        }
        // Pop first to list them in reverse sorted order
        std::vector<EdgeHashMap::const_iterator> popped;
        popped.reserve(pq.size());
        while (!pq.empty()) {
            popped.push_back(pq.top().it);
            pq.pop();
        }
        for (auto rit = popped.rbegin(); rit != popped.rend(); ++rit) {
            rapidjson::Value obj = (*rit)->second.to_json(allocator);
            obj.AddMember("src", (*rit)->first.src, allocator);
            obj.AddMember("dst", (*rit)->first.dst, allocator);
            e_stats.PushBack(obj, allocator);
        }
    }
    d.AddMember("e_stats", e_stats, allocator);

    // Variants
    if (!disable_top_variant_stats_) {
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
    }

    // Happy path
    size_t happy_count = happy->second;
    rapidjson::Value happy_obj(rapidjson::kObjectType);
    rapidjson::Value happy_var = happy->first.to_json(allocator);
    happy_obj.AddMember("variant", happy_var, allocator);
    happy_obj.AddMember("count", happy_count, allocator);
    d.AddMember("happy", happy_obj, allocator);

    // Encode to string.
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return buf.GetString();
}

std::optional<std::string>
VariantStatsFinalizer::base64_encoded_string(const std::vector<VList>& activity_top_variants, const VRef& happy,
                                             const std::string& query_id) const {
    celonis::accelerator::Statistics statistics_proto;
    // construct proto
    // Dictionary
    for (auto it = activity_map_.begin(); it != activity_map_.end(); it++) {
        celonis::accelerator::DictionaryEntry entry;
        entry.set_id(it->second);
        entry.set_name(std::string(it->first.data, it->first.size));
        *statistics_proto.add_dict() = entry;
    }
    // Activity stats
    for (int i = 0; i < activity_stats_.size(); i++) {
        celonis::accelerator::ActivityStatsEntry entry;
        const auto as = activity_stats_[i];
        entry.set_id(i);
        entry.set_count(as.count);
        entry.set_count_case(as.count_case);
        entry.set_count_start(as.count_start);
        entry.set_count_end(as.count_end);
        auto it = edge_map_.find({i, i});
        if (it != edge_map_.end()) {
            entry.set_self_loop_count_case(it->second.count_case);
        }
        *statistics_proto.add_a_stats() = entry;
    }
    // Edge stats
    if (edge_count_ >= 0) {
        statistics_proto.set_e_count(edge_map_.size());
        std::map<Slice, int32_t> ordered_activity_map(activity_map_.begin(), activity_map_.end());
        std::vector<int32_t> activity_unorderd_to_ordered(activity_map_.size());
        int index = 0;
        for (auto it = ordered_activity_map.begin(); it != ordered_activity_map.end(); ++it, ++index) {
            DCHECK_LT(it->second, activity_unorderd_to_ordered.size());
            activity_unorderd_to_ordered[it->second] = index;
        }
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
        std::priority_queue<EdgeOrderedID, std::vector<EdgeOrderedID>, CmpOnEdgeOrderedID> pq;
        for (auto it = edge_map_.cbegin(); it != edge_map_.cend(); ++it) {
            pq.push({activity_unorderd_to_ordered[it->first.src], activity_unorderd_to_ordered[it->first.dst], it});
            if (pq.size() > edge_count_) {
                pq.pop();
            }
        }
        // Pop first to list them in reverse sorted order
        std::vector<EdgeHashMap::const_iterator> popped;
        popped.reserve(pq.size());
        while (!pq.empty()) {
            popped.push_back(pq.top().it);
            pq.pop();
        }
        for (auto rit = popped.rbegin(); rit != popped.rend(); ++rit) {
            celonis::accelerator::EdgeStatsEntry entry;
            entry.set_count((*rit)->second.count);
            entry.set_count_case((*rit)->second.count_case);
            entry.set_src((*rit)->first.src);
            entry.set_dst((*rit)->first.dst);
            *statistics_proto.add_e_stats() = entry;
        }
    }
    // Variants
    if (!disable_top_variant_stats_) {
        uint32_t total_variants = 0;
        for (int i = 0; i < activity_top_variants.size(); i++) {
            celonis::accelerator::VariantEntry entry;
            entry.set_id(i);
            for (int j = 0; j < activity_top_variants[i].size(); j++) {
                celonis::accelerator::VariantCountPair count_pair;
                size_t count = activity_top_variants[i][j]->second;
                count_pair.set_count(count);
                const auto& data = activity_top_variants[i][j]->first.data;
                for (int k = 0; k < data.size(); ++k) {
                    ++total_variants;
                    count_pair.add_variant(data[k]);
                }
                *entry.add_top() = count_pair;
            }
            *statistics_proto.add_top() = entry;
        }
        LOG(INFO) << get_log_prefix(query_id) << ": total number of top variants = " << total_variants << "\n";
    }
    // Happy path
    celonis::accelerator::VariantCountPair count_pair;
    size_t happy_count = happy->second;
    count_pair.set_count(happy_count);
    const auto& data = happy->first.data;
    for (int i = 0; i < data.size(); i++) {
        count_pair.add_variant(data[i]);
    }
    *statistics_proto.mutable_happy() = count_pair;

    // set size limit to 100M.
    std::optional<std::string> encoded_string = to_base64_encoded_string(statistics_proto, (100LL << 20), false);
    if (!encoded_string.has_value()) {
        LOG(ERROR) << "CELONIS_VARIANT_STATS: proto serialized size exceeds maximum supported length (100M).\n";
    }
    return encoded_string;
}

std::optional<std::string>
VariantStatsFinalizer::to_string(const std::vector<VList>& activity_top_variants, const VRef& happy,
                                 const std::string& query_id) const {
    if (enable_proto_encoding_) {
        return base64_encoded_string(activity_top_variants, happy, query_id);
    } else {
        return json_string(activity_top_variants, happy);
    }
}

std::string VariantStatsFinalizer::get_log_prefix(const std::string& query_id) const {
    return "CELONIS_VARIANT_STATS (" + query_id + ")";
}

std::optional<std::string> VariantStatsFinalizer::finalize(FunctionContext* ctx) {
    const std::string query_id = print_id(ctx->state()->query_id());
    const std::string log_prefix = get_log_prefix(query_id);
    LOG(INFO) << log_prefix << ": merging_seconds = " << merging_microseconds_ / 1000000.0 << " seconds." << std::endl;
    LOG(INFO) << log_prefix << ": merging_bytes = " << merging_bytes_ << " bytes." << std::endl;
    LOG(INFO) << log_prefix << ": number of states merged = " << merging_states_ << std::endl;
    if (activity_map_.size() > std::numeric_limits<int16_t>::max()) {
        ctx->set_error(std::string(
                               "CELONIS_VARIANT_STATS: the size of activity_map is " + std::to_string(activity_map_.size()) +
                               " which is greater than the limit " +
                               std::to_string(std::numeric_limits<int16_t>::max()))
                               .c_str(),
                       false);
        return std::nullopt;
    }
    if (variant_map_.empty() || activity_map_.empty()) {
        return enable_proto_encoding_ ? "" : "{}";
    }

    std::vector<size_t> a_lastseen(activity_map_.size());
    LOG(INFO) << log_prefix << ": started traversing variant_map_ (length = " << variant_map_.size() << ")\n";
    for (const auto& [variant, count]: variant_map_) {
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
            if (i > 0 && (edge_count_ >= 0 || variant.data[i - 1] == activity_id)) {
                Edge e(variant.data[i - 1], activity_id);
                auto& e_stats = edge_map_[e];
                e_stats.count += count;
                if (e_stats.last_variant != &variant) {
                    e_stats.last_variant = &variant;
                    e_stats.count_case += count;
                }
            }
        }
    }
    if (UNLIKELY(ctx->state()->cancelled_ref())) {
        ctx->set_error("variant_stats detects cancelled.", false);
        return std::nullopt;
    }
    LOG(INFO) << log_prefix << ": done traversing variant_map\n";
    LOG(INFO) << log_prefix << ": size of activity_stats_ = " << activity_stats_.size() << "\n";
    LOG(INFO) << log_prefix << ": size of edge_map_ = " << edge_map_.size() << "\n";
    LOG(INFO) << log_prefix << ": started finding top\n";
    std::vector<VList> activity_top_variants;
    VRef happy;
    compute_top_variants(activity_top_variants, happy, query_id);
    LOG(INFO) << log_prefix << ": done finding top (activity_top_variants size = "
              << activity_top_variants.size()
              << ")\n";
    LOG(INFO) << log_prefix << ": started to_string\n";
    auto rv = to_string(activity_top_variants, happy, query_id);
    if (rv.has_value()) {
        LOG(INFO) << log_prefix << ": done to_string (length = " << rv->size() << ")\n";
    } else {
        ctx->set_error(std::string("CELONIS_VARIANT_STATS: output string size exceeds the limit (100M)").c_str(),
                       false);
    }
    return rv;
}

} // namespace starrocks
