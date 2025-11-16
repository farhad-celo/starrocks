#include "exprs/celonis/agg/variant_stats.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/fixed_length_column.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/arithmetic_operation.h"
#include "exprs/celonis/base64.h"
#include "exprs/function_context.h"
#include "google/protobuf/util/json_util.h"
#include "modules/query/variantstats.pb.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"
#include "testutil/function_utils.h"
#include "util/slice.h"

namespace starrocks {

namespace {

std::optional<std::string> to_statistics_json_string(const std::string& encoded_string) {
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
    std::string statistics_json;
    google::protobuf::util::MessageToJsonString(statistics_proto, &statistics_json);
    return statistics_json;
}

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

// Parses json to VariantStats data structures.
// Compares results in a manner robust to changes in activity dictionary.
struct VariantStatsResult {
    std::vector<std::string> act_map; // idx -> activity_name
    VariantStatsFinalizer::EdgeHashMap e_stats;
    int64_t e_count = -123;
    std::vector<ActivityStats> a_stats;
    std::vector<size_t> a_stats_self_loop;
    std::vector<std::vector<std::pair<Variant, int>>> top;
    std::pair<Variant, int> happy;

    bool from_json(const std::string& json) {
        rapidjson::Document document;
        document.Parse(json.c_str());
        if (document.HasParseError()) {
            return false;
        }
        if (!parse_dict_from_json(document)) {
            return false;
        }
        if (!parse_a_stats_from_json(document)) {
            return false;
        }
        if (!parse_e_stats_from_json(document)) {
            return false;
        }
        if (!parse_e_count_from_json(document)) {
            return false;
        }
        if (!try_parse_top_from_json(document)) {
            return false;
        }
        if (!try_parse_happy_from_json(document)) {
            return false;
        }
        return true;
    }

    bool equals(const VariantStatsResult& other) {
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

    bool equal_a_stats(const VariantStatsResult& other, const std::vector<int>& remap_idx) {
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
    bool equal_e_stats(const VariantStatsResult& other, const std::vector<int>& remap_idx) {
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

    bool equal_e_count(const VariantStatsResult& other) { return e_count == other.e_count; }

    bool equal_top(const VariantStatsResult& other, const std::vector<int>& remap_idx) {
        // Top variants per activity are sorted by frequency.
        if (top.size() != other.top.size()) {
            return false;
        }
        for (int i = 0; i < top.size(); i++) {
            int pos = remap_idx[i];
            if (top[pos].size() != other.top[i].size()) {
                return false;
            }
            for (int j = 0; j < other.top[i].size(); j++) {
                if (!top[pos][j].first.equal_remap_for_testing(other.top[i][j].first, remap_idx)) return false;
                if (top[pos][j].second != other.top[i][j].second) return false;
            }
        }
        return true;
    }

    bool equal_happy(const VariantStatsResult& other, const std::vector<int>& remap_idx) {
        return happy.first.equal_remap_for_testing(other.happy.first, remap_idx) && happy.second == other.happy.second;
    }

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

    bool parse_dict_from_json(const rapidjson::Document& document) {
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
            int id = obj["id"].GetInt64();
            if (id < 0 || id >= a.Size()) {
                return false;
            }
            std::string name = obj["name"].GetString();
            act_map[id] = name;
        }
        return true;
    }

    bool parse_a_stats_from_json(const rapidjson::Document& document) {
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
            int id = obj["id"].GetInt64();
            if (id < 0 || id >= a.Size()) {
                return false;
            }
            a_stats[id].count = obj["count"].GetInt64();
            a_stats[id].count_case = obj["count_case"].GetInt64();
            a_stats[id].count_start = obj["count_start"].GetInt64();
            a_stats[id].count_end = obj["count_end"].GetInt64();
            if (obj.HasMember("self_loop_count_case")) {
                a_stats_self_loop[id] = obj["self_loop_count_case"].GetInt64();
            }
        }
        return true;
    }

    bool parse_e_stats_from_json(const rapidjson::Document& document) {
        if (!document["e_stats"].IsArray()) {
            return false;
        }
        const auto& a = document["e_stats"].GetArray();
        for (int i = 0; i < a.Size(); i++) {
            EdgeStats es;
            const auto& obj = a[i];
            es.count = obj["count"].GetInt64();
            es.count_case = obj["count_case"].GetInt64();
            int src = obj["src"].GetInt64();
            int dst = obj["dst"].GetInt64();
            Edge e(src, dst);

            e_stats[e] = es;
        }
        return true;
    }

    bool parse_e_count_from_json(const rapidjson::Document& document) {
        if (document.HasMember("e_count")) {
            e_count = document["e_count"].GetInt64();
        }
        return true;
    }

    Variant parse_variant_from_json(const rapidjson::Value& v) {
        Variant result(v.Size());
        for (int i = 0; i < v.Size(); i++) {
            result.add(v[i].GetInt64(), 0);
        }
        return result;
    }

    bool try_parse_top_from_json(const rapidjson::Document& document) {
        if (!document.HasMember("top")) {
            return true;
        }
        const auto& a = document["top"].GetArray();
        top.resize(a.Size());
        for (int i = 0; i < a.Size(); i++) {
            const auto& obj = a[i];
            int id = obj["id"].GetInt64();
            if (id < 0 || id >= a.Size()) {
                return false;
            }
            const auto& t = obj["top"].GetArray();
            for (int j = 0; j < t.Size(); j++) {
                std::pair<Variant, int> av;
                av.first = parse_variant_from_json(t[j]["variant"]);
                av.second = t[j]["count"].GetInt64();
                top[id].push_back(av);
            }
        }
        return true;
    }

    bool try_parse_happy_from_json(const rapidjson::Document& document) {
        if (!document.HasMember("happy")) {
            return true;
        }
        const auto& obj = document["happy"];
        if (!obj["variant"].IsArray()) {
            return false;
        }
        happy.first = parse_variant_from_json(obj["variant"]);
        happy.second = obj["count"].GetInt64();
        return true;
    }
};

class CelonisVariantStatsTest : public testing::Test {
public:
    CelonisVariantStatsTest() = default;

