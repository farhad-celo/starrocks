#include <algorithm>
#include <gtest/gtest.h>

#include "../util.h"
#include "column/struct_column.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/agg/calc_string_bucket_boundaries.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"

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

class CelonisCalcStringBucketCountBoundariesTest : public testing::Test {
protected:
    CelonisCalcStringBucketCountBoundariesTest() = default;

    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor get_return_type() {
        return celonis::array_type(TYPE_VARCHAR);
    }

    std::unique_ptr<FunctionContext> get_ctx() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                TypeDescriptor::from_logical_type(TYPE_LARGEINT),
                TypeDescriptor::from_logical_type(TYPE_BIGINT),
                TypeDescriptor::from_logical_type(TYPE_DOUBLE)
        };
        auto return_type = get_return_type();
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_test_context(std::move(arg_types), return_type));
    }

    void Evaluate(Column* result, const std::optional<Datum>& expected) {
        if (!expected.has_value()) {
            EXPECT_EQ(result->size(), 0);
            return;
        }
        ASSERT_EQ(result->size(), 1);
        if (expected->is_null()) {
            EXPECT_TRUE(result->is_null(0));
            return;
        }
        ASSERT_FALSE(result->is_null(0));

        auto expected_array = expected->get_array();
        auto result_array = result->get(0).get_array();
        ASSERT_EQ(result_array.size(), expected_array.size());
        for (int i = 0; i < expected_array.size(); ++i) {
            auto debug_string = [&]() {
                return fmt::format("index: {}", i);
            };
            if (expected_array[i].is_null()) {
                EXPECT_TRUE(result_array[i].is_null()) << debug_string();
            } else if (result_array[i].is_null()) {
                EXPECT_FALSE(result_array[i].is_null()) << debug_string();
            } else {
                EXPECT_EQ(result_array[i].get_slice().to_string(), expected_array[i].get_slice().to_string())
                                    << debug_string();
            }
        }
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*>
    RunUpdate(const std::vector<std::optional<std::string>>& strings,
              const std::vector<std::optional<int128_t>>& hashes, int64_t count,
              double sample_ratio) {
        auto local_ctx = get_ctx();

        const AggregateFunction* func =
                get_aggregate_function("celonis_calc_string_bucket_count_boundaries", TYPE_VARCHAR, TYPE_ARRAY, false);

        auto string_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
        for (const auto& str: strings) {
            if (str.has_value()) {
                string_col->append_datum(Slice(str.value()));
            } else {
                string_col->append_datum(kNullDatum);
            }
        }

        auto hash_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_LARGEINT), true);
        for (const auto& hash128: hashes) {
            if (hash128.has_value()) {
                hash_col->append_datum(hash128.value());
            } else {
                hash_col->append_datum(kNullDatum);
            }
        }

        auto count_col = ColumnHelper::create_const_column<TYPE_BIGINT>(count, strings.size());
        auto sample_ratio_col = ColumnHelper::create_const_column<TYPE_DOUBLE>(sample_ratio, strings.size());

        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = string_col.get();
        raw_columns[1] = hash_col.get();
        raw_columns[2] = count_col.get();
        raw_columns[3] = sample_ratio_col.get();
        local_ctx->set_constant_columns({nullptr, nullptr, count_col, sample_ratio_col});

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), strings.size(), raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    void RunMerge(const std::vector<std::optional<std::string>>& strings1,
                  const std::vector<std::optional<std::string>>& strings2,
                  const std::vector<std::optional<int128_t>>& hashes1,
                  const std::vector<std::optional<int128_t>>& hashes2, int64_t count, double sample_ratio,
                  const std::optional<Datum>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(strings1, hashes1, count, sample_ratio);
        auto [local_ctx2, state2, func2] = RunUpdate(strings2, hashes2, count, sample_ratio);

        // Serialize state2
        ColumnPtr serde_col = BinaryColumn::create();
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge state2 into state1
        func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

        // Get the result
        auto result = ColumnHelper::create_column(get_return_type(), true);
        func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

        Evaluate(result.get(), expected);
    }

    void RunMergeToNew(const std::vector<std::optional<std::string>>& strings1,
                       const std::vector<std::optional<std::string>>& strings2,
                       const std::vector<std::optional<int128_t>>& hashes1,
                       const std::vector<std::optional<int128_t>>& hashes2, int64_t count, double sample_ratio,
                       const std::optional<Datum>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(strings1, hashes1, count, sample_ratio);
        auto [local_ctx2, state2, func2] = RunUpdate(strings2, hashes2, count, sample_ratio);

        auto local_ctx3 = get_ctx();

        // Serialize state1 and state2
        // Use nullable, because SR prepares nullable *to* column for serialize_to_column.
        auto serde_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge to a new state
        auto state3 = ManagedAggrState::create(local_ctx3.get(), func);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);

        // Get the result
        auto result = local_ctx3->create_column(local_ctx3->get_return_type(), false);
        func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());

        Evaluate(result.get(), expected);
    }

    void Run(const std::vector<std::optional<std::string>>& strings1,
             const std::vector<std::optional<std::string>>& strings2,
             const std::vector<std::optional<int128_t>>& hashes1,
             const std::vector<std::optional<int128_t>>& hashes2, int64_t count, double sample_ratio,
             const std::optional<Datum>& expected) {
        RunMerge(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
        RunMergeToNew(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
    }

    std::vector<std::unique_ptr<MemPool>> mem_pools_;
};

TEST_F(CelonisCalcStringBucketCountBoundariesTest, even_number_of_strings_1) {
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c", "d", "e"};
    std::vector<std::optional<std::string>> strings2 = {"f", "g", "h", "i", "j"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3, 4, 5};
    std::vector<std::optional<int128_t>> hashes2 = {6, 7, 8, 9, 10};
    int64_t count = 10;
    double sample_ratio = 1.0;
    auto expected = DatumArray{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, even_number_of_strings_2) {
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c", "d", "e"};
    std::vector<std::optional<std::string>> strings2 = {"f", "g", "h", "i", "j"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3, 4, 5};
    std::vector<std::optional<int128_t>> hashes2 = {6, 7, 8, 9, 10};
    int64_t count = 5;
    double sample_ratio = 1.0;
    auto expected = DatumArray{"a", "c", "e", "g", "i"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, empty_string_works) {
    std::vector<std::optional<std::string>> strings1 = {"", "b", "c", "d", "e"};
    std::vector<std::optional<std::string>> strings2 = {"f", "g", "h", "i", "j"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3, 4, 5};
    std::vector<std::optional<int128_t>> hashes2 = {6, 7, 8, 9, 10};
    int64_t count = 5;
    double sample_ratio = 1.0;
    auto expected = DatumArray{"", "c", "e", "g", "i"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, odd_number_of_strings_1) {
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c"};
    std::vector<std::optional<std::string>> strings2 = {"d", "e"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3};
    std::vector<std::optional<int128_t>> hashes2 = {4, 5};
    int64_t count = 5;
    double sample_ratio = 1.0;
    auto expected = DatumArray{"a", "b", "c", "d", "e"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, odd_number_of_strings_2) {
    std::vector<std::optional<std::string>> strings1 = {"apple", "bus", "car"};
    std::vector<std::optional<std::string>> strings2 = {"dog", "eye"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3};
    std::vector<std::optional<int128_t>> hashes2 = {4, 5};
    int64_t count = 2;
    double sample_ratio = 1.0;
    auto expected = DatumArray{"apple", "dog"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, non_positive_count) {
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c", "d", "e"};
    std::vector<std::optional<std::string>> strings2 = {"f", "g", "h", "i", "j"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3, 4, 5};
    std::vector<std::optional<int128_t>> hashes2 = {6, 7, 8, 9, 10};
    // If count is not positive, the implementation will use the default value (count = 10).
    int64_t count = -1;
    double sample_ratio = 1.0;
    auto expected = DatumArray{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, negative_sample_ratio) {
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c", "d", "e"};
    std::vector<std::optional<std::string>> strings2 = {"f", "g", "h", "i", "j"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3, 4, 5};
    std::vector<std::optional<int128_t>> hashes2 = {6, 7, 8, 9, 10};
    int64_t count = 5;
    double sample_ratio = -0.2;
    auto expected = DatumArray{"a", "c", "e", "g", "i"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, sample_ratio_greater_than_one) {
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c", "d", "e"};
    std::vector<std::optional<std::string>> strings2 = {"f", "g", "h", "i", "j"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3, 4, 5};
    std::vector<std::optional<int128_t>> hashes2 = {6, 7, 8, 9, 10};
    int64_t count = 5;
    double sample_ratio = 1.2;
    auto expected = DatumArray{"a", "c", "e", "g", "i"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, duplicate_strings) {
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c", "a", "b", "c"};
    std::vector<std::optional<std::string>> strings2 = {"d", "e", "d", "e"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3, 1, 2, 3};
    std::vector<std::optional<int128_t>> hashes2 = {4, 5, 4, 5};
    int64_t count = 2;
    double sample_ratio = 1.0;
    auto expected = DatumArray{"a", "d"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, null_strings_are_ignored) {
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c", "d", "e", std::nullopt};
    std::vector<std::optional<std::string>> strings2 = {"f", "g", "h", "i", "j", std::nullopt};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3, 4, 5, 0};
    std::vector<std::optional<int128_t>> hashes2 = {6, 7, 8, 9, 10, 0};
    int64_t count = 5;
    double sample_ratio = 1.0;
    auto expected = DatumArray{"a", "c", "e", "g", "i"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, null_hashes_are_ignored) {
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c", "d", "e", "x"};
    std::vector<std::optional<std::string>> strings2 = {"f", "g", "h", "i", "j", "y"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3, 4, 5, std::nullopt};
    std::vector<std::optional<int128_t>> hashes2 = {6, 7, 8, 9, 10, std::nullopt};
    int64_t count = 5;
    double sample_ratio = 1.0;
    auto expected = DatumArray{"a", "c", "e", "g", "i"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, DISABLED_too_many_buckets) {
    // 2 * length > MAX_NUM_BUCKETS
    const int64_t length = 550000;
    std::vector<std::optional<std::string>> strings1;
    std::vector<std::optional<std::string>> strings2;
    std::vector<std::optional<int128_t>> hashes1;
    std::vector<std::optional<int128_t>> hashes2;
    strings1.reserve(length);
    strings2.reserve(length);
    hashes1.reserve(length);
    hashes2.reserve(length);
    for (int64_t i = 0; i < length; ++i) {
        hashes1.emplace_back(i);
        strings1.emplace_back(std::to_string(i));
    }
    for (int64_t i = 0; i < length; ++i) {
        hashes2.emplace_back(i + length);
        strings2.emplace_back(std::to_string(i + length));
    }
    int64_t count = 1050000;
    double sample_ratio = 1.0;
    auto expected = std::nullopt;

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, sampling_works_1) {
    int128_t hash_to_drop = std::numeric_limits<int128_t>::max() / 10 * 3;
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c"};
    // "e" is dropped
    std::vector<std::optional<std::string>> strings2 = {"d", "e", "f"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, 3};
    std::vector<std::optional<int128_t>> hashes2 = {4, hash_to_drop, 5};
    int64_t count = 5;
    double sample_ratio = 0.2;
    auto expected = DatumArray{"a", "b", "c", "d", "f"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, sampling_works_2) {
    int128_t hash_to_drop = std::numeric_limits<int128_t>::max() / 10 * 6;
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c", "g"};
    // "c" and "e" are dropped
    std::vector<std::optional<std::string>> strings2 = {"d", "e", "f"};
    std::vector<std::optional<int128_t>> hashes1 = {1, 2, hash_to_drop + 100000, 3};
    std::vector<std::optional<int128_t>> hashes2 = {4, hash_to_drop, 5};
    int64_t count = 5;
    double sample_ratio = 0.5;
    auto expected = DatumArray{"a", "b", "d", "f", "g"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

TEST_F(CelonisCalcStringBucketCountBoundariesTest, min_and_max_are_tracked) {
    int128_t hash_to_drop = std::numeric_limits<int128_t>::max() / 10 * 3;
    std::vector<std::optional<std::string>> strings1 = {"a", "b", "c"};
    std::vector<std::optional<std::string>> strings2 = {"d", "e", "f"};
    // "a" and "f" are not dropped
    std::vector<std::optional<int128_t>> hashes1 = {hash_to_drop, 2, 3};
    std::vector<std::optional<int128_t>> hashes2 = {4, 5, hash_to_drop + 50000};
    int64_t count = 6;
    double sample_ratio = 0.2;
    auto expected = DatumArray{"a", "b", "c", "d", "e", "f"};

    Run(strings1, strings2, hashes1, hashes2, count, sample_ratio, expected);
}

} // namespace starrocks
