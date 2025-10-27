#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/fixed_length_column.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/anyval_util.h"
#include "exprs/arithmetic_operation.h"
#include "exprs/celonis/agg/graph.h"
#include "exprs/celonis/agg/variant_stats.h"
#include "exprs/celonis/base64.h"
#include "exprs/function_context.h"
#include "google/protobuf/util/json_util.h"
#include "modules/query/variantstats.pb.h"
#include "rapidjson/document.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"
#include "testutil/function_utils.h"
#include "util/slice.h"

namespace starrocks {

namespace {

class ManagedAggrState {
public:
    ~ManagedAggrState() { _func->destroy(_ctx, _state); }

    static std::unique_ptr<ManagedAggrState> create(FunctionContext* ctx, const AggregateFunction* func) {
        return std::make_unique<ManagedAggrState>(ctx, func);
    }

    AggDataPtr state() { return _state; }

private:
    ManagedAggrState(FunctionContext* ctx, const AggregateFunction* func) : _ctx(ctx), _func(func) {
        _state = _mem_pool.allocate_aligned(func->size(), func->alignof_size());
        _func->create(_ctx, _state);
    }

    FunctionContext* _ctx;
    const AggregateFunction* _func;
    MemPool _mem_pool;
    AggDataPtr _state;
};

} // namespace

struct GraphResult {
    std::vector<std::string> act_map; // idx -> activity_name
    CelonisGraphAggregateState::EdgeHashMap e_stats;
    int64_t e_count = -123;
    std::vector<ActivityStats> a_stats;
    std::vector<size_t> a_stats_self_loop;

    bool from_json(const std::string& json) {
        rapidjson::Document document;
        document.Parse(json.c_str());
        if (document.HasParseError()) {
            return false;
        }
        if (!dict_from_json(document)) {
            return false;
        }
        if (!a_stats_from_json(document)) {
            return false;
        }
        if (!e_stats_from_json(document)) {
            return false;
        }
        if (!e_count_from_json(document)) {
            return false;
        }
        return true;
    }

    bool equals(const GraphResult& other) {
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
            //name_to_idx[act_map[i]] = i;
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
        if (!equal_e_stats(other, remap_idx)) {
            std::cerr << "e_stats are different.";
            return false;
        }
        if (!equal_e_count(other)) {
            std::cerr << "e_count are different.";
            return false;
        }

        return true;
    }

    bool equal_a_stats(const GraphResult& other, const std::vector<int>& remap_idx) {
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

    // TODO: Check the order when edge_count > 0.
    bool equal_e_stats(const GraphResult& other, const std::vector<int>& remap_idx) {
        if (e_stats.size() != other.e_stats.size()) {
            return false;
        }
        for (auto it = other.e_stats.begin(); it != other.e_stats.end(); it++) {
            int32_t src = remap_idx[it->first.src];
            int32_t dst = remap_idx[it->first.dst];
            Edge e(src, dst);
            auto e_it = e_stats.find(e);
            if (e_it == e_stats.end() || !e_it->second.equal(it->second)) {
                return false;
            }
        }
        return true;
    }

    bool equal_e_count(const GraphResult& other) { return e_count == other.e_count; }

    std::string debug_string() const {
        std::stringstream ss;
        ss << "act_map " << act_map.size() << "\n";
        for (int i = 0; i < act_map.size(); i++) {
            ss << i << " " << act_map[i] << "\n";
        }
        ss << "\na_stats " << a_stats.size() << "\n";
        for (int i = 0; i < a_stats.size(); i++) {
            ss << i << " " << a_stats[i].debug_string() << " self_loop_count_case " << a_stats_self_loop[i] << "\n";
        }
        ss << "\ne_stats " << e_stats.size() << "\n";
        for (auto it = e_stats.cbegin(); it != e_stats.cend(); it++) {
            ss << it->first.debug_string() << " " << it->second.debug_string() << "\n";
        }
        return ss.str();
    }

    bool dict_from_json(const rapidjson::Document& document) {
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

    bool a_stats_from_json(const rapidjson::Document& document) {
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
            a_stats[id].count =
                    obj["count"].IsString() ? std::stoll(obj["count"].GetString()) : obj["count"].GetInt64();
            a_stats[id].count_case = obj["count_case"].IsString() ? std::stoll(obj["count_case"].GetString())
                                                                  : obj["count_case"].GetInt64();
            a_stats[id].count_start = obj["count_start"].IsString() ? std::stoll(obj["count_start"].GetString())
                                                                    : obj["count_start"].GetInt64();
            a_stats[id].count_end = obj["count_end"].IsString() ? std::stoll(obj["count_end"].GetString())
                                                                : obj["count_end"].GetInt64();
            if (obj.HasMember("self_loop_count_case")) {
                a_stats_self_loop[id] = obj["self_loop_count_case"].IsString()
                                                ? std::stoll(obj["self_loop_count_case"].GetString())
                                                : obj["self_loop_count_case"].GetInt64();
            }
        }
        return true;
    }

    bool e_stats_from_json(const rapidjson::Document& document) {
        if (!document["e_stats"].IsArray()) {
            return false;
        }
        const auto& a = document["e_stats"].GetArray();
        for (int i = 0; i < a.Size(); i++) {
            EdgeStats es;
            const auto& obj = a[i];
            es.count = obj["count"].IsString() ? std::stoll(obj["count"].GetString()) : obj["count"].GetInt64();
            es.count_case = obj["count_case"].IsString() ? std::stoll(obj["count_case"].GetString())
                                                         : obj["count_case"].GetInt64();
            int src = obj["src"].IsString() ? std::stoll(obj["src"].GetString()) : obj["src"].GetInt64();
            int dst = obj["dst"].IsString() ? std::stoll(obj["dst"].GetString()) : obj["dst"].GetInt64();
            Edge e(src, dst);

            e_stats[e] = es;
        }
        return true;
    }

    bool e_count_from_json(const rapidjson::Document& document) {
        if (document.HasMember("e_count")) {
            e_count = document["e_count"].IsString() ? std::stoll(document["e_count"].GetString())
                                                     : document["e_count"].GetInt64();
        }
        return true;
    }
};

class CelonisGraphVariantStatsValidationTest : public testing::Test {
public:
    CelonisGraphVariantStatsValidationTest() = default;

protected:
    void SetUp() override {
        runtime_state_vs = new RuntimeState();
        runtime_state_graph = new RuntimeState();
        utils_vs = new FunctionUtils(runtime_state_vs);
        utils_graph = new FunctionUtils(runtime_state_graph);
        ctx_vs = utils_vs->get_fn_ctx();
        ctx_graph = utils_graph->get_fn_ctx();
    }