    void SetUp() override {
        runtime_state = new RuntimeState();
        utils = new FunctionUtils(runtime_state);
        ctx = utils->get_fn_ctx();
    }

    void TearDown() override {
        delete utils;
        // FunctionUtils does not delete runtime_state.
        if (runtime_state != nullptr) {
            delete runtime_state;
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

    ArrayColumn::Ptr build_random_variant_column(int seed_rows, int num_rows, int avg_length) {
        // Generate a set of distinct variants to seed generation.
        std::vector<std::string> alphabet = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
        std::random_device rd;
        std::uniform_int_distribution<size_t> length_g(0, 2 * avg_length);
        std::uniform_int_distribution<size_t> g(0, alphabet.size() - 1);

        std::vector<std::vector<std::string>> seed_data;
        for (int i = 0; i < seed_rows; i++) {
            std::vector<std::string> v;
            int len = length_g(rd);
            for (int i = 0; i < len; i++) {
                int pos = g(rd);
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
            int pos = nd(rd);
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

    Column::Ptr build_weight_column(const std::vector<int>& weight) {
        ColumnBuilder<TYPE_BIGINT> builder(config::vector_chunk_size);

        for (int i = 0; i < weight.size(); i++) {
            builder.append(weight[i]);
        }
        return builder.build(false);
    }

    void format_result(std::string& rs) {
        rs.erase(std::remove(rs.begin(), rs.end(), '\n'), rs.end());
        rs.erase(std::remove(rs.begin(), rs.end(), ' '), rs.end());
        std::replace(rs.begin(), rs.end(), '\"', '\'');
    }

    void match(const std::string& expected, const std::string& actual) {
        VariantStatsResult vs;
        ASSERT_TRUE(vs.from_json(actual));

        std::string e_s = expected;
        std::replace(e_s.begin(), e_s.end(), '\'', '\"');
        VariantStatsResult e_vs;
        ASSERT_TRUE(e_vs.from_json(e_s));

        EXPECT_TRUE(e_vs.equals(vs)) << "Actual: " << vs.debug_string() << "\nExpected: " << e_vs.debug_string();
    }

private:
    FunctionUtils* utils{};
    FunctionContext* ctx{};
    RuntimeState* runtime_state{};
};

TEST_F(CelonisVariantStatsTest, test_equality) {
    std::string s =
            "{ 'dict':[ {'id':3,'name':'a4'}, {'id':2,'name':'a3'}, {'id':0,'name':'a1'}, {'id':1,'name':'a2'} ], "
            "'a_stats':[ {'count':3,'count_case':3,'count_start':3,'count_end':0,'id':0}, "
            "{'count':3,'count_case':3,'count_start':0,'count_end':2,'id':1}, "
            "{'count':2,'count_case':2,'count_start':1,'count_end':1,'id':2}, "
            "{'count':1,'count_case':1,'count_start':0,'count_end':1,'id':3} ], 'e_stats':[ "
            "{'count':3,'count_case':3,'src':0,'dst':1}, {'count':1,'count_case':1,'src':1,'dst':2}, "
            "{'count':1,'count_case':1,'src':2,'dst':3} ], 'top':[ {'id':0,'top':[ {'variant':[0,1],'count':2}, "
            "{'variant':[0,1,2],'count':1}]}, {'id':1,'top':[ {'variant':[0,1],'count':2}, "
            "{'variant':[0,1,2],'count':1}]}, {'id':2,'top':[ {'variant':[0,1,2],'count':1}, "
            "{'variant':[2,3],'count':1}]}, {'id':3,'top':[ {'variant':[2,3],'count':1}]} ], 'happy': "
            "{'variant':[0,1],'count':2} }";
    std::replace(s.begin(), s.end(), '\'', '\"');
    VariantStatsResult vs;
    ASSERT_TRUE(vs.from_json(s));
    std::cout << "\n==== vs\n" << vs.debug_string() << "\n===\n";

    // remap 0 to 1 and 1 to 0
    std::string s1 =
            "{ 'dict':[ {'id':3,'name':'a4'}, {'id':2,'name':'a3'}, {'id':1,'name':'a1'}, {'id':0,'name':'a2'} ], "
            "'a_stats':[ {'count':3,'count_case':3,'count_start':3,'count_end':0,'id':1}, "
            "{'count':3,'count_case':3,'count_start':0,'count_end':2,'id':0}, "
            "{'count':2,'count_case':2,'count_start':1,'count_end':1,'id':2}, "
            "{'count':1,'count_case':1,'count_start':0,'count_end':1,'id':3} ], 'e_stats':[ "
            "{'count':3,'count_case':3,'src':1,'dst':0}, {'count':1,'count_case':1,'src':0,'dst':2}, "
            "{'count':1,'count_case':1,'src':2,'dst':3} ], 'top':[ {'id':0,'top':[{ 'variant':[1,0],'count':2},{ "
            "'variant':[1,0,2],'count':1}]}, {'id':1,'top':[{ 'variant':[1,0],'count':2},{ "
            "'variant':[1,0,2],'count':1}]}, {'id':2,'top':[{ 'variant':[1,0,2],'count':1},{ "
            "'variant':[2,3],'count':1}]}, {'id':3,'top':[{ 'variant':[2,3],'count':1}]} ], "
            "'happy':{'variant':[1,0],'count':2} }";
    std::replace(s1.begin(), s1.end(), '\'', '\"');
    VariantStatsResult vs1;
    ASSERT_TRUE(vs1.from_json(s1));
    std::cout << "\n==== vs1\n" << vs1.debug_string() << "\n===\n";

    EXPECT_TRUE(vs.equals(vs));
    EXPECT_TRUE(vs1.equals(vs1));
    EXPECT_TRUE(vs.equals(vs1));
    EXPECT_TRUE(vs1.equals(vs));
}

TEST_F(CelonisVariantStatsTest, test_no_merge) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"}, {"a1", "a2"}, {"a1", "a2", "a3"}, {"a3", "a4"}});

    auto weights = build_weight_column({1, 1, 1, 1});
    std::vector<const Column*> raw_columns;
    raw_columns.resize(2);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state1->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();

    std::string e_s =
            "{'dict':[{'id':3,'name':'a4'},{'id':2,'name':'a3'},{'id':0,'name':'a1'},{'id':1,'name':'a2'}],'a_stats':[{"
            "'count':3,'count_case':3,'count_start':3,'count_end':0,'id':0},"
            "{'count':3,'count_case':3,'count_start':0,'count_end':2,'id':1},{'count':2,'count_case':2,'count_start':1,"
            "'count_end':1,'id':2},"
            "{'count':1,'count_case':1,'count_start':0,'count_end':1,'id':3}],'e_count':3,'e_stats':[{'count':3,'count_"
            "case':3,'src':0,'dst':1},"
            "{'count':1,'count_case':1,'src':1,'dst':2},{'count':1,'count_case':1,'src':2,'dst':3}],'top':[{'id':0,'"
            "top':[{'variant':[0,1],'count':2},{'variant':[0,1,2],'count':1}]}, "
            "{'id':1,'top':[{'variant':[0,1],'count':2},{'variant':[0,1,2],'count':1}]},{'id':2,'top':[{'variant':[2,3]"
            ",'count':1},{'variant':[0,1,2],'count':1}]},{'id':3,'top':[{'variant':[2,3],'count':1}]}],'happy':{'"
            "variant':[0,1],'count':2}}";
    match(e_s, rs);
}

TEST_F(CelonisVariantStatsTest, test_merge_with_itself) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col = build_variant_column({{}, {"key1", "key2"}, {"sr-1", "sr-2", "sr-2"}});

    auto weights = build_weight_column({1, 1, 1});

    std::vector<const Column*> raw_columns;
    raw_columns.resize(2);
    raw_columns[0] = col.get();
    raw_columns[1] = weights.get();

    auto state = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col->size(), raw_columns.data(), state->state());

    // Serialize the column
    auto part1 = BinaryColumn::create();
    func->serialize_to_column(ctx, state->state(), part1.get());

    // Merge with itself
    func->merge(ctx, part1.get(), state->state(), 0);

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();

    std::string e_s =
            "{'dict':[{'id':3,'name':'sr-2'},{'id':2,'name':'sr-1'},{'id':0,'name':'key1'},{'id':1,'name':'key2'}],'a_"
            "stats':[{'count':2,'count_case':2,'count_start':2,'count_end':0,'id':0},{'count':2,'count_case':2,'count_"
            "start':0,'count_end':2,'id':1},{'count':2,'count_case':2,'count_start':2,'count_end':0,'id':2},{'count':4,"
            "'count_case':2,'count_start':0,'count_end':2,'self_loop_count_case':2,'id':3}],'e_stats':[{'count':2,'"
            "count_case':2,'src':0,'dst':1},{'count':2,'count_case':2,'src':2,'dst':3},{'count':2,'count_case':2,'src':"
            "3,'dst':3}],'top':[{'id':0,'top':[{'variant':[0,1],'count':2}]},{'id':1,'top':[{'variant':[0,1],'count':2}"
            "]},{'id':2,'top':[{'variant':[2,3,3],'count':2}]},{'id':3,'top':[{'variant':[2,3,3],'count':2}]}],'happy':"
            "{'variant':[0,1],'count':2},'e_count':3}";
    match(e_s, rs);
}

TEST_F(CelonisVariantStatsTest, test_merge_distinct_dict) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"}, {"a1", "a2", "a3"}, {"a3", "a4"}});
    auto weights = build_weight_column({1, 1, 1});
    std::vector<const Column*> raw_columns;
    raw_columns.resize(2);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    auto part1 = BinaryColumn::create();
    func->serialize_to_column(ctx, state1->state(), part1.get());

    auto col2 = build_variant_column({{"a1", "a4", "a0"}, {"a1", "a2", "a2", "a2", "a5"}});
    auto weights2 = build_weight_column({1, 1});

    std::vector<const Column*> raw_columns2;
    raw_columns2.resize(2);
    raw_columns2[0] = col2.get();
    raw_columns2[1] = weights2.get();
    auto state2 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col2->size(), raw_columns2.data(), state2->state());
    auto part2 = BinaryColumn::create();
    func->serialize_to_column(ctx, state2->state(), part2.get());

