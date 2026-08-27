#include "variant_stats_v2.h"

#include <chrono>
#include <limits>
#include <stack>

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
#include "util/defer_op.h"

namespace starrocks {

size_t CelonisVariantStatsAggregateV2State::compute_happy_variant(const std::vector<size_t>& sorted) const {
    // Happy path
    // find top start activity
    // find top end activity which is not top start
    // find top variant with start and end from above
    // if not found use top variant

    int top_start = 0;
    size_t count_start = 0;
    // Note that the below implementation tries to find the first start_activity with the largest count. It is possible
    // that there are other activities with the largest count.
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
    size_t happy = 0; // default happy is top freq variant.
    for (size_t i = 0; i < sorted.size(); i++) {
        auto [lo, hi] = get_offsets(sorted[i]);
        if (lo == hi) {
            continue;
        }
        if (get_activity(lo) == top_start && get_activity(hi - 1) == top_end) {
            happy = i;
            break;
        }
    }

    return happy;
}

CelonisVariantStatsAggregateV2State::VariantAnalysisResult CelonisVariantStatsAggregateV2State::analyze_variants()
        const {
    DCHECK(!no_activities() && !no_variants());
    VariantAnalysisResult result;
    auto& activity_top_variants = result.activity_top_variants;
    // 1. sort variants by count (from high to low)
    std::vector<size_t> sorted_indexes;
    sorted_indexes.reserve(counts_.size());
    for (size_t i = 0; i < counts_.size(); ++i) {
        sorted_indexes.push_back(i);
    }
    std::sort(sorted_indexes.begin(), sorted_indexes.end(),
              [&](size_t a, size_t b) { return counts_[a] > counts_[b]; });

    // 2. find a happy variant
    size_t happy_v = compute_happy_variant(sorted_indexes);
    result.happy = sorted_indexes[happy_v];
    // 3. find top-10 variants for each activity
    // Make sure we get enough variants so that each activity has 10 entries
    activity_top_variants.resize(activity_array_.size());
    for (int i = 0; i < activity_top_variants.size(); i++) {
        activity_top_variants[i].reserve(10);
    }

    std::vector<int8_t> a_done(activity_array_.size(), 0);
    int done_count = 0;

    for (int i = 0; i < sorted_indexes.size(); i++) {
        // Check activities matched by this variant.
        auto [lo, hi] = get_offsets(sorted_indexes[i]);
        std::vector<int8_t> a_seen(activity_array_.size(), 0);
        for (auto j = lo; j < hi; ++j) {
            auto idx = get_activity(j);
            if (a_done[idx] == 0 && a_seen[idx] == 0) {
                a_seen[idx] = 1;
                activity_top_variants[idx].push_back(sorted_indexes[i]);
                if (activity_top_variants[idx].size() >= 10) {
                    a_done[idx] = 1;
                    done_count++;
                }
            }
        }
        // Stop early if we have already collected 10 variants for every activity.
        if (done_count == activity_array_.size()) {
            break;
        }
    }
    return result;
}

std::optional<std::string> CelonisVariantStatsAggregateV2State::json_string(
        const VariantAnalysisResult& analysis_result) const {
    const std::vector<std::vector<size_t>>& activity_top_variants = analysis_result.activity_top_variants;
    size_t happy = analysis_result.happy;
    rapidjson::Document d;
    rapidjson::Document::AllocatorType& allocator = d.GetAllocator();
    d.SetObject();

    // Dictionary
    rapidjson::Value dict(rapidjson::kArrayType);
    for (auto i = 0; i < activity_array_.size(); ++i) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("id", i, allocator);
        rapidjson::Value s(rapidjson::kObjectType);
        std::string activity = activity_array_[i];
        obj.AddMember("name", rapidjson::Value().SetString(activity.data(), activity.size(), allocator), allocator);
        dict.PushBack(obj, allocator);
    }
    d.AddMember("dict", dict, allocator);

    // Activity stats
    rapidjson::Value a_stats(rapidjson::kArrayType);
    for (int i = 0; i < activity_stats_.size(); i++) {
        rapidjson::Value obj = activity_stats_[i].to_json(allocator);
        obj.AddMember("id", i, allocator);
        auto it = edge_stats_.find({i, i});
        if (it != edge_stats_.end()) {
            obj.AddMember("self_loop_count_case", it->second.count_case, allocator);
        }
        a_stats.PushBack(obj, allocator);
    }
    d.AddMember("a_stats", a_stats, allocator);

