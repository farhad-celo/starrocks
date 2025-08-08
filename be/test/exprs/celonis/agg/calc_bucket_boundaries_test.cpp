#include <algorithm>
#include <gtest/gtest.h>

#include "../util.h"
#include "column/struct_column.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/agg/calc_bucket_boundaries.h"
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

class CelonisCalcBucketCountBoundariesTest : public testing::Test {
protected:
    CelonisCalcBucketCountBoundariesTest() = default;

    void SetUp() override {}

    void TearDown() override {}

    TypeDescriptor get_return_type(LogicalType logical_type) {
        return celonis::array_type(logical_type);
    }

    std::unique_ptr<FunctionContext> get_ctx(LogicalType logical_type) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                TypeDescriptor::from_logical_type(logical_type), // input
                TypeDescriptor::from_logical_type(TYPE_BIGINT)   // count
        };
        auto return_type = get_return_type(logical_type);
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_test_context(std::move(arg_types), return_type));
    }

    template<LogicalType LT>
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
                EXPECT_EQ(result_array[i].get<RunTimeCppType<LT>>(), expected_array[i].get<RunTimeCppType<LT>>())
                                    << debug_string();
            }
        }
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*>
    RunUpdate(LogicalType logical_type, const DatumArray& input, int count) {
        auto local_ctx = get_ctx(logical_type);

        const AggregateFunction* func =
                get_aggregate_function("celonis_calc_bucket_count_boundaries", logical_type, TYPE_ARRAY, false);

        auto input_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(logical_type), true);
        for (const auto& datum: input) {
            input_col->append_datum(datum);
        }

        auto count_col = ColumnHelper::create_const_column<TYPE_BIGINT>(count, input.size());

        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = input_col.get();
        raw_columns[1] = count_col.get();
        local_ctx->set_constant_columns({nullptr, count_col});

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), input.size(), raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    template<LogicalType LT>
    void RunNoMerge(const DatumArray& input, int count, const std::optional<Datum>& expected) {
        auto [local_ctx, state, func] = RunUpdate(LT, input, count);

        auto result = ColumnHelper::create_column(get_return_type(LT), true);
        func->finalize_to_column(local_ctx.get(), state->state(), result.get());

        Evaluate<LT>(result.get(), expected);
    }

    template<LogicalType LT>
    void RunMerge(const DatumArray& input1, const DatumArray& input2, int count, const std::optional<Datum>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(LT, input1, count);
        auto [local_ctx2, state2, func2] = RunUpdate(LT, input2, count);

        // Serialize state2
        ColumnPtr serde_col = BinaryColumn::create();
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge state2 into state1
        func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

        // Get the result
        auto result = ColumnHelper::create_column(get_return_type(LT), true);
        func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

        Evaluate<LT>(result.get(), expected);
    }

    template<LogicalType LT>
    void
    RunMergeToNew(const DatumArray& input1, const DatumArray& input2, int count, const std::optional<Datum>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(LT, input1, count);
        auto [local_ctx2, state2, func2] = RunUpdate(LT, input2, count);

        auto local_ctx3 = get_ctx(LT);
        // Serialize state1 and state2
        ColumnPtr serde_col = BinaryColumn::create();
        func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge to a new state
        auto state3 = ManagedAggrState::create(local_ctx3.get(), func);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);

        // Get the result
        auto result = local_ctx3->create_column(local_ctx3->get_return_type(), false);
        func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());

        Evaluate<LT>(result.get(), expected);
    }

    template<LogicalType LT>
    void Run(const DatumArray& input1, const DatumArray& input2, int count, const std::optional<Datum>& expected) {
        RunMerge<LT>(input1, input2, count, expected);
        RunMergeToNew<LT>(input1, input2, count, expected);
    }

    std::vector<std::unique_ptr<MemPool>> mem_pools_;
};