    // Merge part1 and part2
    func->merge(ctx, part2.get(), state1->state(), 0);

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state1->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();
    std::string e_s =
            "{'dict':[{'id':3,'name':'a4'},{'id':2,'name':'a3'},{'id':4,'name':'a0'},{'id':0,'name':'a1'},{'id':5,'"
            "name':'a5'},{'id':1,'name':'a2'}],'a_stats':[{'count':4,'count_case':4,'count_start':4,'count_end':0,'id':"
            "0},{'count':5,'count_case':3,'count_start':0,'count_end':1,'self_loop_count_case':1,'id':1},{'count':2,'"
            "count_case':2,'count_start':1,'count_end':1,'id':2},{'count':2,'count_case':2,'count_start':0,'count_end':"
            "1,'id':3},{'count':1,'count_case':1,'count_start':0,'count_end':1,'id':4},{'count':1,'count_case':1,'"
            "count_start':0,'count_end':1,'id':5}],'e_stats':[{'count':1,'count_case':1,'src':1,'dst':2},{'count':1,'"
            "count_case':1,'src':2,'dst':3},{'count':1,'count_case':1,'src':0,'dst':3},{'count':3,'count_case':3,'src':"
            "0,'dst':1},{'count':1,'count_case':1,'src':3,'dst':4},{'count':2,'count_case':1,'src':1,'dst':1},{'count':"
            "1,'count_case':1,'src':1,'dst':5}],'top':[{'id':0,'top':[{'variant':[0,1],'count':1},{'variant':[0,3,4],'"
            "count':1},{'variant':[0,1,2],'count':1},{'variant':[0,1,1,1,5],'count':1}]},{'id':1,'top':[{'variant':[0,"
            "1],'count':1},{'variant':[0,1,2],'count':1},{'variant':[0,1,1,1,5],'count':1}]},{'id':2,'top':[{'variant':"
            "[2,3],'count':1},{'variant':[0,1,2],'count':1}]},{'id':3,'top':[{'variant':[2,3],'count':1},{'variant':[0,"
            "3,4],'count':1}]},{'id':4,'top':[{'variant':[0,3,4],'count':1}]},{'id':5,'top':[{'variant':[0,1,1,1,5],'"
            "count':1}]}],'happy':{'variant':[0,1],'count':1},'e_count':7}";
    match(e_s, rs);
}

TEST_F(CelonisVariantStatsTest, test_weights) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"}, {"a1", "a2"}, {"a1", "a2", "a2"}, {"a3", "a4"}});