    // Edge stats
    rapidjson::Value e_stats(rapidjson::kArrayType);
    if (edge_count_ >= 0) {
        d.AddMember("e_count", edge_stats_.size(), allocator);
        auto sorted_edges = EdgeStatsProcessor<std::vector<std::string>>::get_sorted_edges(edge_stats_, activity_array_,
                                                                                           edge_count_);
        e_stats = EdgeStatsProcessor<std::vector<std::string>>::build_edge_stats_json(sorted_edges, allocator);
    }
    d.AddMember("e_stats", e_stats, allocator);
    // Top variants and happy path
    if (!skip_variant_analysis_) {
        rapidjson::Value topv(rapidjson::kArrayType);
        for (int i = 0; i < activity_top_variants.size(); i++) {
            rapidjson::Value obj(rapidjson::kObjectType);
            obj.AddMember("id", i, allocator);
            rapidjson::Value a(rapidjson::kArrayType);
            for (int j = 0; j < activity_top_variants[i].size(); j++) {
                rapidjson::Value var_obj(rapidjson::kObjectType);
                size_t count = counts_[activity_top_variants[i][j]];

                rapidjson::Value act(rapidjson::kArrayType);
                auto [lo, hi] = get_offsets(activity_top_variants[i][j]);
                for (size_t idx = lo; idx < hi; idx++) {
                    act.PushBack(get_activity(idx), allocator);
                }

                var_obj.AddMember("variant", act, allocator);
                var_obj.AddMember("count", count, allocator);
                a.PushBack(var_obj, allocator);
            }
            obj.AddMember("top", a, allocator);
            topv.PushBack(obj, allocator);
        }
        d.AddMember("top", topv, allocator);
        // Happy path
        if (happy >= 0 && happy < counts_.size()) {
            size_t happy_count = counts_[happy];
            rapidjson::Value happy_obj(rapidjson::kObjectType);
            rapidjson::Value happy_var(rapidjson::kArrayType);
            auto [lo, hi] = get_offsets(happy);
            for (size_t i = lo; i < hi; i++) {
                happy_var.PushBack(get_activity(i), allocator);
            }
            happy_obj.AddMember("variant", happy_var, allocator);
            happy_obj.AddMember("count", happy_count, allocator);
            d.AddMember("happy", happy_obj, allocator);
        }
    }

    // Encode to string.
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return buf.GetString();
}

std::optional<std::string> CelonisVariantStatsAggregateV2State::base64_encoded_string(
        const VariantAnalysisResult& analysis_result) const {
    const std::vector<std::vector<size_t>>& activity_top_variants = analysis_result.activity_top_variants;
    size_t happy = analysis_result.happy;
    celonis::accelerator::Statistics statistics_proto;
    // Dictionary
    for (auto i = 0; i < activity_array_.size(); ++i) {
        celonis::accelerator::DictionaryEntry entry;
        entry.set_id(i);
        entry.set_name(activity_array_[i]);
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
        auto it = edge_stats_.find({i, i});
        if (it != edge_stats_.end()) {
            entry.set_self_loop_count_case(it->second.count_case);
        }
        *statistics_proto.add_a_stats() = entry;
    }
    // Edge stats
    if (edge_count_ >= 0) {
        statistics_proto.set_e_count(edge_stats_.size());
        auto sorted_edges = EdgeStatsProcessor<std::vector<std::string>>::get_sorted_edges(edge_stats_, activity_array_,
                                                                                           edge_count_);
        EdgeStatsProcessor<std::vector<std::string>>::build_edge_stats_proto(sorted_edges, statistics_proto);
    }
    // Top variants and happy path
    if (!skip_variant_analysis_) {
        for (int i = 0; i < activity_top_variants.size(); i++) {
            celonis::accelerator::VariantEntry entry;
            entry.set_id(i);
            for (int j = 0; j < activity_top_variants[i].size(); j++) {
                celonis::accelerator::VariantCountPair count_pair;
                size_t count = counts_[activity_top_variants[i][j]];
                count_pair.set_count(count);
                auto [lo, hi] = get_offsets(activity_top_variants[i][j]);
                for (auto idx = lo; idx < hi; ++idx) {
                    count_pair.add_variant(get_activity(idx));
                }
                *entry.add_top() = count_pair;
            }
            *statistics_proto.add_top() = entry;
        }
        // Happy path
        if (happy >= 0 && happy < counts_.size()) {
            celonis::accelerator::VariantCountPair& happy_variant_with_count = *statistics_proto.mutable_happy();
            size_t happy_count = counts_[happy];
            happy_variant_with_count.set_count(happy_count);
            auto [lo, hi] = get_offsets(happy);
            for (auto i = lo; i < hi; ++i) {
                happy_variant_with_count.add_variant(get_activity(i));
            }
        }
    }

    const int64_t configured_size_limit = config::celonis_variant_stats_max_proto_size_bytes;
    const size_t size_limit =
            configured_size_limit > 0 ? static_cast<size_t>(configured_size_limit) : std::numeric_limits<size_t>::max();
    std::optional<std::string> encoded_string = to_base64_encoded_string(statistics_proto, size_limit, false);
    if (!encoded_string.has_value()) {
        LOG(ERROR) << "CELONIS_VARIANT_STATS_V2: proto serialized size " << statistics_proto.ByteSizeLong()
                   << " exceeds configured limit " << configured_size_limit << " bytes.\n";
    }
    return encoded_string;
}

std::optional<std::string> CelonisVariantStatsAggregateV2State::to_string(
        const VariantAnalysisResult& analysis_result) const {
    return enable_proto_encoding_ ? base64_encoded_string(analysis_result) : json_string(analysis_result);
}

