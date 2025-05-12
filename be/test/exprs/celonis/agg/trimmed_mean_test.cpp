#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "testutil/function_utils.h"

namespace starrocks {

class CelonisTrimmedMeanTest : public testing::Test {
public:
    CelonisTrimmedMeanTest() = default;

    void SetUp() override {
        utils = new FunctionUtils();
        ctx = utils->get_fn_ctx();
    }
    void TearDown() override { delete utils; }

private:
    FunctionUtils* utils{};
    FunctionContext* ctx{};
};

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

TEST_F(CelonisTrimmedMeanTest, pql_example_1_bigint) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    auto const_column_lower = ColumnHelper::create_const_column<TYPE_INT>(30, 1);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_INT>(30, 1);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});

    const AggregateFunction* func = get_aggregate_function("celonis_trimmed_mean", TYPE_BIGINT, TYPE_DOUBLE, false);

    // update input column 1
    auto state1 = ManagedAggrState::create(ctx, func);

    auto data_column1 = RunTimeColumnType<TYPE_BIGINT>::create();
    auto const_column_lower1 = const_column_lower->clone();
    auto const_column_upper1 = const_column_upper->clone();
    data_column1->append(102);
    data_column1->append(101);
    data_column1->append(100);
    data_column1->append(4);
    data_column1->append(3);

    std::vector<const Column*> raw_columns1;
    raw_columns1.resize(3);
    raw_columns1[0] = data_column1.get();
    raw_columns1[1] = const_column_lower1.get();
    raw_columns1[2] = const_column_upper1.get();

    func->update_batch_single_state(local_ctx.get(), data_column1->size(), raw_columns1.data(), state1->state());

    // update input column 2
    auto state2 = ManagedAggrState::create(ctx, func);

    auto data_column2 = RunTimeColumnType<TYPE_BIGINT>::create();
    auto const_column_lower2 = const_column_lower->clone();
    auto const_column_upper2 = const_column_upper->clone();
    data_column2->append(2);
    data_column2->append(1);
    data_column2->append(-100);
    data_column2->append(-101);
    data_column2->append(-102);

    std::vector<const Column*> raw_columns2;
    raw_columns2.resize(3);
    raw_columns2[0] = data_column2.get();
    raw_columns2[1] = const_column_lower2.get();
    raw_columns2[2] = const_column_upper2.get();

    func->update_batch_single_state(local_ctx.get(), data_column2->size(), raw_columns2.data(), state2->state());

    // merge column 1 and column 2
    ColumnPtr serde_column = BinaryColumn::create();
    auto result_column = RunTimeColumnType<TYPE_DOUBLE>::create();
    func->serialize_to_column(local_ctx.get(), state1->state(), serde_column.get());
    func->merge(local_ctx.get(), serde_column.get(), state2->state(), 0);
    func->finalize_to_column(local_ctx.get(), state2->state(), result_column.get());
    ASSERT_FALSE(local_ctx->has_error());

    ASSERT_EQ(2.5, result_column->get_data()[0]);  // (-102, -101, -100), 1, 2, 3, 4, (100, 101, 102)
}

TEST_F(CelonisTrimmedMeanTest, pql_example_2_bigint) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    auto const_column_lower = ColumnHelper::create_const_column<TYPE_INT>(50, 1);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_INT>(50, 1);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});

    const AggregateFunction* func = get_aggregate_function("celonis_trimmed_mean", TYPE_BIGINT, TYPE_DOUBLE, false);

    // update input column 1
    auto state1 = ManagedAggrState::create(ctx, func);

    auto data_column1 = RunTimeColumnType<TYPE_BIGINT>::create();
    auto const_column_lower1 = const_column_lower->clone();
    auto const_column_upper1 = const_column_upper->clone();
    data_column1->append(10);
    data_column1->append(3);
    data_column1->append(4);

    std::vector<const Column*> raw_columns1;
    raw_columns1.resize(3);
    raw_columns1[0] = data_column1.get();
    raw_columns1[1] = const_column_lower1.get();
    raw_columns1[2] = const_column_upper1.get();

    func->update_batch_single_state(local_ctx.get(), data_column1->size(), raw_columns1.data(), state1->state());

    // update input column 2
    auto state2 = ManagedAggrState::create(ctx, func);

    auto data_column2 = RunTimeColumnType<TYPE_BIGINT>::create();
    auto const_column_lower2 = const_column_lower->clone();
    auto const_column_upper2 = const_column_upper->clone();
    data_column2->append(22);
    data_column2->append(5);

    std::vector<const Column*> raw_columns2;
    raw_columns2.resize(3);
    raw_columns2[0] = data_column2.get();
    raw_columns2[1] = const_column_lower2.get();
    raw_columns2[2] = const_column_upper2.get();

    func->update_batch_single_state(local_ctx.get(), data_column2->size(), raw_columns2.data(), state2->state());

    // merge column 1 and column 2
    ColumnPtr serde_column = BinaryColumn::create();
    auto result_column = RunTimeColumnType<TYPE_DOUBLE>::create();
    func->serialize_to_column(local_ctx.get(), state1->state(), serde_column.get());
    func->merge(local_ctx.get(), serde_column.get(), state2->state(), 0);
    func->finalize_to_column(local_ctx.get(), state2->state(), result_column.get());
    ASSERT_FALSE(local_ctx->has_error());

    ASSERT_EQ(5.0, result_column->get_data()[0]);  // (3, 4), 5, (10, 22)
}