TEST_F(CelonisCalcBucketCountBoundariesTest, bigint_merge) {
    auto input1 = DatumArray{1L, 2L, 3L, 4L, 5L};
    auto input2 = DatumArray{6L, 7L, 8L, 9L, 10L};
    int count = 10;
    auto expected = DatumArray{1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L, 11L};

    Run<TYPE_BIGINT>(input1, input2, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, bigint_merge_outlier) {
    auto input1 = DatumArray{1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L,
                             1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L,
                             1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L,
                             1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L,
                             1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L,
                             1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L,
                             1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L,
                             1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L,
                             1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L,
                             1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L};
    auto input2 = DatumArray{100L};
    int count = 10;
    auto expected = DatumArray{1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L, 101L};

    Run<TYPE_BIGINT>(input1, input2, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, bigint_one_row) {
    auto input = DatumArray{10L};
    int count = 10;
    auto expected = DatumArray{10L, 11L};

    RunNoMerge<TYPE_BIGINT>(input, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, bigint_null) {
    auto input = DatumArray{kNullDatum};
    int count = 10;
    auto expected = kNullDatum;

    RunNoMerge<TYPE_BIGINT>(input, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, bigint_merge_one_row_and_null) {
    auto input1 = DatumArray{10L};
    auto input2 = DatumArray{kNullDatum};
    int count = 10;
    auto expected = DatumArray{10L, 11L};

    Run<TYPE_BIGINT>(input1, input2, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, too_many_buckets) {
    // 2 * length > MAX_NUM_BUCKETS
    const int64_t length = 550000;
    auto input1 = DatumArray{};
    input1.reserve(length);
    for (int64_t i = 0; i < length; ++i) {
        input1.emplace_back(i);
    }
    auto input2 = DatumArray{};
    input2.reserve(length);
    for (int64_t i = 0; i < length; ++i) {
        input2.emplace_back(i + length);
    }
    int count = 1050000;
    auto expected = std::nullopt;
    Run<TYPE_BIGINT>(input1, input2, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, double_input) {
    auto input1 = DatumArray{1.0, 2.0, 3.0, 4.0, 5.0};
    auto input2 = DatumArray{6.0, 7.0, 8.0, 9.0, 10.0};
    int count = 10;
    auto expected = DatumArray{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0};
    Run<TYPE_DOUBLE>(input1, input2, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, double_merge_null_and_one_row) {
    auto input1 = DatumArray{kNullDatum};
    auto input2 = DatumArray{123.0};
    int count = 10;
    auto expected = DatumArray{123.0, 124.0};

    Run<TYPE_DOUBLE>(input1, input2, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, datetime_input) {
    auto input1 = DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 10),
                             TimestampValue::create(1970, 1, 1, 0, 0, 5)};
    auto input2 = DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 20)};
    int count = 4;
    auto expected = DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 5),
                               TimestampValue::create(1970, 1, 1, 0, 0, 10),
                               TimestampValue::create(1970, 1, 1, 0, 0, 15),
                               TimestampValue::create(1970, 1, 1, 0, 0, 20, 1000)};

    Run<TYPE_DATETIME>(input1, input2, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, datetime_input_with_nulls) {
    auto input1 = DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 10), kNullDatum,
                             TimestampValue::create(1970, 1, 1, 0, 0, 5)};
    auto input2 = DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 20),
                             kNullDatum};
    int count = 4;
    auto expected = DatumArray{TimestampValue::create(1970, 1, 1, 0, 0, 0), TimestampValue::create(1970, 1, 1, 0, 0, 5),
                               TimestampValue::create(1970, 1, 1, 0, 0, 10),
                               TimestampValue::create(1970, 1, 1, 0, 0, 15),
                               TimestampValue::create(1970, 1, 1, 0, 0, 20, 1000)};

    Run<TYPE_DATETIME>(input1, input2, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, datetime_merge_null_and_one_row) {
    auto input1 = DatumArray{kNullDatum};
    auto input2 = DatumArray{TimestampValue::create(2024, 1, 2, 3, 4, 5)};
    int count = 10;
    auto expected = DatumArray{TimestampValue::create(2024, 1, 2, 3, 3, 9, 568000),
                               TimestampValue::create(2024, 1, 2, 3, 4, 5, 1000)};

    Run<TYPE_DATETIME>(input1, input2, count, expected);
}

} // namespace starrocks
