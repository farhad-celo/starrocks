#include "explore_process.h"

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/celonis/agg/util.h"
#include "exprs/function_context.h"
#include "modules/query/variantstats.pb.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"
#include "util/defer_op.h"

namespace starrocks {

// ********************************************************************************
//         Start of CelonisExploreProcessAggregateState
// ********************************************************************************
void CelonisExploreProcessAggregateState::update(FunctionContext* ctx, const Column** columns, size_t row_num) {
    // Expected input: [ variant_column, count_column, min_variant_count_threshold_on_leaf, enable_proto_encoding ]
    // NULL and NullableColumn are handled by NullableAggregateFunctionVariadic.

    // Update config arguments
    min_variant_count_threshold_on_leaf_ = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(2));
    enable_proto_encoding_ = ColumnHelper::get_const_value<TYPE_BOOLEAN>(ctx->get_constant_column(3));

    int64_t count = 0;
    if (!columns[1]->is_constant()) {
        const auto& w_column = down_cast<const Int64Column&>(*columns[1]);
        count = w_column.get(row_num).get_int64();
    } else {
        const auto& w_column = down_cast<const ConstColumn&>(*columns[1]);
        count = w_column.get(0).get_int64();
    }

    const ArrayColumn& variant_column = down_cast<const ArrayColumn&>(*columns[0]);
    const UInt32Column::Container& c_offset = variant_column.offsets().get_data();

    const Column* activity_elements = &variant_column.elements();
    const NullColumn::Container* activity_nulls = nullptr;

    if (const NullableColumn* nc = dynamic_cast<const NullableColumn*>(activity_elements)) {
        activity_elements = nc->data_column().get();
        if (nc->has_null()) {
            activity_nulls = &(nc->null_column()->get_data());
        }
    }

    const BinaryColumn* b_elements = down_cast<const BinaryColumn*>(activity_elements);
    size_t memory = 0;
    size_t n = c_offset[row_num + 1] - c_offset[row_num];
    Variant variant(n);

    for (size_t i = 0; i < n; i++) {
        size_t offset = c_offset[row_num] + i;
        // Note: if we have a, null, b
        // we will consider that a,b form an edge.
        if (activity_nulls == nullptr || !(*activity_nulls)[offset]) {
            auto idx_hash = maybe_add_activity(b_elements->get_slice(offset), ctx->mem_pool(), activity_map_, &memory);
            variant.add(idx_hash.first, idx_hash.second);
        }
    }
    // Add the variant into the variant_map.
    variant_map_[variant] += count;

    ctx->add_mem_usage(memory);
}

void CelonisExploreProcessAggregateState::serialize_to_column(FunctionContext* ctx, Column* to) const {
    // The current implementation assumes that `activity_stats_` is empty when this function is called.
    // We explicitly enforce this assumption to be true. This is to prevent from returning wrong results given
    // unexpected changes in the engine which breaks the assumption.
    if (!activity_stats_.empty()) {
        ctx->set_error(std::string("CELONIS_EXPLORE_PROCESS: unexpected non-empty activity_stats_ during serialization")
                               .c_str(),
                       false);
        return;
    }
    // append our serialized state to column "to"
    auto* column = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(to));
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
    }

    auto activity_stats_pair = get_activity_stats_from_variant_map();

    const std::string log_prefix = get_log_prefix(ctx);
    const auto& variant_map = min_variant_count_threshold_on_leaf_ <= 1
                                      ? variant_map_
                                      : get_trimmed_variant_map(log_prefix, activity_stats_pair.first);

    size_t old_size = column->get_bytes().size();
    size_t new_size = old_size + serialized_size(variant_map, activity_stats_pair);
    column->get_bytes().resize(new_size);
    serialize(column->get_bytes().data() + old_size, variant_map, activity_stats_pair);
    column->get_offset().emplace_back(new_size);
}