TEST_F(CelonisTrimmedMeanTest, type_double) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    auto const_column_lower = ColumnHelper::create_const_column<TYPE_INT>(20, 1);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_INT>(30, 1);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});

    const AggregateFunction* func = get_aggregate_function("celonis_trimmed_mean", TYPE_DOUBLE, TYPE_DOUBLE, false);

    // update input column 1
    auto state1 = ManagedAggrState::create(ctx, func);

    auto data_column1 = RunTimeColumnType<TYPE_DOUBLE>::create();
    auto const_column_lower1 = const_column_lower->clone();
    auto const_column_upper1 = const_column_upper->clone();
    data_column1->append(10.0);
    data_column1->append(8.0);
    data_column1->append(6.0);
    data_column1->append(4.0);
    data_column1->append(2.0);

    std::vector<const Column*> raw_columns1;
    raw_columns1.resize(3);
    raw_columns1[0] = data_column1.get();
    raw_columns1[1] = const_column_lower1.get();
    raw_columns1[2] = const_column_upper1.get();

    func->update_batch_single_state(local_ctx.get(), data_column1->size(), raw_columns1.data(), state1->state());

    // update input column 2
    auto state2 = ManagedAggrState::create(ctx, func);

    auto data_column2 = RunTimeColumnType<TYPE_DOUBLE>::create();
    auto const_column_lower2 = const_column_lower->clone();
    auto const_column_upper2 = const_column_upper->clone();
    data_column2->append(11.0);
    data_column2->append(9.0);
    data_column2->append(7.0);
    data_column2->append(5.0);
    data_column2->append(3.0);
    data_column2->append(1.0);

    std::vector<const Column*> raw_columns2;
    raw_columns2.resize(3);
    raw_columns2[0] = data_column2.get();
    raw_columns2[1] = const_column_lower2.get();
    raw_columns2[2] = const_column_upper2.get();

    func->update_batch_single_state(local_ctx.get(), data_column2->size(), raw_columns2.data(), state2->state());

    // merge column 1 and column 2
    ColumnPtr serde_column = BinaryColumn::create();
    auto result_column = RunTimeColumnType<TYPE_DOUBLE>::create();
    func->serialize_to_column(local_ctx.get(), state1->state(), serde_column.get());
    func->merge(local_ctx.get(), serde_column.get(), state2->state(), 0);
    func->finalize_to_column(local_ctx.get(), state2->state(), result_column.get());
    ASSERT_FALSE(local_ctx->has_error());

    ASSERT_EQ(5.5, result_column->get_data()[0]);  // (1, 2), 3, 4, 5, 6, 7, 8, (9, 10, 11)
}