    auto weights = build_weight_column({1, 1, 3, 2});
    std::vector<const Column*> raw_columns;
    raw_columns.resize(2);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state1->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();
    std::string e_s =
            "{'dict':[{'id':3,'name':'a4'},{'id':2,'name':'a3'},{'id':0,'name':'a1'},{'id':1,'name':'a2'}],'a_stats':[{"
            "'count':5,'count_case':5,'count_start':5,'count_end':0,'id':0},{'count':8,'count_case':5,'count_start':0,'"
            "count_end':5,'self_loop_count_case':3,'id':1},{'count':2,'count_case':2,'count_start':2,'count_end':0,'id'"
            ":2},{'count':2,'count_case':2,'count_start':0,'count_end':2,'id':3}],'e_stats':[{'count':5,'count_case':5,"
            "'src':0,'dst':1},{'count':3,'count_case':3,'src':1,'dst':1},{'count':2,'count_case':2,'src':2,'dst':3}],'"
            "top':[{'id':0,'top':[{'variant':[0,1,1],'count':3},{'variant':[0,1],'count':2}]},{'id':1,'top':[{'variant'"
            ":[0,1,1],'count':3},{'variant':[0,1],'count':2}]},{'id':2,'top':[{'variant':[2,3],'count':2}]},{'id':3,'"
            "top':[{'variant':[2,3],'count':2}]}],'happy':{'variant':[0,1,1],'count':3},'e_count':3}";
    match(e_s, rs);
}

TEST_F(CelonisVariantStatsTest, test_empty) {
    {
        // No data.
        const AggregateFunction* func =
                get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);
        auto col1 = build_variant_column({});
        auto weights = build_weight_column({});
        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = col1.get();
        raw_columns[1] = weights.get();
        auto state1 = ManagedAggrState::create(ctx, func);
        // Don't call update when there is no data.
        // func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());
        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, state1->state(), result.get());
        EXPECT_EQ(result->size(), 1);
        Slice slice = result->get_slice(0);
        std::string rs = slice.to_string();
        EXPECT_EQ(rs, "{}");
    }

    {
        // Only empty variant.
        const AggregateFunction* func =
                get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);
        auto col1 = build_variant_column({{}});
        auto weights = build_weight_column({1});
        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = col1.get();
        raw_columns[1] = weights.get();
        auto state1 = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());
        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, state1->state(), result.get());
        EXPECT_EQ(result->size(), 1);
        Slice slice = result->get_slice(0);
        std::string rs = slice.to_string();
        EXPECT_EQ(rs, "{}");
    }

    {
        // Single activity.
        const AggregateFunction* func =
                get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);
        auto col1 = build_variant_column({{"a1"}});
        auto weights = build_weight_column({1});
        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = col1.get();
        raw_columns[1] = weights.get();
        auto state1 = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());
        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, state1->state(), result.get());
        EXPECT_EQ(result->size(), 1);
        Slice slice = result->get_slice(0);
        std::string rs = slice.to_string();
        std::string e_s =
                "{'dict':[{'id':0,'name':'a1'}],'a_stats':[{'count':1,'count_case':1,'count_start':1,'count_end':1,'id'"
                ":0}],'e_stats':[],'top':[{'id':0,'top':[{'variant':[0],'count':1}]}],'happy':{'variant':[0],'count':1}"
                ",'e_count':0}";
        match(e_s, rs);
    }

    {
        // Single activity with self loop.
        const AggregateFunction* func =
                get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);
        auto col1 = build_variant_column({{"a1", "a1"}});
        auto weights = build_weight_column({1, 1});
        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = col1.get();
        raw_columns[1] = weights.get();
        auto state1 = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());
        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, state1->state(), result.get());
        EXPECT_EQ(result->size(), 1);
        Slice slice = result->get_slice(0);
        std::string rs = slice.to_string();
        std::string e_s =
                "{'dict':[{'id':0,'name':'a1'}],'a_stats':[{'count':2,'count_case':1,'count_start':1,'count_end':1,'"
                "self_loop_count_case':1,'id':0}],'e_stats':[{'count':1,'count_case':1,'src':0,'dst':0}],'top':[{'id':"
                "0,'top':[{'variant':[0,0],'count':1}]}],'happy':{'variant':[0,0],'count':1},'e_count':1}";
        match(e_s, rs);
    }
}

