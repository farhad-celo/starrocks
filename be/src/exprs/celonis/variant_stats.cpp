#include "exprs/celonis/variant_stats.h"

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/const_column.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate.h"
#include "gutil/casts.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"
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
    ss << "src " << src << " dst " << dst << " hash " << hash;
    return ss.str();
}

void EdgeStats::merge(const EdgeStats& other) {
    count += other.count;
    count_case += other.count_case;
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

void EdgeStats::serialize(uint8_t* dst) const {
    memcpy(dst, &count, sizeof(size_t));
    dst += sizeof(size_t);
    memcpy(dst, &count_case, sizeof(size_t));
    dst += sizeof(size_t);
}

void EdgeStats::deserialize(const uint8_t* src) {
    memcpy(&count, src, sizeof(size_t));
    src += sizeof(size_t);
    memcpy(&count_case, src, sizeof(size_t));
    src += sizeof(size_t);
}


void ActivityStats::merge(const ActivityStats& other) {
    count += other.count;
    count_case += other.count_case;
    count_start += other.count_start;
    count_end += other.count_end;
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
    ss << "count " << count << " count_case " << count_case
       << " count_start " << count_start << " count_end " << count_end;
    return ss.str();
}

void ActivityStats::serialize(uint8_t* dst) const {
    memcpy(dst, &count, sizeof(size_t));
    dst += sizeof(size_t);
    memcpy(dst, &count_case, sizeof(size_t));
    dst += sizeof(size_t);
    memcpy(dst, &count_start, sizeof(size_t));
    dst += sizeof(size_t);
    memcpy(dst, &count_end, sizeof(size_t));
    dst += sizeof(size_t);
}

void ActivityStats::deserialize(const uint8_t* src) {
    memcpy(&count, src, sizeof(size_t));
    src += sizeof(size_t);
    memcpy(&count_case, src, sizeof(size_t));
    src += sizeof(size_t);
    memcpy(&count_start, src, sizeof(size_t));
    src += sizeof(size_t);
    memcpy(&count_end, src, sizeof(size_t));
    src += sizeof(size_t);
}

std::pair<int32_t, size_t>
VariantStatsState::maybe_add_activity(MemPool* mem_pool, const Slice& slice, size_t& memory) {
    int32_t index = 0;
    SliceWithHash key(slice);
    size_t hash = key.hash;

    auto it = activity_map.find(key, key.hash);
    if (it == activity_map.end()) {
        // New activity - allocate memory
        char* pos = (char*) mem_pool->allocate(key.size);
        DCHECK(pos != nullptr);
        memcpy(pos, key.data, key.size);
        memory += phmap::item_serialize_size<SliceHashMap>::value;
        key.data = pos;
        index = activity_map.size();
        activity_map.insert(std::pair<SliceWithHash, int32_t>(key, activity_map.size()));
        activity_stats.resize(activity_stats.size() + 1);
    } else {
        key.data = it->first.data;
        index = it->second;
    }
    return std::make_pair(index, hash);
}

size_t VariantStatsState::update(MemPool* mem_pool, const ArrayColumn& activity_column,
                                 size_t row_num, int64_t weight) {
    const UInt32Column::Container& c_offset = activity_column.offsets().get_data();
    const Column* activity_elements = &activity_column.elements();
    const NullableColumn* nc = dynamic_cast<const NullableColumn*>(activity_elements);
    const NullColumn::Container* activity_nulls = nullptr;

    if (activity_elements->has_null()) {
        activity_nulls = &(nc->null_column()->get_data());
    }

    if (nc != nullptr) {
        activity_elements = nc->data_column().get();
    }

    const BinaryColumn* b_elements = down_cast<const BinaryColumn*>(activity_elements);
    size_t memory = 0;
    size_t n = c_offset[row_num + 1] - c_offset[row_num];
    Variant variant(n);
    std::vector<int8_t> a_seen(n + activity_map.size(), 0);

    EdgeHashSet e_seen;
    std::pair<int32_t, size_t> prev_idx_hash;
    for (size_t i = 0; i < n; i++) {
        size_t offset = c_offset[row_num] + i;
        if (activity_nulls != nullptr && (*activity_nulls)[offset]) {
            // Note: if we have a, null, b
            // we will consider that a,b form an edge.
            continue;
        }
        auto idx_hash = maybe_add_activity(mem_pool,  b_elements->get_slice(offset),memory);
        variant.add(idx_hash.first, idx_hash.second);

        ActivityStats& a_stats = activity_stats[idx_hash.first];
        a_stats.count += weight;
        if (a_seen[idx_hash.first] == 0) {
            a_stats.count_case += weight;
            a_seen[idx_hash.first] = 1;
        }
        if (i == 0) {
            a_stats.count_start += weight;
        } else if (i == n - 1) {
            a_stats.count_end += weight;
        }

        if (i > 0) {
            Edge e(prev_idx_hash.first, idx_hash.first, prev_idx_hash.second, idx_hash.second);
            EdgeStats& e_stats = edge_map[e];
            e_stats.count += weight;
            auto e_it = e_seen.find(e, e.hash);
            if (e_it == e_seen.end()) {
                e_stats.count_case += weight;
                e_seen.insert(e);
            }
        }
        prev_idx_hash = idx_hash;
    }
    // Add the variant into the variant_map.
    variant_map[variant] += weight;

    return memory;
}

size_t VariantStatsState::serialized_size() const {
    size_t result = 0;
    result += sizeof(uint32_t); // num_activities

    // activities dictionary
    for (auto it = activity_map.begin(); it != activity_map.end(); it++) {
        result += sizeof(uint32_t); // idx
        result += sizeof(uint32_t); // size
        result += it->first.size;   // data
    }

    // activity stats
    result += ActivityStats::serialize_size() * activity_stats.size();

    // edge stats
    result += sizeof(uint32_t); // num_edges
    result += (EdgeStats::serialize_size() + 2 * sizeof(uint32_t)) * edge_map.size();

    // variants
    result += sizeof(uint32_t); // num_variants
    for (auto it = variant_map.begin(); it != variant_map.end(); it++) {
        result += sizeof(size_t); // count
        result += sizeof(uint32_t); // num_activities
        result += sizeof(uint32_t) * it->first.data.size(); // activity indices
    }
    return result;
}

void VariantStatsState::serialize(uint8_t* dst) const {
    // TODO(hagonzal): Consider proto serialization.

    // serialization format:
    // num_activities
    // (idx size activity) (idx size activity) ...
    // act_stats act_stats ...
    // num_edges
    // (src dst edge_stats) (src dst edge_stats) ...
    // num_variants
    // count num_activities (activity_idx) (activity_idx) ...
    // count num_activities (activity_idx) (activity_idx) ...

    // activities dictionary
    uint32_t num_activities = activity_map.size();
    memcpy(dst, &num_activities, sizeof(uint32_t));
    dst += sizeof(uint32_t);
    for (auto it = activity_map.begin(); it != activity_map.end(); it++) {
        uint32_t size = it->first.size;
        uint32_t idx = it->second;
        memcpy(dst, &idx, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        memcpy(dst, &size, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        memcpy(dst, it->first.data, it->first.size);
        dst += it->first.size;
    }

    // activity stats
    for (int i = 0; i < activity_stats.size(); i++) {
        activity_stats[i].serialize(dst);
        dst += ActivityStats::serialize_size();
    }

    // edge stats
    uint32_t num_edges = edge_map.size();
    memcpy(dst, &num_edges, sizeof(uint32_t));
    dst += sizeof(uint32_t);
    for (auto it = edge_map.begin(); it != edge_map.end(); it++) {
        uint32_t src_e = it->first.src;
        uint32_t dst_e = it->first.dst;
        memcpy(dst, &src_e, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        memcpy(dst, &dst_e, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        it->second.serialize(dst);
        dst += EdgeStats::serialize_size();
    }

    // variants
    uint32_t num_variants = variant_map.size();
    memcpy(dst, &num_variants, sizeof(uint32_t));
    dst += sizeof(uint32_t);
    for (auto it = variant_map.begin(); it != variant_map.end(); it++) {
        size_t count = it->second;
        uint32_t length = it->first.data.size();

        memcpy(dst, &count, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &length, sizeof(uint32_t));
        dst += sizeof(uint32_t);

        for (int i = 0; i < it->first.data.size(); i++) {
            uint32_t idx = it->first.data[i];
            memcpy(dst, &idx, sizeof(uint32_t));
            dst += sizeof(uint32_t);
        }
    }
}

size_t VariantStatsState::deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len) {
    // read from src and merge with existing state.
    size_t mem = 0;
    const uint8_t* end = src + len;

    // src can contain multiple serialized states. We merge each of them.
    while (src < end) {
        // activities dictionary
        uint32_t num_activities;
        memcpy(&num_activities, src, sizeof(uint32_t));
        src += sizeof(uint32_t);
        // Maps input activity dictionary to the current activity dictionary.
        // src_index -> (local_idx, local_hash)
        std::vector<std::pair<int32_t, size_t>> index_vector;
        index_vector.resize(num_activities);
        for (uint32_t i = 0; i < num_activities; i++) {
            uint32_t len;
            uint32_t idx;
            memcpy(&idx, src, sizeof(uint32_t));
            src += sizeof(uint32_t);
            memcpy(&len, src, sizeof(uint32_t));
            src += sizeof(uint32_t);
            Slice s(src, len);
            src += len;
            index_vector[idx] = maybe_add_activity(mem_pool, s, mem);
        }

        // activity stats
        for (uint32_t i = 0; i < num_activities; i++) {
            ActivityStats as;
            as.deserialize(src);
            src += ActivityStats::serialize_size();
            activity_stats[index_vector[i].first].merge(as);
        }

        // edge stats
        uint32_t num_edges;
        memcpy(&num_edges, src, sizeof(uint32_t));
        src += sizeof(uint32_t);
        for (int i = 0; i < num_edges; i++) {
            uint32_t src_e;
            uint32_t dst_e;
            memcpy(&src_e, src, sizeof(uint32_t));
            src += sizeof(uint32_t);
            memcpy(&dst_e, src, sizeof(uint32_t));
            src += sizeof(uint32_t);

            EdgeStats es;
            es.deserialize(src);
            src += EdgeStats::serialize_size();
            Edge e(index_vector[src_e].first, index_vector[dst_e].first, index_vector[src_e].second,
                   index_vector[dst_e].second);
            edge_map[e].merge(es);
        }

        // variants
        uint32_t num_variants;
        memcpy(&num_variants, src, sizeof(uint32_t));
        src += sizeof(uint32_t);

        for (int i = 0; i < num_variants; i++) {
            size_t count;
            uint32_t num_activities;
            memcpy(&count, src, sizeof(size_t));
            src += sizeof(size_t);
            memcpy(&num_activities, src, sizeof(uint32_t));
            src += sizeof(uint32_t);

            Variant variant(num_activities);
            for (int j = 0; j < num_activities; j++) {
                uint32_t idx;
                memcpy(&idx, src, sizeof(uint32_t));
                src += sizeof(uint32_t);
                auto pair = index_vector[idx];
                variant.add(pair.first, pair.second);
            }
            variant_map[variant] += count;
        }
    }
    return mem;
}

int VariantStatsState::compute_happy_variant(const std::vector<VRef>& sorted) const {
    // Happy path
    // find top start activity
    // find top end activity
    // find top variant with start end from above
    // if not found use top variant

    int top_start = 0;
    size_t count_start = 0;
    for (int i = 0; i < activity_stats.size(); i++) {
        if (activity_stats[i].count_start > count_start) {
            count_start = activity_stats[i].count_start;
            top_start = i;
        }
    }

    // top_end activity has to be different from top start.
    uint32_t top_end = 0;
    size_t count_end = 0;
    for (int i = 0; i < activity_stats.size(); i++) {
        if (activity_stats[i].count_end > count_end && i != top_start) {
            count_end = activity_stats[i].count_end;
            top_end = i;
        }
    }

    // Note: if there is a single activity in the data we will get
    // start = 0, end = 0 and will return the top variant.

    int happy = 0; // default happy is top freq variant.
    for (int i = 0; i < sorted.size(); i++) {
        const auto& v = sorted[i].first->first.data;
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

int VariantStatsState::compute_top_variants(std::vector<VList>& activity_top_variants, VRef& happy) const {
    if (variant_map.empty() || activity_map.empty()) {
        // No data.
        return 0;
    }

    // 1. sort variants by count
    std::vector<VRef> v_count(variant_map.size());
    int i = 0;
    for (auto it = variant_map.cbegin(); it != variant_map.cend(); it++) {
        v_count[i] = {it, it->second};
        i++;
    }
    std::sort(v_count.begin(), v_count.end(), [](const VRef& lhs, const VRef& rhs) {
        return lhs.second > rhs.second;
    });

    // 2. find top-10 variants for each activity
    // Make sure we get enough variants so that each activity has 10 entries

    activity_top_variants.resize(activity_map.size());
    for (int i = 0; i < activity_top_variants.size(); i++) {
        activity_top_variants[i].reserve(10);
    }

    std::vector<int8_t> a_done(activity_map.size(), 0);
    int done_count = 0;

    for (int i = 0; i < v_count.size(); i++) {
        // Check activities matched by this variant.
        const auto& variant = v_count[i].first->first;
        std::vector<int8_t> a_seen(activity_map.size(), 0);
        for (int j = 0; j < variant.data.size(); j++) {
            uint32_t idx = variant.data[j];
            if (activity_top_variants[idx].size() < 10 && a_seen[idx] == 0) {
                activity_top_variants[idx].push_back(v_count[i]);
                a_seen[idx] = 1;
            } else {
                if (a_done[idx] == 0) {
                    a_done[idx] = 1;
                    done_count++;
                }
            }
        }
        // Stop early if we have already collected 10  variants for every activity.
        if (done_count == activity_map.size()) {
            break;
        }
    }
    int happy_v = compute_happy_variant(v_count);
    happy = v_count[happy_v];
    return variant_map.size();
}

std::string VariantStatsState::json_string(std::vector<VList>& activity_top_variants, VRef& happy) const {
    rapidjson::Document d;
    rapidjson::Document::AllocatorType& allocator = d.GetAllocator();
    d.SetObject();

    // Dictionary
    rapidjson::Value dict(rapidjson::kArrayType);
    for (auto it = activity_map.begin(); it != activity_map.end(); it++) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("id", it->second, allocator);
        rapidjson::Value s(rapidjson::kObjectType);
        obj.AddMember("name", rapidjson::Value().SetString(it->first.data, it->first.size, allocator),
                      allocator);
        dict.PushBack(obj, allocator);
    }
    d.AddMember("dict", dict, allocator);

    // Activity stats
    rapidjson::Value a_stats(rapidjson::kArrayType);
    for (int i = 0; i < activity_stats.size(); i++) {
        rapidjson::Value obj = activity_stats[i].to_json(allocator);
        obj.AddMember("id", i, allocator);
        a_stats.PushBack(obj, allocator);
    }
    d.AddMember("a_stats", a_stats, allocator);

    // Edge stats
    rapidjson::Value e_stats(rapidjson::kArrayType);
    for (auto it = edge_map.begin(); it != edge_map.end(); it++) {
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
            rapidjson::Value act = activity_top_variants[i][j].first->first.to_json(allocator);
            size_t count = activity_top_variants[i][j].second;
            var_obj.AddMember("variant", act, allocator);
            var_obj.AddMember("count", count, allocator);
            a.PushBack(var_obj, allocator);
        }
        obj.AddMember("top", a, allocator);
        topv.PushBack(obj, allocator);
    }
    d.AddMember("top", topv, allocator);

    // Happy path
    size_t happy_count = happy.first->second;
    rapidjson::Value happy_obj(rapidjson::kObjectType);
    rapidjson::Value happy_var = happy.first->first.to_json(allocator);
    happy_obj.AddMember("variant", happy_var, allocator);
    happy_obj.AddMember("count", happy_count, allocator);
    d.AddMember("happy", happy_obj, allocator);

    // Encode to string.
    rapidjson::StringBuffer buf;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return buf.GetString();
}

std::string VariantStatsState::debug_string() const {
    std::stringstream ss;
    // print the activity_map
    ss << "activity_map\n";
    for (auto it = activity_map.begin(); it != activity_map.end(); it++) {
        ss << it->first.to_string() << ":" << it->second << "\n";
    }

    // print activity stats
    ss << "activity_stats\n";
    for (int i = 0; i < activity_stats.size(); i++) {
        ss << "(" << i << ") " << activity_stats[i].debug_string() << "\n";
    }

    // print edge stats
    ss << "edge_stats\n";
    for (auto it = edge_map.begin(); it != edge_map.end(); it++) {
        ss << "(" << it->first.src << " " << it->first.dst << ") " << it->first.hash << " : ";
        ss << it->second.debug_string() << "\n";
    }

    // print the variant_map
    ss << "variant_map\n";
    for (auto it = variant_map.begin(); it != variant_map.end(); it++) {
        ss << it->first.debug_string() << " count " << it->second << "\n";
    }
    return ss.str();
}

std::string VariantStatsState::finalize() const {
    std::vector<VList> activity_top_variants;
    VRef happy;
    int num_variants = compute_top_variants(activity_top_variants, happy);
    if (num_variants == 0) {
        return "{}";
    }
    return json_string(activity_top_variants, happy);
}

void VariantStatsAggregateFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr state,
                                           size_t row_num) const  {
    // Expects columns: [0] variant_column, [1] weight_column
    // Pass a constant column with value 1 to ignore weight.

    if (columns[0]->is_nullable() && columns[0]->is_null(row_num)) {
        return;
    }
    if (columns[1]->is_nullable() && columns[1]->is_null(row_num)) {
        return;
    }
    int64_t weight = 0;
    if (!columns[1]->is_constant()) {
        const auto& w_column = down_cast<const Int64Column&>(*columns[1]);
        weight = w_column.get(row_num).get_int64();
    } else {
        const auto& w_column = down_cast<const ConstColumn&>(*columns[1]);
        weight = w_column.get(0).get_int64();
    }
    const ArrayColumn& activity_column = down_cast<const ArrayColumn&>(*columns[0]);
    this->data(state).update(ctx->mem_pool(), activity_column, row_num, weight);
}

void VariantStatsAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                          size_t row_num) const {
    // merge internal state with column[row_num]
    // the column type is binary
    DCHECK(column->is_binary());
    const auto* input_column = down_cast<const BinaryColumn*>(column);
    Slice slice = input_column->get_slice(row_num);
    size_t mem_usage = 0;
    mem_usage += this->data(state).deserialize_and_merge(ctx->mem_pool(), (const uint8_t*) slice.data, slice.size);
    ctx->add_mem_usage(mem_usage);
}

void VariantStatsAggregateFunction::serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                        Column* to) const {
    // append our serialized state to column "to"
    auto* column = down_cast<BinaryColumn*>(to);
    size_t old_size = column->get_bytes().size();
    size_t new_size = old_size + this->data(state).serialized_size();
    column->get_bytes().resize(new_size);
    this->data(state).serialize(column->get_bytes().data() + old_size);
    column->get_offset().emplace_back(new_size);
}

void VariantStatsAggregateFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                       Column* to) const {
    std::string s = this->data(state).finalize();
    down_cast<BinaryColumn*>(to)->append(s);
}

} // namespace starrocks
