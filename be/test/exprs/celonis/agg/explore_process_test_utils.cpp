#include "explore_process_test_utils.h"

#include <algorithm>
#include <iostream>
#include <random>
#include <sstream>
#include <unordered_map>

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/fixed_length_column.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/celonis/base64.h"
#include "exprs/function_context.h"
#include "gtest/gtest.h"
#include "modules/query/variantstats.pb.h"
#include "util/slice.h"

namespace starrocks {

// ManagedAggrState implementation
ManagedAggrState::~ManagedAggrState() {
    _func->destroy(_ctx, _state);
}

std::unique_ptr<ManagedAggrState> ManagedAggrState::create(FunctionContext* ctx, const AggregateFunction* func) {
    return std::make_unique<ManagedAggrState>(ctx, func);
}

AggDataPtr ManagedAggrState::state() {
    return _state;
}

ManagedAggrState::ManagedAggrState(FunctionContext* ctx, const AggregateFunction* func) : _ctx(ctx), _func(func) {
    _state = _mem_pool.allocate_aligned(func->size(), func->alignof_size());
    _func->create(_ctx, _state);
}

// ExploreProcessResult implementation
bool ExploreProcessResult::from_json(const std::string& json) {
    rapidjson::Document document;
    document.Parse(json.c_str());
    return !document.HasParseError() && try_parse_dict_from_json(document) && try_parse_a_stats_from_json(document) &&
           try_parse_top_from_json(document) && try_parse_happy_from_json(document);
}

bool ExploreProcessResult::equals(const ExploreProcessResult& other) {
    // Robust to activity dictionary remapping.
    if (act_map.size() != other.act_map.size()) {
        std::cerr << "act_map.size() is different.";
        return false;
    }

    // Build the remap_idx to go from other.activity_id to this.activity_id
    std::vector<int> remap_idx; // map other_idx to this_idx
    std::unordered_map<std::string, int> name_to_idx;
    for (int i = 0; i < act_map.size(); i++) {
        name_to_idx.emplace(act_map[i], i);
    }
    remap_idx.resize(act_map.size());
    for (int i = 0; i < other.act_map.size(); i++) {
        const auto it = name_to_idx.find(other.act_map[i]);
        if (it == name_to_idx.end()) {
            return false;
        }
        remap_idx[i] = it->second;
    }
    // Check equality of each component.
    if (!equal_a_stats(other, remap_idx)) {
        std::cerr << "a_stats are different.";
        return false;
    }
    if (!equal_top(other, remap_idx)) {
        std::cerr << "top are different.";
        return false;
    }
    if (!equal_happy(other, remap_idx)) {
        std::cerr << "other are different.";
        return false;
    }

    return true;
}

bool ExploreProcessResult::equal_a_stats(const ExploreProcessResult& other, const std::vector<int>& remap_idx) {
    if (a_stats.size() != other.a_stats.size()) {
        return false;
    }
    for (int i = 0; i < a_stats.size(); i++) {
        int pos = remap_idx[i];
        if (!a_stats[pos].equal(other.a_stats[i])) return false;
        if (a_stats_self_loop[pos] != other.a_stats_self_loop[i]) return false;
    }
    return true;
}

bool ExploreProcessResult::equal_top(const ExploreProcessResult& other, const std::vector<int>& remap_idx) {
    // Compare as unordered collections;
    if (top.size() != other.top.size()) {
        return false;
    }
    auto cmp = [](const auto& lhs, const auto& rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first < rhs.first;
        }
        return lhs.second < rhs.second;
    };
    for (int i = 0; i < top.size(); i++) {
        int pos = remap_idx[i];
        if (top[pos].size() != other.top[i].size()) {
            return false;
        }
        std::vector<std::pair<std::vector<int32_t>, int>> this_top;
        this_top.reserve(top[pos].size());
        for (const auto& entry : top[pos]) {
            this_top.emplace_back(entry.first.data, entry.second);
        }
        std::vector<std::pair<std::vector<int32_t>, int>> other_top;
        other_top.reserve(other.top[i].size());
        for (const auto& entry : other.top[i]) {
            std::vector<int32_t> mapped;
            mapped.reserve(entry.first.data.size());
            for (auto id : entry.first.data) {
                mapped.push_back(remap_idx[id]);
            }
            other_top.emplace_back(std::move(mapped), entry.second);
        }
        std::sort(this_top.begin(), this_top.end(), cmp);
        std::sort(other_top.begin(), other_top.end(), cmp);
        if (this_top != other_top) {
            return false;
        }
    }
    return true;
}