size_t CelonisExploreProcessAggregateState::deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len) {
    auto start_time = std::chrono::steady_clock::now();
    merging_bytes_ += len;
    ++merging_states_;

    // read from src and merge with existing state.
    size_t mem = 0;
    const uint8_t* end = src + len;

    std::vector<std::pair<int32_t, size_t>> index_vector;
    src = deserialize_activity_map_and_merge(src, mem_pool, activity_map_, index_vector, &mem);
    src = deserialize_variant_map_and_merge(src, index_vector, variant_map_);
    activity_stats_.resize(activity_map_.size());
    activity_self_loop_count_case_.resize(activity_map_.size());

    // read activity_stats and merge it with existing stats
    size_t num_activities;
    memcpy(&num_activities, src, sizeof(size_t));
    src += sizeof(size_t);
    for (size_t i = 0; i < num_activities; ++i) {
        uint32_t local_idx = index_vector[i].first;
        auto& stats = activity_stats_[local_idx];
        size_t count = 0;
        size_t count_case = 0;
        size_t count_start = 0;
        size_t count_end = 0;
        size_t self_loop_count_case = 0;
        memcpy(&count, src, sizeof(size_t));
        src += sizeof(size_t);
        memcpy(&count_case, src, sizeof(size_t));
        src += sizeof(size_t);
        memcpy(&count_start, src, sizeof(size_t));
        src += sizeof(size_t);
        memcpy(&count_end, src, sizeof(size_t));
        src += sizeof(size_t);
        memcpy(&self_loop_count_case, src, sizeof(size_t));
        src += sizeof(size_t);
        stats.count += count;
        stats.count_case += count_case;
        stats.count_start += count_start;
        stats.count_end += count_end;
        activity_self_loop_count_case_[local_idx] += self_loop_count_case;
    }

    // Configuration Parameters
    memcpy(&min_variant_count_threshold_on_leaf_, src, sizeof(int64_t));
    src += sizeof(int64_t);
    memcpy(&enable_proto_encoding_, src, sizeof(uint8_t));
    src += sizeof(uint8_t);

    DCHECK_EQ(src, end);
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    merging_microseconds_ += duration.count();
    return mem;
}

void CelonisExploreProcessAggregateState::finalize_to_column(FunctionContext* ctx, Column* to) const {
    DCHECK_EQ(activity_map_.size(), activity_stats_.size());

    auto defer = DeferOp([&]() {
        if (ctx->has_error() && to != nullptr) {
            to->append_default();
        }
    });
    const std::string log_prefix = get_log_prefix(ctx);
    LOG(INFO) << log_prefix << ": merging_seconds = " << merging_microseconds_ / 1000000.0 << " seconds." << std::endl;
    LOG(INFO) << log_prefix << ": merging_bytes = " << merging_bytes_ << " bytes." << std::endl;
    LOG(INFO) << log_prefix << ": number of states merged = " << merging_states_ << std::endl;
    LOG(INFO) << log_prefix << ": distinct activity count = " << activity_map_.size() << std::endl;

    if (activity_map_.size() > MAX_ALLOWED_NUM_DISTINCT_ACTIVITIES) {
        ctx->set_error(std::string("CELONIS_EXPLORE_PROCESS: the size of activity_map is " +
                                   std::to_string(activity_map_.size()) + " which is greater than the limit " +
                                   std::to_string(MAX_ALLOWED_NUM_DISTINCT_ACTIVITIES))
                               .c_str(),
                       false);
        return;
    }
    if (activity_map_.empty()) {
        down_cast<BinaryColumn*>(to)->append(enable_proto_encoding_ ? "" : "{}");
        return;
    }

    if (UNLIKELY(ctx->state()->cancelled_ref())) {
        ctx->set_error("CELONIS_EXPLORE_PROCESS detects cancelled.", false);
        return;
    }

    LOG(INFO) << log_prefix << ": started analyzing variants\n";
    VariantAnalysisResult variant_analysis =
            analyze_variants_for_explore_process(variant_map_, activity_map_, activity_stats_, log_prefix);
    LOG(INFO) << log_prefix << ": done analyzing variants (activity_top_variants size = "
              << variant_analysis.activity_top_variants.size() << ")\n";

    LOG(INFO) << log_prefix << ": started to_string\n";
    auto rv = enable_proto_encoding_ ? base64_encoded_string(variant_analysis, log_prefix)
                                     : json_string(variant_analysis);
    if (!rv.has_value()) {
        return;
    }
    LOG(INFO) << log_prefix << ": done to_string (length = " << rv->size() << ")\n";
    down_cast<BinaryColumn*>(to)->append(rv.value());
}

std::pair<std::vector<ActivityStats>, std::vector<size_t>>
CelonisExploreProcessAggregateState::get_activity_stats_from_variant_map() const {
    size_t activity_size = activity_map_.size();
    std::vector<ActivityStats> activity_stats(activity_size);
    std::vector<size_t> activity_self_loop_count_case(activity_size);

    std::vector<size_t> activity_last_saw_variant(activity_size);
    std::vector<size_t> activity_self_loop_last_saw_variant(activity_size);

    for (const auto& [variant, count] : variant_map_) {
        if (variant.data.size() != 0) {
            activity_stats[variant.data.front()].count_start += count;
            activity_stats[variant.data.back()].count_end += count;
        }
        for (int i = 0; i < variant.data.size(); i++) {
            auto activity_id = variant.data[i];
            ActivityStats& a_stats = activity_stats[activity_id];
            a_stats.count += count;
            if (activity_last_saw_variant[activity_id] != variant.hash) {
                a_stats.count_case += count;
                activity_last_saw_variant[activity_id] = variant.hash;
            }
            if (i > 0 && variant.data[i - 1] == activity_id &&
                activity_self_loop_last_saw_variant[activity_id] != variant.hash) {
                activity_self_loop_count_case[activity_id] += count;
                activity_self_loop_last_saw_variant[activity_id] = variant.hash;
            }
        }
    }

    return {activity_stats, activity_self_loop_count_case};
}