std::string CelonisVariantStateV2AggregationFunction::get_log_prefix(const std::string& query_id) const {
    return "CELONIS_VARIANT_STATS_V2 (" + query_id + ")";
}

void CelonisVariantStateV2AggregationFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr state,
                                                      size_t row_num) const {
    this->data(state).update(ctx, columns, row_num);
}

void CelonisVariantStateV2AggregationFunction::merge(FunctionContext* ctx, const Column* column,
                                                     AggDataPtr __restrict state, size_t row_num) const {
    // merge internal state with column[row_num]
    // the column type is binary
    if (column->is_null(row_num)) {
        return;
    }
    const auto* input_column = down_cast<const BinaryColumn*>(ColumnHelper::get_data_column(column));
    Slice slice = input_column->get_slice(row_num);
    this->data(state).deserialize_and_merge((const uint8_t*)slice.data, slice.size);
}

void CelonisVariantStateV2AggregationFunction::serialize_to_column(FunctionContext* ctx,
                                                                   ConstAggDataPtr __restrict state, Column* to) const {
    // append our serialized state to column "to"
    auto* column = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(to));
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
    }
    size_t old_size = column->get_bytes().size();
    size_t new_size = old_size + this->data(state).serialized_size();
    column->get_bytes().resize(new_size);
    this->data(state).serialize(column->get_bytes().data() + old_size);
    column->get_offset().emplace_back(new_size);
}

void CelonisVariantStateV2AggregationFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
                                                                           size_t chunk_size, ColumnPtr* dst) const {
    // Used for streaming aggregation. Not implemented.
    throw std::runtime_error("celonis_variant_stats_v2: convert_to_serialize_format not supported");
}

void CelonisVariantStateV2AggregationFunction::finalize_to_column(FunctionContext* ctx,
                                                                  ConstAggDataPtr __restrict state, Column* to) const {
    auto defer = DeferOp([&]() {
        if (ctx->has_error() && to != nullptr) {
            to->append_default();
        }
    });
    auto& state_impl = this->data(state);
    const std::string query_id = print_id(ctx->state()->query_id());
    const std::string log_prefix = get_log_prefix(query_id);
    LOG(INFO) << log_prefix << ": merging_seconds = " << state_impl.merging_microseconds() / 1000000.0 << " seconds."
              << std::endl;
    LOG(INFO) << log_prefix << ": merging_bytes = " << state_impl.merging_bytes() << " bytes." << std::endl;
    LOG(INFO) << log_prefix << ": number of states merged = " << state_impl.merging_states() << std::endl;
    LOG(INFO) << log_prefix << ": distinct activity count = " << state_impl.num_distinct_activities() << std::endl;
    LOG(INFO) << log_prefix << ": distinct variant count = " << state_impl.num_distinct_variants() << std::endl;

    if (state_impl.activity_array().size() > MAX_ALLOWED_NUM_DISTINCT_ACTIVITIES) {
        ctx->set_error(
                std::string("CELONIS_VARIANT_STATS_V2: the number of unique activities is " +
                            std::to_string(state_impl.activity_array().size()) + " which is greater than the limit " +
                            std::to_string(MAX_ALLOWED_NUM_DISTINCT_ACTIVITIES))
                        .c_str(),
                false);
        return;
    }
    std::string output = "";
    if (state_impl.no_activities() || state_impl.no_variants()) {
        output = state_impl.enable_proto_encoding() ? "" : "{}";
        down_cast<BinaryColumn*>(to)->append(output);
        return;
    }
    if (UNLIKELY(ctx->state()->cancelled_ref())) {
        ctx->set_error("variant_stats_v2 detects cancelled.", false);
        return;
    }
    CelonisVariantStatsAggregateV2State::VariantAnalysisResult variant_analysis;
    if (!state_impl.skip_variant_analysis()) {
        LOG(INFO) << log_prefix << ": started analyzing variants\n";
        variant_analysis = state_impl.analyze_variants();
        LOG(INFO) << log_prefix << ": done analyzing variants (activity_top_variants size = "
                  << variant_analysis.activity_top_variants.size() << ")\n";
    }
    LOG(INFO) << log_prefix << ": started to_string\n";
    auto rv = state_impl.to_string(variant_analysis);
    if (rv.has_value()) {
        LOG(INFO) << log_prefix << ": done to_string (length = " << rv->size() << ")\n";
        output = rv.value();
    } else {
        ctx->set_error(std::string("CELONIS_VARIANT_STATS_V2: output protobuf size exceeds "
                                   "celonis_variant_stats_max_proto_size_bytes (" +
                                   std::to_string(config::celonis_variant_stats_max_proto_size_bytes) + " bytes)")
                               .c_str(),
                       false);
        return;
    }
    down_cast<BinaryColumn*>(to)->append(output);
}

std::string CelonisVariantStateV2AggregationFunction::get_name() const {
    return "celonis_variant_stats_v2";
}

} // namespace starrocks
