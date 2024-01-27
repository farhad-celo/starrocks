#include <algorithm>
#include <gtest/gtest.h>

#include "column/struct_column.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/calc_bucket_boundaries.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "util.h"

namespace starrocks {

namespace {

class ManagedAggrState {
public:
    ~ManagedAggrState() { _func->destroy(_ctx, _state); }

    static std::unique_ptr<ManagedAggrState> create(FunctionContext *ctx, const AggregateFunction *func) {
        return std::make_unique<ManagedAggrState>(ctx, func);
    }

    AggDataPtr state() { return _state; }

private:
    ManagedAggrState(FunctionContext *ctx, const AggregateFunction *func) : _ctx(ctx), _func(func) {
        _state = _mem_pool.allocate_aligned(func->size(), func->alignof_size());
        _func->create(_ctx, _state);
    }

    FunctionContext *_ctx;
    const AggregateFunction *_func;
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
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(logical_type)), // input
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)) // no_lower_bound
        };
        auto return_type = AnyValUtil::column_type_to_type_desc(get_return_type(logical_type));
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_test_context(std::move(arg_types), return_type));
    }

    template<LogicalType LT>
    void Evaluate(Column* result, const Datum& expected) {
        ASSERT_EQ(result->size(), 1);
        if (expected.is_null()) {
            EXPECT_TRUE(result->is_null(0));
            return;
        }
        ASSERT_FALSE(result->is_null(0));

        auto expected_array = expected.get_array();
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

        const AggregateFunction *func =
                get_aggregate_function("celonis_calc_bucket_count_boundaries", logical_type, TYPE_ARRAY, false);

        auto input_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(logical_type), true);
        for (const auto& datum : input) {
            input_col->append_datum(datum);
        }

        auto count_col = ColumnHelper::create_const_column<TYPE_BIGINT>(count, input.size());

        std::vector<const Column *> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = input_col.get();
        raw_columns[1] = count_col.get();
        local_ctx->set_constant_columns({nullptr, count_col});

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), input.size(), raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    template<LogicalType LT>
    void RunNoMerge(const DatumArray& input, int count, const Datum& expected) {
        auto [local_ctx, state, func] = RunUpdate(LT, input, count);

        auto result = ColumnHelper::create_column(get_return_type(LT), true);
        func->finalize_to_column(local_ctx.get(), state->state(), result.get());

        Evaluate<LT>(result.get(), expected);
    }

    template<LogicalType LT>
    void RunMerge(const DatumArray& input1, const DatumArray& input2, int count, const Datum& expected) {
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
    std::vector<std::unique_ptr<MemPool>> mem_pools_;
};

TEST_F(CelonisCalcBucketCountBoundariesTest, bigint_merge) {
    auto input1 = DatumArray{1L, 2L, 3L, 4L, 5L};
    auto input2 = DatumArray{6L, 7L, 8L, 9L, 10L};
    int count = 10;
    auto expected = DatumArray{1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L, 11L};

    RunMerge<TYPE_BIGINT>(input1, input2, count, expected);
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

    RunMerge<TYPE_BIGINT>(input1, input2, count, expected);
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

    RunMerge<TYPE_BIGINT>(input1, input2, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, double) {
    const LogicalType LT = TYPE_DOUBLE;
    auto input1 = DatumArray{1.0, 2.0, 3.0, 4.0, 5.0};
    auto input2 = DatumArray{6.0, 7.0, 8.0, 9.0, 10.0};
    int count = 10;
    auto expected = DatumArray{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0};

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

TEST_F(CelonisCalcBucketCountBoundariesTest, double_merge_null_and_one_row) {
    auto input1 = DatumArray{kNullDatum};
    auto input2 = DatumArray{123.};
    int count = 10;
    auto expected = DatumArray{123., 124.};

    RunMerge<TYPE_DOUBLE>(input1, input2, count, expected);
}

TEST_F(CelonisCalcBucketCountBoundariesTest, datetime_merge_null_and_one_row) {
    auto input1 = DatumArray{kNullDatum};
    auto input2 = DatumArray{TimestampValue::create(2024, 1, 2, 3, 4, 5)};
    int count = 10;
    auto expected = DatumArray{TimestampValue::create(2024, 1, 2, 3, 3, 28),
                               TimestampValue::create(2024, 1, 2, 3, 4, 6)};

    RunMerge<TYPE_DATETIME>(input1, input2, count, expected);
}

// TODO(j.kim): Add more DATETIME tests

} // namespace starrocks