VariantHashMap CelonisExploreProcessAggregateState::get_trimmed_variant_map(
        const std::string& log_prefix, const std::vector<ActivityStats>& activity_stats) const {
    auto time_start = std::chrono::steady_clock::now();

    std::vector<VRef> sorted_vrefs;
    sorted_vrefs.reserve(variant_map_.size());
    for (auto it = variant_map_.cbegin(); it != variant_map_.cend(); ++it) {
        sorted_vrefs.emplace_back(it);
    }

    std::ranges::sort(sorted_vrefs, [](const VRef& lhs, const VRef& rhs) {
        if (lhs->second != rhs->second) {
            return lhs->second < rhs->second;
        }
        return lhs->first.hash > rhs->first.hash;
    });

    auto time_sort_end = std::chrono::steady_clock::now();
    long long duration_sort_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(time_sort_end - time_start).count();
    LOG(INFO) << log_prefix << "get_trimmed_variant_map. Sorting takes " << duration_sort_ms << "ms.\n";

    VariantHashMap trimmed_variant_map;

    auto count_range = activity_stats | std::views::transform(&ActivityStats::count);
    std::vector<size_t> activity_remaining_count(begin(count_range), end(count_range));

    // Trim low-count variants (below threshold) unless they contain the last occurrence of any activity
    size_t i = 0;
    while (i < sorted_vrefs.size()) {
        size_t count = sorted_vrefs[i]->second;
        if (count >= min_variant_count_threshold_on_leaf_) {
            break;
        }
        bool keep = false;
        const auto& variant = sorted_vrefs[i]->first;
        for (const auto activity : variant.data) {
            DCHECK_GE(activity_remaining_count[activity], count);
            activity_remaining_count[activity] -= count;
            if (activity_remaining_count[activity] == 0) {
                keep = true;
            }
        }
        if (keep) {
            // Add back counts
            for (const auto activity : variant.data) {
                activity_remaining_count[activity] += count;
            }
            trimmed_variant_map[variant] = count;
        }
        i++;
    }
    while (i < sorted_vrefs.size()) {
        trimmed_variant_map[sorted_vrefs[i]->first] = sorted_vrefs[i]->second;
        i++;
    }

    auto time_trim_end = std::chrono::steady_clock::now();
    long long duration_trim_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(time_trim_end - time_sort_end).count();
    LOG(INFO) << log_prefix << "get_trimmed_variant_map. Trimming takes " << duration_trim_ms << "ms.\n";
    LOG(INFO) << log_prefix << "get_trimmed_variant_map. Trimmed " << variant_map_.size() - trimmed_variant_map.size()
              << " variants.\n";

    return trimmed_variant_map;
}

void CelonisExploreProcessAggregateState::serialize(
        uint8_t* dst, const VariantHashMap& variant_map,
        const std::pair<std::vector<ActivityStats>, std::vector<size_t>>& activity_stats_pair) const {
    const auto& activity_stats = activity_stats_pair.first;
    const auto& activity_self_loop_count_case = activity_stats_pair.second;

    DCHECK_EQ(activity_map_.size(), activity_stats.size());

    dst = serialize_activity_map(dst, activity_map_);
    dst = serialize_variant_map(dst, variant_map);

    // Activity Statistics
    size_t num_activities = activity_map_.size();
    memcpy(dst, &num_activities, sizeof(size_t));
    dst += sizeof(size_t);
    for (size_t i = 0; i < activity_stats.size(); ++i) {
        const auto& stats = activity_stats[i];
        memcpy(dst, &stats.count, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &stats.count_case, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &stats.count_start, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &stats.count_end, sizeof(size_t));
        dst += sizeof(size_t);
        size_t self_loop_count_case = activity_self_loop_count_case[i];
        memcpy(dst, &self_loop_count_case, sizeof(size_t));
        dst += sizeof(size_t);
    }

    // Configuration Parameters
    memcpy(dst, &min_variant_count_threshold_on_leaf_, sizeof(int64_t));
    dst += sizeof(int64_t);
    memcpy(dst, &enable_proto_encoding_, sizeof(uint8_t));
    dst += sizeof(uint8_t);
}