TEST_F(CelonisTrimmedMeanTest, null_handling) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    auto const_column_lower = ColumnHelper::create_const_column<TYPE_INT>(0, 1);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_INT>(0, 1);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});

    const AggregateFunction* func = get_aggregate_function("celonis_trimmed_mean", TYPE_BIGINT, TYPE_DOUBLE, true);

    // update input column 1
    auto state1 = ManagedAggrState::create(ctx, func);

    auto data_column1 = NullableColumn::create(RunTimeColumnType<TYPE_BIGINT>::create(), NullColumn::create(0, 0));
    auto const_column_lower1 = const_column_lower->clone();
    auto const_column_upper1 = const_column_upper->clone();
    data_column1->append_datum(1L);
    data_column1->append_nulls(1);
    data_column1->append_datum(2L);
    data_column1->append_datum(3L);
    data_column1->append_datum(4L);

    std::vector<const Column*> raw_columns1;
    raw_columns1.resize(3);
    raw_columns1[0] = data_column1.get();
    raw_columns1[1] = const_column_lower1.get();
    raw_columns1[2] = const_column_upper1.get();

    func->update_batch_single_state(local_ctx.get(), data_column1->size(), raw_columns1.data(), state1->state());

    // update input column 2
    auto state2 = ManagedAggrState::create(ctx, func);

    auto data_column2 = RunTimeColumnType<TYPE_BIGINT>::create();
    auto const_column_lower2 = const_column_lower->clone();
    auto const_column_upper2 = const_column_upper->clone();
    [[maybe_unused]] bool ok = data_column2->append_nulls(10);

    std::vector<const Column*> raw_columns2;
    raw_columns2.resize(3);
    raw_columns2[0] = data_column2.get();
    raw_columns2[1] = const_column_lower2.get();
    raw_columns2[2] = const_column_upper2.get();

    func->update_batch_single_state(local_ctx.get(), data_column2->size(), raw_columns2.data(), state2->state());

    // merge column 1 and column 2
    ColumnPtr serde_column = NullableColumn::create(BinaryColumn::create(), NullColumn::create(0, 0));
    auto result_column = RunTimeColumnType<TYPE_DOUBLE>::create();
    func->serialize_to_column(local_ctx.get(), state1->state(), serde_column.get());
    func->merge(local_ctx.get(), serde_column.get(), state2->state(), 0);
    func->finalize_to_column(local_ctx.get(), state2->state(), result_column.get());

    ASSERT_EQ(2.5, result_column->get(0).get_double());  // 1, 2, 3, 4
}

TEST_F(CelonisTrimmedMeanTest, invalid_lower_and_upper) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE));
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    auto const_column_lower = ColumnHelper::create_const_column<TYPE_INT>(60, 1);
    auto const_column_upper = ColumnHelper::create_const_column<TYPE_INT>(50, 1);
    local_ctx->set_constant_columns({nullptr, const_column_lower, const_column_upper});

    const AggregateFunction* func = get_aggregate_function("celonis_trimmed_mean", TYPE_BIGINT, TYPE_DOUBLE, false);

    // update input column 1
    auto state1 = ManagedAggrState::create(ctx, func);

    auto data_column1 = RunTimeColumnType<TYPE_BIGINT>::create();
    auto const_column_lower1 = const_column_lower->clone();
    auto const_column_upper1 = const_column_upper->clone();
    data_column1->append(10);
    data_column1->append(3);
    data_column1->append(4);

    std::vector<const Column*> raw_columns1;
    raw_columns1.resize(3);
    raw_columns1[0] = data_column1.get();
    raw_columns1[1] = const_column_lower1.get();
    raw_columns1[2] = const_column_upper1.get();

    func->update_batch_single_state(local_ctx.get(), data_column1->size(), raw_columns1.data(), state1->state());

    // update input column 2
    auto state2 = ManagedAggrState::create(ctx, func);

    auto data_column2 = RunTimeColumnType<TYPE_BIGINT>::create();
    auto const_column_lower2 = const_column_lower->clone();
    auto const_column_upper2 = const_column_upper->clone();
    data_column2->append(22);
    data_column2->append(5);

    std::vector<const Column*> raw_columns2;
    raw_columns2.resize(3);
    raw_columns2[0] = data_column2.get();
    raw_columns2[1] = const_column_lower2.get();
    raw_columns2[2] = const_column_upper2.get();

    func->update_batch_single_state(local_ctx.get(), data_column2->size(), raw_columns2.data(), state2->state());

    // merge column 1 and column 2
    ColumnPtr serde_column = BinaryColumn::create();
    auto result_column = RunTimeColumnType<TYPE_DOUBLE>::create();
    func->serialize_to_column(local_ctx.get(), state1->state(), serde_column.get());
    func->merge(local_ctx.get(), serde_column.get(), state2->state(), 0);
    func->finalize_to_column(local_ctx.get(), state2->state(), result_column.get());

    ASSERT_EQ(0.0, result_column->get_data()[0]);
}

} // namespace starrocks