TEST_F(CelonisVariantStatsTest, test_null_activity) {
    // null activity.
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);
    auto col1 = build_variant_column({{"a", "null", "b"}});
    auto weights = build_weight_column({1});
    std::vector<const Column*> raw_columns;
    raw_columns.resize(2);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state1->state(), result.get());
    EXPECT_EQ(result->size(), 1);
    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();
    std::string e_s =
            "{'dict':[{'id':1,'name':'b'},{'id':0,'name':'a'}],'a_stats':[{'count':1,'count_case':1,'count_start':1,'"
            "count_end':0,'id':0},{'count':1,'count_case':1,'count_start':0,'count_end':1,'id':1}],'e_stats':[{'count':"
            "1,'count_case':1,'src':0,'dst':1}],'top':[{'id':0,'top':[{'variant':[0,1],'count':1}]},{'id':1,'top':[{'"
            "variant':[0,1],'count':1}]}],'happy':{'variant':[0,1],'count':1},'e_count':1}";
    match(e_s, rs);
}

TEST_F(CelonisVariantStatsTest, test_large) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_random_variant_column(100, 10000, 10);
    auto col2 = build_random_variant_column(100, 10000, 10);
    auto weight_column = ColumnHelper::create_const_column<TYPE_BIGINT>(1, 1);

    // compute stats and serialize for col1
    std::vector<const Column*> raw_columns1;
    raw_columns1.resize(2);
    raw_columns1[0] = col1.get();
    raw_columns1[1] = weight_column.get();
    auto state = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns1.data(), state->state());
    auto part1 = BinaryColumn::create();
    func->serialize_to_column(ctx, state->state(), part1.get());

    // compute stats and serialize for col2
    std::vector<const Column*> raw_columns2;
    raw_columns2.resize(2);
    raw_columns2[0] = col2.get();
    raw_columns2[1] = weight_column.get();
    auto state2 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col2->size(), raw_columns2.data(), state2->state());
    auto part2 = BinaryColumn::create();
    func->serialize_to_column(ctx, state2->state(), part2.get());

    // Merge
    func->merge(ctx, part2.get(), state->state(), 0);

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();
    std::cout << rs << "\n";
    format_result(rs);
    std::cout << rs << "\n";
}

TEST_F(CelonisVariantStatsTest, test_top_with_repeated_activities) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"}, {"a1", "a2", "a1", "a2"}});

    auto weights = build_weight_column({1, 10});
    std::vector<const Column*> raw_columns;
    raw_columns.resize(2);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state1->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();

    std::string e_s =
            R"json({
            "dict": [
                {
                    "id": 0,
                    "name": "a1"
                },
                {
                    "id": 1,
                    "name": "a2"
                }
            ],
            "a_stats": [
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 11,
                    "count_end": 0,
                    "id": 0
                },
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 0,
                    "count_end": 11,
                    "id": 1
                }
            ],
            "e_stats": [
                {
                    "count": 21,
                    "count_case": 11,
                    "src": 0,
                    "dst": 1
                },
                {
                    "count": 10,
                    "count_case": 10,
                    "src": 1,
                    "dst": 0
                }
            ],
            "top": [
                {
                    "id": 0,
                    "top": [
                        {
                            "variant": [0,1,0,1],
                            "count": 10
                        },
                        {
                            "variant": [0,1],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 1,
                    "top": [
                        {
                            "variant": [0,1,0,1],
                            "count": 10
                        },
                        {
                            "variant": [0,1],
                            "count": 1
                        }
                    ]
                }
            ],
            "happy": {
                "variant": [0,1,0,1],
                "count": 10
            },
            "e_count": 2
        })json";
    match(e_s, rs);
}

