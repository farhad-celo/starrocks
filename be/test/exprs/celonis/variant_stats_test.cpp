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
#include "runtime/mem_pool.h"
#include "runtime/time_types.h"
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

class VariantStatsTest : public testing::Test {
public:
    VariantStatsTest() = default;

    void SetUp() override {
        utils = new FunctionUtils();
        ctx = utils->get_fn_ctx();
    }

    void TearDown() override { delete utils; }

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

        std::vector<std::vector<std::string> > seed_data;
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

private:
    FunctionUtils* utils{};
    FunctionContext* ctx{};
};

// TODO(hagonzal): Write a helper function to check the correctness of results. Comparing json is fragile.

TEST_F(VariantStatsTest, test_merge_with_itself) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col = build_variant_column({{},
                                     {"key1", "key2"},
                                     {"sr-1", "sr-2", "sr-2"}});

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
    std::cout << rs << "\n";
    format_result(rs);
    std::cout << rs << "\n";
    EXPECT_EQ(rs,
              "{'dict':[{'id':3,'name':'sr-2'},{'id':2,'name':'sr-1'},{'id':0,'name':'key1'},{'id':1,'name':'key2'}],'a_stats':[{'count':2,'count_case':2,'count_start':2,'count_end':0,'id':0},{'count':2,'count_case':2,'count_start':0,'count_end':2,'id':1},{'count':2,'count_case':2,'count_start':2,'count_end':0,'id':2},{'count':4,'count_case':2,'count_start':0,'count_end':2,'id':3}],'e_stats':[{'count':2,'count_case':2,'src':0,'dst':1},{'count':2,'count_case':2,'src':2,'dst':3},{'count':2,'count_case':2,'src':3,'dst':3}],'top':[{'id':0,'top':[{'variant':[0,1],'count':2}]},{'id':1,'top':[{'variant':[0,1],'count':2}]},{'id':2,'top':[{'variant':[2,3,3],'count':2}]},{'id':3,'top':[{'variant':[2,3,3],'count':2}]}],'happy':{'variant':[0,1],'count':2}}");
}

TEST_F(VariantStatsTest, test_merge_distinct_dict) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"},
                                      {"a1", "a2", "a3"},
                                      {"a3", "a4"}});
    auto weights = build_weight_column({1, 1, 1});
    std::vector<const Column*> raw_columns;
    raw_columns.resize(2);
    raw_columns[0] = col1.get();
    raw_columns[1] = weights.get();
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    auto part1 = BinaryColumn::create();
    func->serialize_to_column(ctx, state1->state(), part1.get());

    auto col2 = build_variant_column({{"a1", "a4", "a0"},
                                      {"a1", "a2", "a2", "a2", "a5"}});
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
    std::cout << rs << "\n";
    format_result(rs);
    std::cout << rs << "\n";

    EXPECT_EQ(rs,
              "{'dict':[{'id':3,'name':'a4'},{'id':2,'name':'a3'},{'id':4,'name':'a0'},{'id':0,'name':'a1'},{'id':5,'name':'a5'},{'id':1,'name':'a2'}],'a_stats':[{'count':4,'count_case':4,'count_start':4,'count_end':0,'id':0},{'count':5,'count_case':3,'count_start':0,'count_end':1,'id':1},{'count':2,'count_case':2,'count_start':1,'count_end':1,'id':2},{'count':2,'count_case':2,'count_start':0,'count_end':1,'id':3},{'count':1,'count_case':1,'count_start':0,'count_end':1,'id':4},{'count':1,'count_case':1,'count_start':0,'count_end':1,'id':5}],'e_stats':[{'count':1,'count_case':1,'src':1,'dst':2},{'count':1,'count_case':1,'src':2,'dst':3},{'count':1,'count_case':1,'src':0,'dst':3},{'count':3,'count_case':3,'src':0,'dst':1},{'count':1,'count_case':1,'src':3,'dst':4},{'count':2,'count_case':1,'src':1,'dst':1},{'count':1,'count_case':1,'src':1,'dst':5}],'top':[{'id':0,'top':[{'variant':[0,1,2],'count':1},{'variant':[0,1,1,1,5],'count':1},{'variant':[0,3,4],'count':1},{'variant':[0,1],'count':1}]},{'id':1,'top':[{'variant':[0,1,2],'count':1},{'variant':[0,1,1,1,5],'count':1},{'variant':[0,1],'count':1}]},{'id':2,'top':[{'variant':[0,1,2],'count':1},{'variant':[2,3],'count':1}]},{'id':3,'top':[{'variant':[0,3,4],'count':1},{'variant':[2,3],'count':1}]},{'id':4,'top':[{'variant':[0,3,4],'count':1}]},{'id':5,'top':[{'variant':[0,1,1,1,5],'count':1}]}],'happy':{'variant':[0,1],'count':1}}");
}