bool ExploreProcessResult::equal_happy(const ExploreProcessResult& other, const std::vector<int>& remap_idx) {
    return happy.first.equal_remap_for_testing(other.happy.first, remap_idx) && happy.second == other.happy.second;
}

std::string ExploreProcessResult::debug_string() const {
    std::stringstream ss;
    ss << "act_map " << act_map.size() << "\n";
    for (int i = 0; i < act_map.size(); i++) {
        ss << i << " " << act_map[i] << "\n";
    }
    ss << "\na_stats " << a_stats.size() << "\n";
    for (int i = 0; i < a_stats.size(); i++) {
        ss << i << " " << a_stats[i].debug_string() << " self_loop_count_case " << a_stats_self_loop[i] << "\n";
    }
    ss << "\ntop " << top.size() << "\n";
    for (int i = 0; i < top.size(); i++) {
        ss << i << ":\n";
        for (int j = 0; j < top[i].size(); j++) {
            ss << top[i][j].first.debug_string() << " " << top[i][j].second << "\n";
        }
    }
    ss << "\nhappy\n";
    ss << happy.first.debug_string() << " " << happy.second << "\n";
    return ss.str();
}

bool ExploreProcessResult::try_parse_dict_from_json(const rapidjson::Document& document) {
    if (!document.HasMember("dict")) {
        return false;
    }
    if (!document["dict"].IsArray()) {
        return false;
    }
    auto a = document["dict"].GetArray();
    act_map.resize(a.Size());
    for (int i = 0; i < a.Size(); i++) {
        const auto& obj = a[i];
        int id = obj["id"].IsString() ? std::stoll(obj["id"].GetString()) : obj["id"].GetInt64();
        if (id < 0 || id >= a.Size()) {
            return false;
        }
        std::string name = obj["name"].GetString();
        act_map[id] = name;
    }
    return true;
}

bool ExploreProcessResult::try_parse_a_stats_from_json(const rapidjson::Document& document) {
    if (!document.HasMember("dict")) {
        return false;
    }
    if (!document["a_stats"].IsArray()) {
        return false;
    }
    const auto& a = document["a_stats"].GetArray();
    a_stats.resize(a.Size());
    a_stats_self_loop.resize(a.Size());
    for (int i = 0; i < a.Size(); i++) {
        const auto& obj = a[i];
        int id = obj["id"].IsString() ? std::stoll(obj["id"].GetString()) : obj["id"].GetInt64();
        if (id < 0 || id >= a.Size()) {
            return false;
        }
        a_stats[id].count = obj["count"].IsString() ? std::stoll(obj["count"].GetString()) : obj["count"].GetInt64();
        a_stats[id].count_case =
                obj["count_case"].IsString() ? std::stoll(obj["count_case"].GetString()) : obj["count_case"].GetInt64();
        a_stats[id].count_start = obj["count_start"].IsString() ? std::stoll(obj["count_start"].GetString())
                                                                : obj["count_start"].GetInt64();
        a_stats[id].count_end =
                obj["count_end"].IsString() ? std::stoll(obj["count_end"].GetString()) : obj["count_end"].GetInt64();
        if (obj.HasMember("self_loop_count_case")) {
            a_stats_self_loop[id] = obj["self_loop_count_case"].IsString()
                                            ? std::stoll(obj["self_loop_count_case"].GetString())
                                            : obj["self_loop_count_case"].GetInt64();
        }
    }
    return true;
}

Variant ExploreProcessResult::parse_variant_from_json(const rapidjson::Value& v) {
    Variant result(v.Size());
    for (int i = 0; i < v.Size(); i++) {
        result.add(v[i].GetInt64(), 0);
    }
    return result;
}

