#include "graph.h"

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/celonis/agg/util.h"
#include "exprs/celonis/agg/variant_stats_edge_utils.h"
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
//         Start of CelonisGraphAggregateState
// ********************************************************************************

void CelonisGraphAggregateState::update(FunctionContext* ctx, const Column** columns, size_t row_num) {
    // Expected input: [ variant_column, count_column [, edge_count [, enable_proto_encoding ] ] ]
    // Pass a constant column with value 1 to ignore count.
    // NULL and NullableColumn are handled by NullableAggregateFunctionVariadic.

    // Update config arguments
    if (ctx->is_notnull_constant_column(2)) {
        edge_count_ = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(2));
    }
    if (ctx->is_notnull_constant_column(3)) {
        enable_proto_encoding_ = ColumnHelper::get_const_value<TYPE_BOOLEAN>(ctx->get_constant_column(3));
    }

    // Update activity/edge stats
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
    size_t mem = 0;
    size_t n = c_offset[row_num + 1] - c_offset[row_num];

    // Using vector<uint8_t> instead of vector<bool> for performance reasons. Logically it's a vector of bools.
    std::vector<uint8_t> activity_updated_count_cases(activity_map_.size(), 0);
    phmap::flat_hash_set<Edge, HashOnEdge, EqualOnEdge> edge_updated_count_cases;
    int32_t prev_activity_id = -1;
    int32_t start_activity_id = -1;
    int32_t end_activity_id = -1;
    for (size_t i = 0; i < n; i++) {
        size_t offset = c_offset[row_num] + i;
        if (activity_nulls != nullptr && (*activity_nulls)[offset]) {
            // Note: if we have a, null, b
            // we will consider that a,b form an edge.
            continue;
        }
        int32_t activity_id =
                maybe_add_activity(activity_map_, b_elements->get_slice(offset), ctx->mem_pool(), &mem).first;
        activity_stats_.resize(activity_map_.size());
        activity_updated_count_cases.resize(activity_map_.size(), 0);
        ActivityStats& a_stats = activity_stats_[activity_id];
        a_stats.count += count;
        if (activity_updated_count_cases[activity_id] == 0) {
            activity_updated_count_cases[activity_id] = 1;
            a_stats.count_case += count;
        }
        if (prev_activity_id >= 0) {
            Edge e(prev_activity_id, activity_id);
            auto& e_stats = edge_stats_[e];
            e_stats.count += count;
            auto [it, inserted] = edge_updated_count_cases.insert(e);
            if (inserted) {
                e_stats.count_case += count;
            }
        }
        prev_activity_id = activity_id;

        if (start_activity_id == -1) {
            start_activity_id = activity_id;
        }
        end_activity_id = activity_id;
    }

    if (start_activity_id != -1) {
        activity_stats_[start_activity_id].count_start += count;
    }

    if (end_activity_id != -1) {
        activity_stats_[end_activity_id].count_end += count;
    }

    ctx->add_mem_usage(mem);
}

void CelonisGraphAggregateState::serialize(uint8_t* dst) const {
    // Serialization format:
    //
    // Activity Dictionary:
    // - `uint32_t num_activities`: The number of unique activities.
    // - For each activity (loop `num_activities` times):
    // - `uint32_t idx`: The unique identifier (index) for the activity.
    // - `uint32_t size`: The length of the activity name string in bytes.
    // - `char[] activity_name`: The raw bytes of the activity name string.
    //
    // Activity Statistics:
    // - For each activity (loop `num_activities` times):
    // - `size_t count`: The total number of occurrences of this activity.
    // - `size_t count_case`: The number of cases in which this activity appears.
    // - `size_t count_start`: The number of times this activity is the first event in a case.
    // - `size_t count_end`: The number of times this activity is the last event in a case.
    //
    // Edge Statistics:
    // - `size_t num_edges`: The number of unique edges.
    // - For each edge (loop `num_edges` times):
    // - `int32_t src`: The index of the source activity.
    // - `int32_t dst`: The index of the destination activity.
    // - `size_t count`: The total number of times this edge occurs.
    // - `size_t count_case`: The number of cases in which this edge appears.
    //
    // Configurations Data:
    // - `int64_t edge_count_`: An additional count, likely for total edges.
    // - `uint8_t enable_proto_encoding_`: A boolean flag (1 or 0) indicating whether Protobuf encoding is enabled.

    DCHECK_EQ(activity_map_.size(), activity_stats_.size());

    // Activity Dictionary
    dst = serialize_activity_map(dst, activity_map_);

    // Activity Statistics
    size_t num_activities = activity_map_.size();
    memcpy(dst, &num_activities, sizeof(size_t));
    dst += sizeof(size_t);
    for (const auto& stats : activity_stats_) {
        memcpy(dst, &stats.count, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &stats.count_case, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &stats.count_start, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &stats.count_end, sizeof(size_t));
        dst += sizeof(size_t);
    }

    // Edge Statistics
    size_t num_edges = edge_stats_.size();
    memcpy(dst, &num_edges, sizeof(size_t));
    dst += sizeof(size_t);
    for (const auto& p : edge_stats_) {
        const auto& edge = p.first;
        const auto& stats = p.second;
        memcpy(dst, &edge.src, sizeof(int32_t));
        dst += sizeof(int32_t);
        memcpy(dst, &edge.dst, sizeof(int32_t));
        dst += sizeof(int32_t);
        memcpy(dst, &stats.count, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &stats.count_case, sizeof(size_t));
        dst += sizeof(size_t);
    }

    // Configurations Data
    memcpy(dst, &edge_count_, sizeof(int64_t));
    dst += sizeof(int64_t);
    *dst++ = static_cast<uint8_t>(enable_proto_encoding_);
}