size_t CelonisExploreProcessAggregateState::serialized_size(
        const VariantHashMap& variant_map,
        const std::pair<std::vector<ActivityStats>, std::vector<size_t>>& activity_stats_pair) const {
    // Activities Dictionary and Variant Statistics
    size_t result = 0;
    result += get_serialized_size(activity_map_);
    result += get_serialized_size(variant_map);

    // activity_stats
    result += sizeof(size_t); // num_activities
    result += 5 * sizeof(size_t) *
              activity_stats_pair.first.size(); // Combined size of activity_stats_ and activity_self_loop_count_case_

    result += sizeof(int64_t); // min_variant_count_threshold_on_leaf_
    result += sizeof(uint8_t); // enable_proto_encoding_

    return result;
}

std::optional<std::string> CelonisExploreProcessAggregateState::base64_encoded_string(
        const VariantAnalysisResult& variant_analysis, const std::string& log_prefix) const {
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
        entry.set_self_loop_count_case(activity_self_loop_count_case_[i]);
        *statistics_proto.add_a_stats() = entry;
    }

    // Top variants and happy path
    build_variant_analysis_proto(variant_analysis, statistics_proto, log_prefix);

    // Limit size to 100M.
    std::optional<std::string> encoded_string =
            to_base64_encoded_string(statistics_proto, MAX_ALLOWED_PROTO_SERIALIZED_SIZE, false);
    if (!encoded_string.has_value()) {
        LOG(ERROR) << log_prefix
                   << "CELONIS_EXPLORE_PROCESS: proto serialized size exceeds maximum supported length (100M).\n";
    }
    return encoded_string;
}

std::optional<std::string> CelonisExploreProcessAggregateState::json_string(
        const VariantAnalysisResult& variant_analysis) const {
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
    for (size_t i = 0; i < activity_stats_.size(); i++) {
        rapidjson::Value obj = activity_stats_[i].to_json(allocator);
        obj.AddMember("id", i, allocator);
        obj.AddMember("self_loop_count_case", activity_self_loop_count_case_[i], allocator);
        a_stats.PushBack(obj, allocator);
    }
    d.AddMember("a_stats", a_stats, allocator);
    // Top variants and happy path
    build_variant_analysis_json(variant_analysis, d, allocator);

    // Encode to string.
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return buf.GetString();
}

std::string CelonisExploreProcessAggregateState::get_log_prefix(FunctionContext* ctx) {
    const std::string query_id = print_id(ctx->state()->query_id());
    return "CELONIS_EXPLORE_PROCESS (" + query_id + ")";
}
// ********************************************************************************
//         End of CelonisExploreProcessAggregateState
// ********************************************************************************

// ********************************************************************************
//         Start of CelonisExploreProcessAggregationFunction
// ********************************************************************************
void CelonisExploreProcessAggregationFunction::update(FunctionContext* ctx, const Column** columns,
                                                      AggDataPtr __restrict state, size_t row_num) const {
    this->data(state).update(ctx, columns, row_num);
}

void CelonisExploreProcessAggregationFunction::merge(FunctionContext* ctx, const Column* column,
                                                     AggDataPtr __restrict state, size_t row_num) const {
    // merge internal state with column[row_num]
    // the column type is binary
    if (column->is_null(row_num)) {
        return;
    }
    const auto* input_column = down_cast<const BinaryColumn*>(ColumnHelper::get_data_column(column));
    Slice slice = input_column->get_slice(row_num);
    size_t mem_usage = 0;
    mem_usage += this->data(state).deserialize_and_merge(ctx->mem_pool(), (const uint8_t*)slice.data, slice.size);
    ctx->add_mem_usage(mem_usage);
}

void CelonisExploreProcessAggregationFunction::serialize_to_column(FunctionContext* ctx __attribute__((unused)),
                                                                   ConstAggDataPtr __restrict state, Column* to) const {
    this->data(state).serialize_to_column(ctx, to);
}

void CelonisExploreProcessAggregationFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
                                                                           size_t chunk_size, ColumnPtr* dst) const {
    // Used for streaming aggregation. Not implemented.
    throw std::runtime_error("celonis_explore_process: convert_to_serialize_format not supported");
}

void CelonisExploreProcessAggregationFunction::finalize_to_column(FunctionContext* ctx __attribute__((unused)),
                                                                  ConstAggDataPtr __restrict state, Column* to) const {
    this->data(state).finalize_to_column(ctx, to);
}

std::string CelonisExploreProcessAggregationFunction::get_name() const {
    return "celonis_explore_process";
}
// ********************************************************************************
//         End of CelonisExploreProcessAggregationFunction
// ********************************************************************************
} // namespace starrocks