bool ExploreProcessResult::try_parse_top_from_json(const rapidjson::Document& document) {
    if (!document.HasMember("top")) {
        return true;
    }
    const auto& a = document["top"].GetArray();
    top.resize(a.Size());
    for (int i = 0; i < a.Size(); i++) {
        const auto& obj = a[i];
        int id = obj["id"].IsString() ? std::stoll(obj["id"].GetString()) : obj["id"].GetInt64();
        if (id < 0 || id >= a.Size()) {
            return false;
        }
        const auto& t = obj["top"].GetArray();
        for (int j = 0; j < t.Size(); j++) {
            std::pair<Variant, int> av;
            av.first = parse_variant_from_json(t[j]["variant"]);
            av.second = t[j]["count"].IsString() ? std::stoll(t[j]["count"].GetString()) : t[j]["count"].GetInt64();
            top[id].push_back(av);
        }
    }
    return true;
}

bool ExploreProcessResult::try_parse_happy_from_json(const rapidjson::Document& document) {
    if (!document.HasMember("happy")) {
        return true;
    }
    const auto& obj = document["happy"];
    if (!obj["variant"].IsArray()) {
        return false;
    }
    happy.first = parse_variant_from_json(obj["variant"]);
    happy.second = obj["count"].IsString() ? std::stoll(obj["count"].GetString()) : obj["count"].GetInt64();
    return true;
}

// Utility functions implementation
ArrayColumn::Ptr build_variant_column(const std::vector<std::vector<std::string>>& rows) {
    ColumnBuilder<TYPE_VARCHAR> builder(config::vector_chunk_size);
    auto offsets = UInt32Column::create();
    int offset = 0;

    offsets->append(offset);
    for (int i = 0; i < rows.size(); i++) {
        for (int j = 0; j < rows[i].size(); j++) {
            if (rows[i][j] == "null") {
                builder.append_null();
            } else {
                builder.append(Slice(rows[i][j]));
            }
        }
        offset += rows[i].size();
        offsets->append(offset);
    }

    auto data_col = builder.build_nullable_column();
    return ArrayColumn::create(data_col, offsets);
}

Column::Ptr build_weight_column(const std::vector<int>& weight) {
    ColumnBuilder<TYPE_BIGINT> builder(config::vector_chunk_size);

    for (int i = 0; i < weight.size(); i++) {
        builder.append(weight[i]);
    }
    return builder.build(false);
}

ArrayColumn::Ptr build_random_variant_column(int seed_rows, int num_rows, int avg_length, unsigned int seed) {
    // Generate a set of distinct variants to seed generation.
    std::vector<std::string> alphabet = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
    std::mt19937 gen(seed);
    std::uniform_int_distribution<size_t> length_g(0, 2 * avg_length);
    std::uniform_int_distribution<size_t> g(0, alphabet.size() - 1);

    std::vector<std::vector<std::string>> seed_data;
    for (int i = 0; i < seed_rows; i++) {
        std::vector<std::string> v;
        int len = length_g(gen);
        for (int i = 0; i < len; i++) {
            int pos = g(gen);
            v.push_back(alphabet[pos]);
        }
        seed_data.push_back(v);
    }

    // Pick num_rows from seed data using a normal distribution.
    double mean = seed_data.size() / 2.0;
    double stddev = seed_data.size() / 10.0;
    std::normal_distribution<> nd{mean, stddev};

    ColumnBuilder<TYPE_VARCHAR> builder(config::vector_chunk_size);
    auto offsets = UInt32Column::create();
    int offset = 0;
    offsets->append(offset);
    for (int i = 0; i < num_rows; i++) {
        int pos = nd(gen);
        if (pos < 0) pos = 0;
        if (pos >= seed_data.size()) pos = seed_data.size() - 1;
        for (int j = 0; j < seed_data[pos].size(); j++) {
            builder.append(Slice(seed_data[pos][j]));
        }
        offset += seed_data[pos].size();
        offsets->append(offset);
    }
    auto data_col = builder.build_nullable_column();
    return ArrayColumn::create(data_col, offsets);
}