size_t CelonisGraphAggregateState::serialized_size() const {
    size_t result = get_serialized_size(activity_map_);

    // activity_stats
    result += sizeof(size_t); // num_activities
    result += 4 * sizeof(size_t) * activity_stats_.size();

    // edge_stats
    result += sizeof(size_t); // num_edges
    result += (2 * sizeof(int32_t) + 2 * sizeof(size_t)) * edge_stats_.size();

    result += sizeof(int64_t); // edge_count_
    result += sizeof(uint8_t); // enable_proto_encoding_

    return result;
}

size_t CelonisGraphAggregateState::deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len) {
    auto start_time = std::chrono::high_resolution_clock::now();
    merging_bytes_ += len;
    ++merging_states_;

    // read from src and merge with existing state.
    size_t mem = 0;
    const uint8_t* end = src + len;

    std::vector<std::pair<int32_t, size_t>> index_vector;
    src = deserialize_activity_map_and_merge(src, index_vector, activity_map_, mem_pool, &mem);
    activity_stats_.resize(activity_map_.size());

    // read activity_stats and merge it with existing stats
    size_t num_activities;
    memcpy(&num_activities, src, sizeof(size_t));
    src += sizeof(size_t);
    for (size_t i = 0; i < num_activities; ++i) {
        int32_t local_idx = index_vector[i].first;
        auto& stats = activity_stats_[local_idx];
        // [activity_stats] count, count_case, count_start, count_end
        size_t count = 0;
        size_t count_case = 0;
        size_t count_start = 0;
        size_t count_end = 0;
        memcpy(&count, src, sizeof(size_t));
        src += sizeof(size_t);
        memcpy(&count_case, src, sizeof(size_t));
        src += sizeof(size_t);
        memcpy(&count_start, src, sizeof(size_t));
        src += sizeof(size_t);
        memcpy(&count_end, src, sizeof(size_t));
        src += sizeof(size_t);
        stats.count += count;
        stats.count_case += count_case;
        stats.count_start += count_start;
        stats.count_end += count_end;
    }

    // read edge_stats and merge it with existing stats
    size_t num_edges = 0;
    memcpy(&num_edges, src, sizeof(size_t));
    src += sizeof(size_t);
    for (size_t i = 0; i < num_edges; ++i) {
        // [edge_stats] src1, dst1, count, count_case
        int32_t edge_src, edge_dst;
        memcpy(&edge_src, src, sizeof(int32_t));
        src += sizeof(int32_t);
        memcpy(&edge_dst, src, sizeof(int32_t));
        src += sizeof(int32_t);

        int32_t local_src = index_vector[edge_src].first;
        int32_t local_dst = index_vector[edge_dst].first;
        Edge e(local_src, local_dst);
        auto& stats = edge_stats_[e];

        size_t count = 0;
        size_t count_case = 0;
        memcpy(&count, src, sizeof(size_t));
        src += sizeof(size_t);
        memcpy(&count_case, src, sizeof(size_t));
        src += sizeof(size_t);

        stats.count += count;
        stats.count_case += count_case;
    }

    memcpy(&edge_count_, src, sizeof(int64_t));
    src += sizeof(int64_t);
    memcpy(&enable_proto_encoding_, src, sizeof(uint8_t));
    src += sizeof(uint8_t);

    DCHECK_EQ(src, end);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    merging_microseconds_ += duration.count();
    return mem;
}

