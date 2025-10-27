#include <gtest/gtest.h>

#include <random>

#include "column/array_column.h"
#include "column/column_builder.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/celonis/agg/variant_stats.h"
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

    AggDataPtr state() const { return _state; }

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

class CelonisVariantAggTest : public testing::Test {
public:
    using Rows = std::vector<std::vector<std::string>>;
    using Weights = std::vector<size_t>;
    using DistinctVariant = std::vector<Slice>;
    using DistinctVariantMap = std::map<DistinctVariant, int32_t>;

    CelonisVariantAggTest() = default;

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

    Column::Ptr build_weight_column(const std::vector<size_t>& weight) {
        ColumnBuilder<TYPE_BIGINT> builder(config::vector_chunk_size);

        for (int i = 0; i < weight.size(); i++) {
            builder.append(weight[i]);
        }
        return builder.build(false);
    }

    DistinctVariantMap get_actual_distinct_variant_map(const ManagedAggrState& managed_state) {
        auto state = *reinterpret_cast<VariantAggregateState*>(managed_state.state());

        std::map<int32_t, Slice> activity_map_inverted;
        for (auto [slice, id] : state.activity_map()) {
            const auto [it, success] = activity_map_inverted.insert({id, slice});
            EXPECT_TRUE(success);
        }

        DistinctVariantMap result;
        for (const auto& [variant, count] : state.variant_map()) {
            DistinctVariant dv;
            for (auto activity_id : variant.data) {
                dv.push_back(activity_map_inverted[activity_id]);
            }
            result[dv] = count;
        }
        EXPECT_EQ(result.size(), state.variant_map().size());

        return result;
    }

    void match(const ManagedAggrState& state, const DistinctVariantMap& expected) {
        DistinctVariantMap actual = get_actual_distinct_variant_map(state);
        EXPECT_EQ(actual, expected);
    }

    void run_no_merge_test(const Rows& rows, const Weights& weights, const DistinctVariantMap& expected) {
        const AggregateFunction* func =
                get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

        auto col = build_variant_column(rows);
        auto weight_col = build_weight_column(weights);
        std::vector<const Column*> raw_columns{col.get(), weight_col.get()};
        auto state = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, col->size(), raw_columns.data(), state->state());

        match(*state, expected);
    }

private:
    FunctionUtils* utils{};
    FunctionContext* ctx{};
};

TEST_F(CelonisVariantAggTest, test_no_merge) {
    Rows rows = {{"a1", "a2"}, {"a1", "a2"}, {"a1", "a2", "a3"}, {"a3", "a4"}};
    Weights weights = {1, 1, 1, 1};
    DistinctVariantMap expected = {{{"a1", "a2"}, 2}, {{"a1", "a2", "a3"}, 1}, {{"a3", "a4"}, 1}};
    run_no_merge_test(rows, weights, expected);
}

TEST_F(CelonisVariantAggTest, test_merge_with_itself) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col = build_variant_column({{}, {"key1", "key2"}, {"sr-1", "sr-2", "sr-2"}});

    auto weights = build_weight_column({1, 1, 1});

    std::vector<const Column*> raw_columns{col.get(), weights.get()};
    auto state = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col->size(), raw_columns.data(), state->state());

    // Serialize the column
    auto part1 = BinaryColumn::create();
    func->serialize_to_column(ctx, state->state(), part1.get());

    // Merge with itself
    func->merge(ctx, part1.get(), state->state(), 0);

    DistinctVariantMap expected = {{{}, 2}, {{"key1", "key2"}, 2}, {{"sr-1", "sr-2", "sr-2"}, 2}};
    match(*state, expected);
}

TEST_F(CelonisVariantAggTest, test_merge_distinct_dict) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"}, {"a1", "a2", "a3"}, {"a3", "a4"}});
    auto weights1 = build_weight_column({1, 1, 1});
    std::vector<const Column*> raw_columns{col1.get(), weights1.get()};
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    auto part1 = BinaryColumn::create();
    func->serialize_to_column(ctx, state1->state(), part1.get());

    auto col2 = build_variant_column({{"a1", "a4", "a0"}, {"a1", "a2", "a2", "a2", "a5"}});
    auto weights2 = build_weight_column({1, 1});

    std::vector<const Column*> raw_columns2{col2.get(), weights2.get()};
    auto state2 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col2->size(), raw_columns2.data(), state2->state());
    auto part2 = BinaryColumn::create();
    func->serialize_to_column(ctx, state2->state(), part2.get());

    // Merge part1 and part2
    func->merge(ctx, part2.get(), state1->state(), 0);

    DistinctVariantMap expected = {{{"a1", "a2"}, 1},
                                   {{"a1", "a2", "a3"}, 1},
                                   {{"a3", "a4"}, 1},
                                   {{"a1", "a4", "a0"}, 1},
                                   {{"a1", "a2", "a2", "a2", "a5"}, 1}};
    match(*state1, expected);
}

TEST_F(CelonisVariantAggTest, test_weights) {
    Rows rows = {{"a1", "a2"}, {"a1", "a2"}, {"a1", "a2", "a2"}, {"a3", "a4"}};
    Weights weights = {1, 10, 3, 2};
    DistinctVariantMap expected = {{{"a1", "a2"}, 11}, {{"a1", "a2", "a2"}, 3}, {{"a3", "a4"}, 2}};
    run_no_merge_test(rows, weights, expected);
}