Column::Ptr build_random_weight_column(int num_rows, int avg_weight, unsigned int seed) {
    ColumnBuilder<TYPE_BIGINT> builder(config::vector_chunk_size);
    std::mt19937 gen(seed);
    std::uniform_int_distribution<size_t> g(0, 2 * avg_weight);
    for (int i = 0; i < num_rows; ++i) {
        builder.append(g(gen));
    }
    return builder.build(false);
}

void match_result(const std::string& result1, const std::string& result2) {
    ExploreProcessResult gr1;
    ASSERT_TRUE(gr1.from_json(result1));

    ExploreProcessResult gr2;
    ASSERT_TRUE(gr2.from_json(result2));

    EXPECT_TRUE(gr1.equals(gr2)) << "Result 1: " << gr1.debug_string() << "\nResult 2: " << gr2.debug_string();
}

std::string compute_result_explore_process(const std::vector<std::pair<ArrayColumn::Ptr, Column::Ptr>>& data,
                                           int min_variant_count_threshold_on_leaf, bool enable_proto_encoding,
                                           FunctionContext* ctx) {
    const AggregateFunction* func = get_aggregate_function("celonis_explore_process", TYPE_ARRAY, TYPE_VARCHAR, false);

    std::vector<ColumnPtr> serde_columns;
    for (const auto& vw_pair : data) {
        auto variant_column = vw_pair.first;
        auto weight_column = vw_pair.second;
        auto min_variant_count_threshold_on_leaf_column = ColumnHelper::create_const_column<TYPE_BIGINT>(
                min_variant_count_threshold_on_leaf, variant_column->size());
        auto enable_proto_encoding_column =
                ColumnHelper::create_const_column<TYPE_BOOLEAN>(enable_proto_encoding, variant_column->size());
        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = variant_column.get();
        raw_columns[1] = weight_column.get();
        raw_columns[2] = min_variant_count_threshold_on_leaf_column.get();
        raw_columns[3] = enable_proto_encoding_column.get();
        ctx->set_constant_columns(
                {nullptr, nullptr, min_variant_count_threshold_on_leaf_column, enable_proto_encoding_column});
        auto state = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, variant_column->size(), raw_columns.data(), state->state());

        auto serde_column = BinaryColumn::create();
        func->serialize_to_column(ctx, state->state(), serde_column.get());

        serde_columns.push_back(serde_column);
    }

    auto final_state_on_root = ManagedAggrState::create(ctx, func);
    for (const auto& serde_column : serde_columns) {
        func->merge(ctx, serde_column.get(), final_state_on_root->state(), 0);
    }

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, final_state_on_root->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();

    return rs;
}

std::string compute_result_variant_stats(const std::vector<std::pair<ArrayColumn::Ptr, Column::Ptr>>& data,
                                         FunctionContext* ctx) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    std::vector<ColumnPtr> serde_columns;
    for (const auto& vw_pair : data) {
        auto variant_column = vw_pair.first;
        auto weight_column = vw_pair.second;
        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = variant_column.get();
        raw_columns[1] = weight_column.get();
        auto state = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, variant_column->size(), raw_columns.data(), state->state());

        auto serde_column = BinaryColumn::create();
        func->serialize_to_column(ctx, state->state(), serde_column.get());

        serde_columns.push_back(serde_column);
    }

    auto final_state_on_root = ManagedAggrState::create(ctx, func);
    for (const auto& serde_column : serde_columns) {
        func->merge(ctx, serde_column.get(), final_state_on_root->state(), 0);
    }

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, final_state_on_root->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();

    return rs;
}

std::optional<std::string> proto_to_statistics_json_string(const std::string& encoded_string) {
    int cipher_len = encoded_string.length();
    std::unique_ptr<char[]> p;
    p.reset(new char[cipher_len + 3]);

    int len = base64_decode3(encoded_string.data(), encoded_string.length(), p.get());
    std::string decoded_string(p.get(), len);
    ::celonis::accelerator::Statistics statistics_proto;
    bool success = statistics_proto.ParseFromString(decoded_string);
    if (!success) {
        return std::nullopt;
    }

    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    options.always_print_primitive_fields = true;

    std::string statistics_json;
    google::protobuf::util::MessageToJsonString(statistics_proto, &statistics_json, options);
    return statistics_json;
}
} // namespace starrocks