void CelonisGraphAggregateState::finalize_to_column(FunctionContext* ctx, Column* to) const {
    DCHECK_EQ(activity_map_.size(), activity_stats_.size());

    auto defer = DeferOp([&]() {
        if (ctx->has_error() && to != nullptr) {
            to->append_default();
        }
    });
    const std::string query_id = print_id(ctx->state()->query_id());
    const std::string log_prefix = get_log_prefix(query_id);
    LOG(INFO) << log_prefix << ": merging_seconds = " << merging_microseconds_ / 1000000.0 << " seconds." << std::endl;
    LOG(INFO) << log_prefix << ": merging_bytes = " << merging_bytes_ << " bytes." << std::endl;
    LOG(INFO) << log_prefix << ": number of states merged = " << merging_states_ << std::endl;
    LOG(INFO) << log_prefix << ": distinct activity count = " << activity_map_.size() << std::endl;

    if (activity_map_.size() > MAX_ALLOWED_NUM_DISTINCT_ACTIVITIES) {
        ctx->set_error(
                std::string("CELONIS_GRAPH: the size of activity_map is " + std::to_string(activity_map_.size()) +
                            " which is greater than the limit " + std::to_string(MAX_ALLOWED_NUM_DISTINCT_ACTIVITIES))
                        .c_str(),
                false);
        return;
    }
    if (activity_map_.empty()) {
        down_cast<BinaryColumn*>(to)->append(enable_proto_encoding_ ? "" : "{}");
        return;
    }
    if (UNLIKELY(ctx->state()->cancelled_ref())) {
        ctx->set_error("graph detects cancelled.", false);
        return;
    }
    LOG(INFO) << log_prefix << ": started to_string\n";
    auto rv = enable_proto_encoding_ ? base64_encoded_string() : json_string();
    if (!rv.has_value()) {
        ctx->set_error(std::string("CELONIS_GRAPH: output string size exceeds the limit (100M)").c_str(), false);
        return;
    }
    LOG(INFO) << log_prefix << ": done to_string (length = " << rv->size() << ")\n";
    down_cast<BinaryColumn*>(to)->append(rv.value());
}

//TODO(xingyuan): extract code to an utils class
std::optional<std::string> CelonisGraphAggregateState::base64_encoded_string() const {
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
        const auto& as = activity_stats_[i];
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
        auto sorted_edges = EdgeStatsProcessor<SliceHashMap>::get_sorted_edges(edge_stats_, activity_map_, edge_count_);
        EdgeStatsProcessor<SliceHashMap>::build_edge_stats_proto(sorted_edges, statistics_proto);
    }
    std::optional<std::string> encoded_string = to_base64_encoded_string(statistics_proto, (100LL << 20), false);
    if (!encoded_string.has_value()) {
        LOG(ERROR) << "CELONIS_GRAPH: proto serialized size exceeds maximum supported length (100M).\n";
    }
    return encoded_string;
}

std::optional<std::string> CelonisGraphAggregateState::json_string() const {
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
        auto sorted_edges = EdgeStatsProcessor<SliceHashMap>::get_sorted_edges(edge_stats_, activity_map_, edge_count_);
        e_stats = EdgeStatsProcessor<SliceHashMap>::build_edge_stats_json(sorted_edges, allocator);
    }
    d.AddMember("e_stats", e_stats, allocator);

    // Encode to string.
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return buf.GetString();
}

std::string CelonisGraphAggregateState::get_log_prefix(const std::string& query_id) const {
    return "CELONIS_GRAPH (" + query_id + ")";
}

// ********************************************************************************
//         End of CelonisGraphAggregateState
// ********************************************************************************

// ********************************************************************************
//         Start of CelonisGraphAggregationFunction
// ********************************************************************************

void CelonisGraphAggregationFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                                             size_t row_num) const {
    this->data(state).update(ctx, columns, row_num);
}

void CelonisGraphAggregationFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                            size_t row_num) const {
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

void CelonisGraphAggregationFunction::serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                          Column* to) const {
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

void CelonisGraphAggregationFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
                                                                  size_t chunk_size, ColumnPtr* dst) const {
    // Used for streaming aggregation. Not implemented.
    throw std::runtime_error("celonis_graph: convert_to_serialize_format not supported");
}

void CelonisGraphAggregationFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                         Column* to) const {
    this->data(state).finalize_to_column(ctx, to);
}

std::string CelonisGraphAggregationFunction::get_name() const {
    return "celonis_graph";
}

// ********************************************************************************
//         End of CelonisGraphAggregationFunction
// ********************************************************************************

} // namespace starrocks