TEST_F(CelonisVariantStatsTest, test_edge_count) {
    // edge_count = 0, populate e_count, do not populate e_stats
    {
        const AggregateFunction* func =
                get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

        auto col1 = build_variant_column(
                {{"a1", "a2", "a3", "a4", "a5", "a6", "a7", "a00", "a01", "a02"}, {"a1", "a2", "a1", "a2"}});

        auto weights = build_weight_column({1, 10});
        auto edge_count = ColumnHelper::create_const_column<TYPE_BIGINT>(0, col1->size());
        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = col1.get();
        raw_columns[1] = weights.get();
        raw_columns[2] = edge_count.get();
        ctx->set_constant_columns({nullptr, nullptr, edge_count});
        auto state1 = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

        // Get the result
        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, state1->state(), result.get());
        EXPECT_EQ(result->size(), 1);

        Slice slice = result->get_slice(0);
        std::string rs = slice.to_string();

        std::string e_s =
                R"json({
            "dict": [
                {
                    "id": 0,
                    "name": "a1"
                },
                {
                    "id": 1,
                    "name": "a2"
                },
                {
                    "id": 2,
                    "name": "a3"
                },
                {
                    "id": 3,
                    "name": "a4"
                },
                {
                    "id": 4,
                    "name": "a5"
                },
                {
                    "id": 5,
                    "name": "a6"
                },
                {
                    "id": 6,
                    "name": "a7"
                },
                {
                    "id": 7,
                    "name": "a00"
                },
                {
                    "id": 8,
                    "name": "a01"
                },
                {
                    "id": 9,
                    "name": "a02"
                }
            ],
            "a_stats": [
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 11,
                    "count_end": 0,
                    "id": 0
                },
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 0,
                    "count_end": 10,
                    "id": 1
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 2
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 3
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 4
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 5
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 6
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 7
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 8
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 1,
                    "id": 9
                }
            ],
            "e_count": 10,
            "e_stats": [],
            "top": [
                {
                    "id": 0,
                    "top": [
                        {
                            "variant": [0,1,0,1],
                            "count": 10
                        },
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 1,
                    "top": [
                        {
                            "variant": [0,1,0,1],
                            "count": 10
                        },
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 2,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 3,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 4,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 5,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 6,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 7,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 8,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 9,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                }
            ],
            "happy": {
                "variant": [0,1,0,1],
                "count": 10
            }
        })json";
        match(e_s, rs);
    }
    {
        const AggregateFunction* func =
                get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

        auto col1 = build_variant_column(
                {{"a1", "a2", "a3", "a4", "a5", "a6", "a7", "a00", "a01", "a02"}, {"a1", "a2", "a1", "a2"}});

        auto weights = build_weight_column({1, 10});
        auto edge_count = ColumnHelper::create_const_column<TYPE_BIGINT>(-1, col1->size());
        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = col1.get();
        raw_columns[1] = weights.get();
        raw_columns[2] = edge_count.get();
        ctx->set_constant_columns({nullptr, nullptr, edge_count});
        auto state1 = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

        // Get the result
        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, state1->state(), result.get());
        EXPECT_EQ(result->size(), 1);

        Slice slice = result->get_slice(0);
        std::string rs = slice.to_string();

        std::string e_s =
                R"json({
            "dict": [
                {
                    "id": 0,
                    "name": "a1"
                },
                {
                    "id": 1,
                    "name": "a2"
                },
                {
                    "id": 2,
                    "name": "a3"
                },
                {
                    "id": 3,
                    "name": "a4"
                },
                {
                    "id": 4,
                    "name": "a5"
                },
                {
                    "id": 5,
                    "name": "a6"
                },
                {
                    "id": 6,
                    "name": "a7"
                },
                {
                    "id": 7,
                    "name": "a00"
                },
                {
                    "id": 8,
                    "name": "a01"
                },
                {
                    "id": 9,
                    "name": "a02"
                }
            ],
            "a_stats": [
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 11,
                    "count_end": 0,
                    "id": 0
                },
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 0,
                    "count_end": 10,
                    "id": 1
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 2
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 3
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 4
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 5
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 6
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 7
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 8
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 1,
                    "id": 9
                }
            ],
            "e_stats": [],
            "top": [
                {
                    "id": 0,
                    "top": [
                        {
                            "variant": [0,1,0,1],
                            "count": 10
                        },
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 1,
                    "top": [
                        {
                            "variant": [0,1,0,1],
                            "count": 10
                        },
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 2,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 3,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 4,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 5,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 6,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 7,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 8,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 9,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                }
            ],
            "happy": {
                "variant": [0,1,0,1],
                "count": 10
            }
        })json";
        match(e_s, rs);
    }
    {
        const AggregateFunction* func =
                get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

        auto col1 = build_variant_column(
                {{"a1", "a2", "a3", "a4", "a5", "a6", "a7", "a00", "a01", "a02"}, {"a1", "a2", "a1", "a2"}});

        auto weights = build_weight_column({1, 10});
        auto edge_count = ColumnHelper::create_const_column<TYPE_BIGINT>(5, col1->size());
        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = col1.get();
        raw_columns[1] = weights.get();
        raw_columns[2] = edge_count.get();
        ctx->set_constant_columns({nullptr, nullptr, edge_count});
        auto state1 = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

        // Get the result
        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, state1->state(), result.get());
        EXPECT_EQ(result->size(), 1);

        Slice slice = result->get_slice(0);
        std::string rs = slice.to_string();

        std::string e_s =
                R"json({
            "dict": [
                {
                    "id": 0,
                    "name": "a1"
                },
                {
                    "id": 1,
                    "name": "a2"
                },
                {
                    "id": 2,
                    "name": "a3"
                },
                {
                    "id": 3,
                    "name": "a4"
                },
                {
                    "id": 4,
                    "name": "a5"
                },
                {
                    "id": 5,
                    "name": "a6"
                },
                {
                    "id": 6,
                    "name": "a7"
                },
                {
                    "id": 7,
                    "name": "a00"
                },
                {
                    "id": 8,
                    "name": "a01"
                },
                {
                    "id": 9,
                    "name": "a02"
                }
            ],
            "a_stats": [
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 11,
                    "count_end": 0,
                    "id": 0
                },
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 0,
                    "count_end": 10,
                    "id": 1
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 2
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 3
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 4
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 5
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 6
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 7
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 8
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 1,
                    "id": 9
                }
            ],
            "e_count": 10,
            "e_stats": [
                {
                    "count": 1,
                    "count_case": 1,
                    "src": 7,
                    "dst": 8
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "src": 8,
                    "dst": 9
                },
                {
                    "count": 21,
                    "count_case": 11,
                    "src": 0,
                    "dst": 1
                },
                {
                    "count": 10,
                    "count_case": 10,
                    "src": 1,
                    "dst": 0
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "src": 1,
                    "dst": 2
                }
            ],
            "top": [
                {
                    "id": 0,
                    "top": [
                        {
                            "variant": [0,1,0,1],
                            "count": 10
                        },
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 1,
                    "top": [
                        {
                            "variant": [0,1,0,1],
                            "count": 10
                        },
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 2,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 3,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 4,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 5,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 6,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 7,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 8,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 9,
                    "top": [
                        {
                            "variant": [0,1,2,3,4,5,6,7,8,9],
                            "count": 1
                        }
                    ]
                }
            ],
            "happy": {
                "variant": [0,1,0,1],
                "count": 10
            }
        })json";
        match(e_s, rs);
    }
}