TEST_F(CelonisVariantAggTest, test_merge_weights) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"}, {"a1", "a2", "a3"}, {"a3", "a4"}});
    auto weights1 = build_weight_column({10, 100, 20});
    std::vector<const Column*> raw_columns{col1.get(), weights1.get()};
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    auto part1 = BinaryColumn::create();
    func->serialize_to_column(ctx, state1->state(), part1.get());

    auto col2 = build_variant_column(
            {{"a1", "a4", "a0"}, {"a1", "a2", "a3"}, {"a3", "a4"}, {"a1", "a2", "a2", "a2", "a5"}});
    auto weights2 = build_weight_column({20, 50, 5, 1});

    std::vector<const Column*> raw_columns2{col2.get(), weights2.get()};
    auto state2 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col2->size(), raw_columns2.data(), state2->state());
    auto part2 = BinaryColumn::create();
    func->serialize_to_column(ctx, state2->state(), part2.get());

    // Merge part1 and part2
    func->merge(ctx, part2.get(), state1->state(), 0);

    DistinctVariantMap expected = {{{"a1", "a2"}, 10},
                                   {{"a1", "a2", "a3"}, 150},
                                   {{"a3", "a4"}, 25},
                                   {{"a1", "a4", "a0"}, 20},
                                   {{"a1", "a2", "a2", "a2", "a5"}, 1}};
    match(*state1, expected);
}

TEST_F(CelonisVariantAggTest, test_merge_very_large_weights) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_variant_column({{"a1", "a2"}, {"a1", "a2", "a3"}, {"a3", "a4"}});
    auto weights1 = build_weight_column({10'000'000'000L, 100'000'000'000L, 20'000'000'000L});
    std::vector<const Column*> raw_columns{col1.get(), weights1.get()};
    auto state1 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns.data(), state1->state());

    auto part1 = BinaryColumn::create();
    func->serialize_to_column(ctx, state1->state(), part1.get());

    auto col2 = build_variant_column(
            {{"a1", "a4", "a0"}, {"a1", "a2", "a3"}, {"a3", "a4"}, {"a1", "a2", "a2", "a2", "a5"}});
    auto weights2 = build_weight_column({20'000'000'000L, 50'000'000'000L, 50L, 1L});

    std::vector<const Column*> raw_columns2{col2.get(), weights2.get()};
    auto state2 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col2->size(), raw_columns2.data(), state2->state());
    auto part2 = BinaryColumn::create();
    func->serialize_to_column(ctx, state2->state(), part2.get());

    // Merge part1 and part2
    func->merge(ctx, part2.get(), state1->state(), 0);

    DistinctVariantMap expected = {{{"a1", "a2"}, 10'000'000'000L},
                                   {{"a1", "a2", "a3"}, 150'000'000'000L},
                                   {{"a3", "a4"}, 20'000'000'050L},
                                   {{"a1", "a4", "a0"}, 20'000'000'000L},
                                   {{"a1", "a2", "a2", "a2", "a5"}, 1L}};
    match(*state1, expected);
}

TEST_F(CelonisVariantAggTest, test_empty) {
    {
        // No data.
        Rows rows = {};
        Weights weights = {};
        DistinctVariantMap expected = {};
        run_no_merge_test(rows, weights, expected);
    }

    {
        // Only empty variant.
        Rows rows = {{}};
        Weights weights = {1};
        DistinctVariantMap expected = {{{}, 1}};
        run_no_merge_test(rows, weights, expected);
    }

    {
        // Single activity.
        Rows rows = {{"a1"}};
        Weights weights = {1};
        DistinctVariantMap expected = {{{"a1"}, 1}};
        run_no_merge_test(rows, weights, expected);
    }

    {
        // Single activity with self loop.
        Rows rows = {{"a1", "a1"}};
        Weights weights = {1};
        DistinctVariantMap expected = {{{"a1", "a1"}, 1}};
        run_no_merge_test(rows, weights, expected);
    }
}

TEST_F(CelonisVariantAggTest, test_null_activity) {
    // null activity.
    Rows rows = {{"a", "null", "b"}};
    Weights weights = {1};
    DistinctVariantMap expected = {{{"a", "b"}, 1}};
    run_no_merge_test(rows, weights, expected);
}

TEST_F(CelonisVariantAggTest, test_large) {
    const AggregateFunction* func = get_aggregate_function("celonis_variant_stats", TYPE_ARRAY, TYPE_VARCHAR, false);

    auto col1 = build_random_variant_column(100, 10000, 10);
    auto col2 = build_random_variant_column(100, 10000, 10);
    auto weight_column = ColumnHelper::create_const_column<TYPE_BIGINT>(1, 1);

    // compute stats and serialize for col1
    std::vector<const Column*> raw_columns1{col1.get(), weight_column.get()};
    auto state = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col1->size(), raw_columns1.data(), state->state());
    auto part1 = BinaryColumn::create();
    func->serialize_to_column(ctx, state->state(), part1.get());

    // compute stats and serialize for col2
    std::vector<const Column*> raw_columns2{col2.get(), weight_column.get()};
    auto state2 = ManagedAggrState::create(ctx, func);
    func->update_batch_single_state(ctx, col2->size(), raw_columns2.data(), state2->state());
    auto part2 = BinaryColumn::create();
    func->serialize_to_column(ctx, state2->state(), part2.get());

    // Merge
    func->merge(ctx, part2.get(), state->state(), 0);

    DistinctVariantMap actual = get_actual_distinct_variant_map(*state);
    for (const auto& [variant, count] : actual) {
        std::cout << "variant:";
        for (const auto& activity : variant) {
            std::cout << " " << activity;
        }
        std::cout << ", count: " << count << "\n";
    }
}

} // namespace starrocks