TEST_F(VariantStatsTest, test_no_merge) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"},
                                      {"a1", "a2"},
                                      {"a1", "a2", "a3"},
                                      {"a3", "a4"}});

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
    std::cout << rs << "\n";
    format_result(rs);
    std::cout << rs << "\n";

    EXPECT_EQ(rs,
              "{'dict':[{'id':3,'name':'a4'},{'id':2,'name':'a3'},{'id':0,'name':'a1'},{'id':1,'name':'a2'}],'a_stats':[{'count':3,'count_case':3,'count_start':3,'count_end':0,'id':0},{'count':3,'count_case':3,'count_start':0,'count_end':2,'id':1},{'count':2,'count_case':2,'count_start':1,'count_end':1,'id':2},{'count':1,'count_case':1,'count_start':0,'count_end':1,'id':3}],'e_stats':[{'count':3,'count_case':3,'src':0,'dst':1},{'count':1,'count_case':1,'src':1,'dst':2},{'count':1,'count_case':1,'src':2,'dst':3}],'top':[{'id':0,'top':[{'variant':[0,1],'count':2},{'variant':[0,1,2],'count':1}]},{'id':1,'top':[{'variant':[0,1],'count':2},{'variant':[0,1,2],'count':1}]},{'id':2,'top':[{'variant':[0,1,2],'count':1},{'variant':[2,3],'count':1}]},{'id':3,'top':[{'variant':[2,3],'count':1}]}],'happy':{'variant':[0,1],'count':2}}");
}

TEST_F(VariantStatsTest, test_weights) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"},
                                      {"a1", "a2"},
                                      {"a1", "a2", "a2"},
                                      {"a3", "a4"}});

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
    std::cout << rs << "\n";
    format_result(rs);
    std::cout << rs << "\n";

    EXPECT_EQ(rs,
              "{'dict':[{'id':3,'name':'a4'},{'id':2,'name':'a3'},{'id':0,'name':'a1'},{'id':1,'name':'a2'}],'a_stats':[{'count':5,'count_case':5,'count_start':5,'count_end':0,'id':0},{'count':8,'count_case':5,'count_start':0,'count_end':5,'id':1},{'count':2,'count_case':2,'count_start':2,'count_end':0,'id':2},{'count':2,'count_case':2,'count_start':0,'count_end':2,'id':3}],'e_stats':[{'count':5,'count_case':5,'src':0,'dst':1},{'count':3,'count_case':3,'src':1,'dst':1},{'count':2,'count_case':2,'src':2,'dst':3}],'top':[{'id':0,'top':[{'variant':[0,1,1],'count':3},{'variant':[0,1],'count':2}]},{'id':1,'top':[{'variant':[0,1,1],'count':3},{'variant':[0,1],'count':2}]},{'id':2,'top':[{'variant':[2,3],'count':2}]},{'id':3,'top':[{'variant':[2,3],'count':2}]}],'happy':{'variant':[0,1,1],'count':3}}");
}

TEST_F(VariantStatsTest, test_empty) {
    {
        // No data.
        const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR,
                                                               false);
        auto col1 = build_variant_column({});
        auto weights = build_weight_column({});
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
        // Only empty variant.
        const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR,
                                                               false);
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
        const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR,
                                                               false);
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
        format_result(rs);
        std::cout << rs << "\n";
        EXPECT_EQ(rs,
                  "{'dict':[{'id':0,'name':'a1'}],'a_stats':[{'count':1,'count_case':1,'count_start':1,'count_end':0,'id':0}],'e_stats':[],'top':[{'id':0,'top':[{'variant':[0],'count':1}]}],'happy':{'variant':[0],'count':1}}");
    }

    {
        // Single activity with self loop.
        const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR,
                                                               false);
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
        format_result(rs);
        std::cout << rs << "\n";
        EXPECT_EQ(rs,
                  "{'dict':[{'id':0,'name':'a1'}],'a_stats':[{'count':2,'count_case':1,'count_start':1,'count_end':1,'id':0}],'e_stats':[{'count':1,'count_case':1,'src':0,'dst':0}],'top':[{'id':0,'top':[{'variant':[0,0],'count':1}]}],'happy':{'variant':[0,0],'count':1}}");
    }
}

TEST_F(VariantStatsTest, test_null_activity) {
    // null activity.
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR,
                                                           false);
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
    format_result(rs);
    std::cout << rs << "\n";
    EXPECT_EQ(rs, "{'dict':[{'id':1,'name':'b'},{'id':0,'name':'a'}],'a_stats':[{'count':1,'count_case':1,'count_start':1,'count_end':0,'id':0},{'count':1,'count_case':1,'count_start':0,'count_end':1,'id':1}],'e_stats':[{'count':1,'count_case':1,'src':0,'dst':1}],'top':[{'id':0,'top':[{'variant':[0,1],'count':1}]},{'id':1,'top':[{'variant':[0,1],'count':1}]}],'happy':{'variant':[0,1],'count':1}}");
}

TEST_F(VariantStatsTest, test_large) {
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

} // namespace starrocks