    void TearDown() override {
        delete utils_vs;
        delete utils_graph;
        // FunctionUtils does not delete runtime_state.
        if (runtime_state_vs != nullptr) {
            delete runtime_state_vs;
        }
        if (runtime_state_graph != nullptr) {
            delete runtime_state_graph;
        }
    }

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

    ArrayColumn::Ptr build_random_variant_column(int seed_rows, int num_rows, int avg_length) {
        // Generate a set of distinct variants to seed generation.
        std::vector<std::string> alphabet = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
        std::random_device rd;
        std::mt19937 gen(rd());
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
    Column::Ptr build_random_weight_column(int num_rows, int avg_weight) {
        ColumnBuilder<TYPE_BIGINT> builder(config::vector_chunk_size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> g(0, 2 * avg_weight);
        for (int i = 0; i < num_rows; ++i) {
            builder.append(g(gen));
        }
        return builder.build(false);
    }
    void match(const std::string& result1, const std::string& result2) {
        GraphResult gr1;
        ASSERT_TRUE(gr1.from_json(result1));

        GraphResult gr2;
        ASSERT_TRUE(gr2.from_json(result2));

        EXPECT_TRUE(gr1.equals(gr2)) << "Result 1: " << gr1.debug_string() << "\nResult 2: " << gr2.debug_string();
    }

    static std::string compute_result(const std::vector<std::pair<ArrayColumn::Ptr, Column::Ptr>>& data,
                                      std::optional<int> edge_count, const AggregateFunction* func,
                                      FunctionContext* ctx) {
        std::vector<ColumnPtr> serde_columns;
        for (const auto& vw_pair : data) {
            auto variant_column = vw_pair.first;
            auto weight_column = vw_pair.second;
            std::vector<const Column*> raw_columns;
            raw_columns.resize(2);
            raw_columns[0] = variant_column.get();
            raw_columns[1] = weight_column.get();
            if (edge_count.has_value()) {
                auto edge_count_column =
                        ColumnHelper::create_const_column<TYPE_BIGINT>(edge_count.value(), variant_column->size());
                raw_columns.resize(3);
                raw_columns[2] = edge_count_column.get();
                ctx->set_constant_columns({nullptr, nullptr, edge_count_column});
            }
            auto state = ManagedAggrState::create(ctx, func);
            func->update_batch_single_state(ctx, variant_column->size(), raw_columns.data(), state->state());

            auto serde_column = BinaryColumn::create();
            func->serialize_to_column(ctx, state->state(), serde_column.get());

            serde_columns.push_back(serde_column);
        }

        auto final_state = ManagedAggrState::create(ctx, func);
        for (const auto& serde_column : serde_columns) {
            func->merge(ctx, serde_column.get(), final_state->state(), 0);
        }

        // Get the result
        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, final_state->state(), result.get());
        EXPECT_EQ(result->size(), 1);

        Slice slice = result->get_slice(0);
        std::string rs = slice.to_string();

        return rs;
    }

private:
    FunctionUtils* utils_vs{};
    FunctionUtils* utils_graph{};
    FunctionContext* ctx_vs{};
    FunctionContext* ctx_graph{};
    RuntimeState* runtime_state_vs{};
    RuntimeState* runtime_state_graph{};
};

TEST_F(CelonisGraphVariantStatsValidationTest, test_basic) {
    auto data = {
            std::make_pair(build_variant_column(
                                   {{"a1", "a2"}, {"a10, a1", "a2", "a3"}, {"a3", "a4"}, {"a5", "a6", "a7, a1, a2"}}),
                           build_weight_column({10, 1, 1, 10})),
            std::make_pair(build_variant_column({{"a3", "a4", "a11"}, {"a9", "a6", "a7", "a8"}}),
                           build_weight_column({1, 15})),
            std::make_pair(build_variant_column({{"a9", "a10", "a3"}}), build_weight_column({1})),
    };
    const AggregateFunction* func_vs = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);
    std::string rs_vs = compute_result(data, std::nullopt, func_vs, ctx_vs);
    const AggregateFunction* func_graph = get_aggregate_function("celonis_graph", TYPE_ARRAY, TYPE_VARCHAR, false);
    std::string rs_gragh = compute_result(data, std::nullopt, func_graph, ctx_graph);
    match(rs_vs, rs_gragh);
}

TEST_F(CelonisGraphVariantStatsValidationTest, test_random_small) {
    auto data = {std::make_pair(build_random_variant_column(2, 2, 2), build_random_weight_column(2, 2)),
                 std::make_pair(build_random_variant_column(2, 2, 2), build_random_weight_column(2, 2)),
                 std::make_pair(build_random_variant_column(2, 2, 2), build_random_weight_column(2, 2)),
                 std::make_pair(build_random_variant_column(2, 2, 2), build_random_weight_column(2, 2)),
                 std::make_pair(build_random_variant_column(2, 2, 2), build_random_weight_column(2, 2))};
    const AggregateFunction* func_vs = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);
    std::string rs_vs = compute_result(data, std::nullopt, func_vs, ctx_vs);
    const AggregateFunction* func_graph = get_aggregate_function("celonis_graph", TYPE_ARRAY, TYPE_VARCHAR, false);
    std::string rs_gragh = compute_result(data, std::nullopt, func_graph, ctx_graph);
    match(rs_vs, rs_gragh);
}

TEST_F(CelonisGraphVariantStatsValidationTest, test_random_large) {
    auto data = {
            std::make_pair(build_random_variant_column(100, 10000, 10), build_random_weight_column(10000, 100)),
            std::make_pair(build_random_variant_column(100, 10000, 10), build_random_weight_column(10000, 100)),
            std::make_pair(build_random_variant_column(100, 10000, 10), build_random_weight_column(10000, 100)),
            std::make_pair(build_random_variant_column(100, 10000, 10), build_random_weight_column(10000, 100)),
            std::make_pair(build_random_variant_column(100, 10000, 10), build_random_weight_column(10000, 100)),
    };
    const AggregateFunction* func_vs = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);
    std::string rs_vs = compute_result(data, std::nullopt, func_vs, ctx_vs);
    const AggregateFunction* func_graph = get_aggregate_function("celonis_graph", TYPE_ARRAY, TYPE_VARCHAR, false);
    std::string rs_gragh = compute_result(data, std::nullopt, func_graph, ctx_graph);
    match(rs_vs, rs_gragh);
}

TEST_F(CelonisGraphVariantStatsValidationTest, test_edge_count) {
    auto data = {
            std::make_pair(build_random_variant_column(100, 10000, 10), build_random_weight_column(10000, 100)),
            std::make_pair(build_random_variant_column(100, 10000, 10), build_random_weight_column(10000, 100)),
            std::make_pair(build_random_variant_column(100, 10000, 10), build_random_weight_column(10000, 100)),
            std::make_pair(build_random_variant_column(100, 10000, 10), build_random_weight_column(10000, 100)),
            std::make_pair(build_random_variant_column(100, 10000, 10), build_random_weight_column(10000, 100)),
    };
    const AggregateFunction* func_vs = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);
    std::string rs_vs = compute_result(data, std::make_optional(5), func_vs, ctx_vs);
    const AggregateFunction* func_graph = get_aggregate_function("celonis_graph", TYPE_ARRAY, TYPE_VARCHAR, false);
    std::string rs_gragh = compute_result(data, std::make_optional(5), func_graph, ctx_graph);
    match(rs_vs, rs_gragh);
}

} // namespace starrocks
