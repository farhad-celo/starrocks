#include "variant_stats.h"

#include <limits>
#include <queue>

#include "column/column_helper.h"
#include "common/config.h"
#include "exprs/celonis/agg/util.h"
#include "exprs/celonis/agg/variant_stats_edge_utils.h"
#include "modules/query/variantstats.pb.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"

namespace starrocks {

std::optional<std::string> VariantStatsFinalizer::json_string(
        const std::optional<VariantAnalysisResult>& variant_analysis) const {
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
        auto sorted_edges = EdgeStatsProcessor<SliceHashMap>::get_sorted_edges(edge_map_, activity_map_, edge_count_);
        e_stats = EdgeStatsProcessor<SliceHashMap>::build_edge_stats_json(sorted_edges, allocator);
    }
    d.AddMember("e_stats", e_stats, allocator);
    // Top variants and happy path
    if (variant_analysis.has_value()) {
        build_variant_analysis_json(variant_analysis.value(), d, allocator);
    }
    // Encode to string.
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return buf.GetString();
}

std::optional<std::string> VariantStatsFinalizer::base64_encoded_string(
        const std::optional<VariantAnalysisResult>& variant_analysis, const std::string& query_id) const {
    celonis::accelerator::Statistics statistics_proto;
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
        auto sorted_edges = EdgeStatsProcessor<SliceHashMap>::get_sorted_edges(edge_map_, activity_map_, edge_count_);
        EdgeStatsProcessor<SliceHashMap>::build_edge_stats_proto(sorted_edges, statistics_proto);
    }
    // Top variants and happy path
    if (variant_analysis.has_value()) {
        build_variant_analysis_proto(variant_analysis.value(), statistics_proto, get_log_prefix(query_id));
    }
    const int64_t configured_size_limit = config::celonis_variant_stats_max_proto_size_bytes;
    const size_t size_limit =
            configured_size_limit > 0 ? static_cast<size_t>(configured_size_limit) : std::numeric_limits<size_t>::max();
    std::optional<std::string> encoded_string = to_base64_encoded_string(statistics_proto, size_limit, false);
    if (!encoded_string.has_value()) {
        LOG(ERROR) << "CELONIS_VARIANT_STATS: proto serialized size " << statistics_proto.ByteSizeLong()
                   << " exceeds configured limit " << configured_size_limit << " bytes.\n";
    }
    return encoded_string;
}

std::optional<std::string> VariantStatsFinalizer::to_string(const std::optional<VariantAnalysisResult>& analysis_result,
                                                            const std::string& query_id) const {
    return enable_proto_encoding_ ? base64_encoded_string(analysis_result, query_id) : json_string(analysis_result);
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
    if (activity_map_.size() > MAX_ALLOWED_NUM_DISTINCT_ACTIVITIES) {
        ctx->set_error(std::string("CELONIS_VARIANT_STATS: the size of activity_map is " +
                                   std::to_string(activity_map_.size()) + " which is greater than the limit " +
                                   std::to_string(MAX_ALLOWED_NUM_DISTINCT_ACTIVITIES))
                               .c_str(),
                       false);
        return std::nullopt;
    }
    if (variant_map_.empty() || activity_map_.empty()) {
        return enable_proto_encoding_ ? "" : "{}";
    }

    std::vector<size_t> a_lastseen(activity_map_.size());
    size_t total_count = 0;
    LOG(INFO) << log_prefix << ": started traversing variant_map (length = " << variant_map_.size() << ")\n";
    for (const auto& [variant, count] : variant_map_) {
        total_count += count;
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
    LOG(INFO) << log_prefix << ": total number of variants = " << total_count << "\n";
    LOG(INFO) << log_prefix << ": done traversing variant_map\n";
    LOG(INFO) << log_prefix << ": size of activity_stats = " << activity_stats_.size() << "\n";
    LOG(INFO) << log_prefix << ": size of edge_map = " << edge_map_.size() << "\n";
    std::optional<VariantAnalysisResult> variant_analysis = std::nullopt;
    if (!skip_variant_analysis_) {
        LOG(INFO) << log_prefix << ": started analyzing variants\n";
        variant_analysis =
                analyze_variants_for_explore_process(variant_map_, activity_map_, activity_stats_, log_prefix);
        LOG(INFO) << log_prefix << ": done analyzing variants (activity_top_variants size = "
                  << variant_analysis.value().activity_top_variants.size() << ")\n";
    }
    LOG(INFO) << log_prefix << ": started to_string\n";
    auto rv = to_string(variant_analysis, query_id);
    if (rv.has_value()) {
        LOG(INFO) << log_prefix << ": done to_string (length = " << rv->size() << ")\n";
    } else {
        ctx->set_error(std::string("CELONIS_VARIANT_STATS: output protobuf size exceeds "
                                   "celonis_variant_stats_max_proto_size_bytes (" +
                                   std::to_string(config::celonis_variant_stats_max_proto_size_bytes) + " bytes)")
                               .c_str(),
                       false);
    }
    return rv;
}

} // namespace starrocks