TEST_F(CelonisVariantStatsTest, test_self_loop_with_negative_edge_count) {
    // with edge_count == -1, the stats is populated correctly.
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"}, {"a1", "a2", "a2", "a1"}});

    auto weights = build_weight_column({1, 10});
    auto edge_count = ColumnHelper::create_const_column<TYPE_BIGINT>(-1, col1->size());
    std::vector<const Column*> raw_columns;
    raw_columns.resize(3);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    raw_columns[2] = edge_count.get();
    ctx->set_constant_columns({nullptr, nullptr, edge_count});
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state1->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();

    std::string e_s =
            R"json({
            "dict": [
                {
                    "id": 0,
                    "name": "a1"
                },
                {
                    "id": 1,
                    "name": "a2"
                }
            ],
            "a_stats": [
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 11,
                    "count_end": 10,
                    "id": 0
                },
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 0,
                    "count_end": 1,
                    "self_loop_count_case": 10,
                    "id": 1
                }
            ],
            "e_stats": [],
            "top": [
                {
                    "id": 0,
                    "top": [
                        {
                            "variant": [0,1,1,0],
                            "count": 10
                        },
                        {
                            "variant": [0,1],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 1,
                    "top": [
                        {
                            "variant": [0,1,1,0],
                            "count": 10
                        },
                        {
                            "variant": [0,1],
                            "count": 1
                        }
                    ]
                }
            ],
            "happy": {
                "variant": [0,1],
                "count": 1
            }
        })json";
    match(e_s, rs);
}

TEST_F(CelonisVariantStatsTest, test_self_loop) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"}, {"a1", "a2", "a2", "a1"}});

    auto weights = build_weight_column({1, 10});
    std::vector<const Column*> raw_columns;
    raw_columns.resize(2);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state1->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();

    std::string e_s =
            R"json({
            "dict": [
                {
                    "id": 0,
                    "name": "a1"
                },
                {
                    "id": 1,
                    "name": "a2"
                }
            ],
            "a_stats": [
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 11,
                    "count_end": 10,
                    "id": 0
                },
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 0,
                    "count_end": 1,
                    "self_loop_count_case": 10,
                    "id": 1
                }
            ],
            "e_stats": [
                {
                    "count": 11,
                    "count_case": 11,
                    "src": 0,
                    "dst": 1
                },
                {
                    "count": 10,
                    "count_case": 10,
                    "src": 1,
                    "dst": 0
                },
                {
                    "count": 10,
                    "count_case": 10,
                    "src": 1,
                    "dst": 1
                }
            ],
            "top": [
                {
                    "id": 0,
                    "top": [
                        {
                            "variant": [0,1,1,0],
                            "count": 10
                        },
                        {
                            "variant": [0,1],
                            "count": 1
                        }
                    ]
                },
                {
                    "id": 1,
                    "top": [
                        {
                            "variant": [0,1,1,0],
                            "count": 10
                        },
                        {
                            "variant": [0,1],
                            "count": 1
                        }
                    ]
                }
            ],
            "happy": {
                "variant": [0,1],
                "count": 1
            },
            "e_count": 3
        })json";
    match(e_s, rs);
}

TEST_F(CelonisVariantStatsTest, test_disable_top_variant_stats) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column(
            {{"a1", "a2", "a3", "a4", "a5", "a6", "a7", "a00", "a01", "a02"}, {"a1", "a2", "a1", "a2"}});

    auto weights = build_weight_column({1, 10});
    auto edge_count = ColumnHelper::create_const_column<TYPE_BIGINT>(5, col1->size());
    auto disable_top = ColumnHelper::create_const_column<TYPE_BOOLEAN>(true, col1->size());
    std::vector<const Column*> raw_columns;
    raw_columns.resize(4);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    raw_columns[2] = edge_count.get();
    raw_columns[3] = disable_top.get();
    ctx->set_constant_columns({nullptr, nullptr, edge_count, disable_top});
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state1->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string rs = slice.to_string();

    std::string e_s =
            R"json({
            "dict": [
                {
                    "id": 0,
                    "name": "a1"
                },
                {
                    "id": 1,
                    "name": "a2"
                },
                {
                    "id": 2,
                    "name": "a3"
                },
                {
                    "id": 3,
                    "name": "a4"
                },
                {
                    "id": 4,
                    "name": "a5"
                },
                {
                    "id": 5,
                    "name": "a6"
                },
                {
                    "id": 6,
                    "name": "a7"
                },
                {
                    "id": 7,
                    "name": "a00"
                },
                {
                    "id": 8,
                    "name": "a01"
                },
                {
                    "id": 9,
                    "name": "a02"
                }
            ],
            "a_stats": [
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 11,
                    "count_end": 0,
                    "id": 0
                },
                {
                    "count": 21,
                    "count_case": 11,
                    "count_start": 0,
                    "count_end": 10,
                    "id": 1
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 2
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 3
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 4
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 5
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 6
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 7
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 0,
                    "id": 8
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "count_start": 0,
                    "count_end": 1,
                    "id": 9
                }
            ],
            "e_count": 10,
            "e_stats": [
                {
                    "count": 1,
                    "count_case": 1,
                    "src": 7,
                    "dst": 8
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "src": 8,
                    "dst": 9
                },
                {
                    "count": 21,
                    "count_case": 11,
                    "src": 0,
                    "dst": 1
                },
                {
                    "count": 10,
                    "count_case": 10,
                    "src": 1,
                    "dst": 0
                },
                {
                    "count": 1,
                    "count_case": 1,
                    "src": 1,
                    "dst": 2
                }
            ]
        })json";
    match(e_s, rs);
}

TEST_F(CelonisVariantStatsTest, test_enable_proto_encoding) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column(
            {{"a1", "a2", "a3", "a4", "a5", "a6", "a7", "a00", "a01", "a02"}, {"a1", "a2", "a1", "a2"}});

    auto weights = build_weight_column({1, 10});
    auto edge_count = ColumnHelper::create_const_column<TYPE_BIGINT>(5, col1->size());
    auto disable_top = ColumnHelper::create_const_column<TYPE_BOOLEAN>(true, col1->size());
    auto enable_proto_encoding = ColumnHelper::create_const_column<TYPE_BOOLEAN>(true, col1->size());
    std::vector<const Column*> raw_columns;
    raw_columns.resize(5);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    raw_columns[2] = edge_count.get();
    raw_columns[3] = disable_top.get();
    raw_columns[4] = enable_proto_encoding.get();
    ctx->set_constant_columns({nullptr, nullptr, edge_count, disable_top, enable_proto_encoding});
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state1->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string encoded_string = slice.to_string();
    auto json_string = to_statistics_json_string(encoded_string);
    ASSERT_TRUE(json_string.has_value());
    EXPECT_EQ(
            "{\"dict\":[{\"id\":3,\"name\":\"a4\"},{\"id\":5,\"name\":\"a6\"},{\"id\":4,\"name\":\"a5\"},{\"id\":6,"
            "\"name\":\"a7\"},{\"id\":9,\"name\":\"a02\"},{\"id\":2,\"name\":\"a3\"},{\"id\":7,\"name\":\"a00\"},{"
            "\"id\":0,\"name\":\"a1\"},{\"id\":1,\"name\":\"a2\"},{\"id\":8,\"name\":\"a01\"}],\"aStats\":[{\"count\":"
            "\"21\",\"countCase\":\"11\",\"countStart\":\"11\",\"countEnd\":\"0\",\"id\":0},{\"count\":\"21\","
            "\"countCase\":\"11\",\"countStart\":\"0\",\"countEnd\":\"10\",\"id\":1},{\"count\":\"1\",\"countCase\":"
            "\"1\",\"countStart\":\"0\",\"countEnd\":\"0\",\"id\":2},{\"count\":\"1\",\"countCase\":\"1\","
            "\"countStart\":\"0\",\"countEnd\":\"0\",\"id\":3},{\"count\":\"1\",\"countCase\":\"1\",\"countStart\":"
            "\"0\",\"countEnd\":\"0\",\"id\":4},{\"count\":\"1\",\"countCase\":\"1\",\"countStart\":\"0\",\"countEnd\":"
            "\"0\",\"id\":5},{\"count\":\"1\",\"countCase\":\"1\",\"countStart\":\"0\",\"countEnd\":\"0\",\"id\":6},{"
            "\"count\":\"1\",\"countCase\":\"1\",\"countStart\":\"0\",\"countEnd\":\"0\",\"id\":7},{\"count\":\"1\","
            "\"countCase\":\"1\",\"countStart\":\"0\",\"countEnd\":\"0\",\"id\":8},{\"count\":\"1\",\"countCase\":"
            "\"1\",\"countStart\":\"0\",\"countEnd\":\"1\",\"id\":9}],\"eCount\":\"10\",\"eStats\":[{\"count\":\"1\","
            "\"countCase\":\"1\",\"src\":7,\"dst\":8},{\"count\":\"1\",\"countCase\":\"1\",\"src\":8,\"dst\":9},{"
            "\"count\":\"21\",\"countCase\":\"11\",\"src\":0,\"dst\":1},{\"count\":\"10\",\"countCase\":\"10\",\"src\":"
            "1,\"dst\":0},{\"count\":\"1\",\"countCase\":\"1\",\"src\":1,\"dst\":2}]}",
            json_string.value());
}

TEST_F(CelonisVariantStatsTest, test_enable_proto_encoding_empty) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{}});

    auto weights = build_weight_column({1});
    auto edge_count = ColumnHelper::create_const_column<TYPE_BIGINT>(5, col1->size());
    auto disable_top = ColumnHelper::create_const_column<TYPE_BOOLEAN>(true, col1->size());
    auto enable_proto_encoding = ColumnHelper::create_const_column<TYPE_BOOLEAN>(true, col1->size());
    std::vector<const Column*> raw_columns;
    raw_columns.resize(5);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    raw_columns[2] = edge_count.get();
    raw_columns[3] = disable_top.get();
    raw_columns[4] = enable_proto_encoding.get();
    ctx->set_constant_columns({nullptr, nullptr, edge_count, disable_top, enable_proto_encoding});
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    // Get the result
    auto result = BinaryColumn::create();
    func->finalize_to_column(ctx, state1->state(), result.get());
    EXPECT_EQ(result->size(), 1);

    Slice slice = result->get_slice(0);
    std::string encoded_string = slice.to_string();
    EXPECT_EQ("", encoded_string);
}

TEST_F(CelonisVariantStatsTest, test_cancellation_work) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column(
            {{"a1", "a2", "a3", "a4", "a5", "a6", "a7", "a00", "a01", "a02"}, {"a1", "a2", "a1", "a2"}});

    auto weights = build_weight_column({1, 10});
    auto edge_count = ColumnHelper::create_const_column<TYPE_BIGINT>(5, col1->size());
    auto disable_top = ColumnHelper::create_const_column<TYPE_BOOLEAN>(true, col1->size());
    auto enable_proto_encoding = ColumnHelper::create_const_column<TYPE_BOOLEAN>(true, col1->size());
    std::vector<const Column*> raw_columns;
    raw_columns.resize(5);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    raw_columns[2] = edge_count.get();
    raw_columns[3] = disable_top.get();
    raw_columns[4] = enable_proto_encoding.get();
    ctx->set_constant_columns({nullptr, nullptr, edge_count, disable_top, enable_proto_encoding});
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    auto result = BinaryColumn::create();
    // set is_cancelled to true
    ctx->state()->set_is_cancelled(true);
    func->finalize_to_column(ctx, state1->state(), result.get());
    ASSERT_TRUE(ctx->has_error());
}

} // namespace starrocks
