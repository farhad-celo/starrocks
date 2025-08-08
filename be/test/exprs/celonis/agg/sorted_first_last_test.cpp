#include <gtest/gtest.h>

#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/agg/sorted_first_last.h"
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

class CelonisSortedFirstLastTest : public testing::Test {
protected:
    CelonisSortedFirstLastTest() = default;

    struct SortColumnType {
        LogicalType type;
        bool is_asc_order;
        bool nulls_first;
    };

    void SetUp() override {}

    void TearDown() override {}

    template <LogicalType LT>
    std::tuple<std::unique_ptr<FunctionContext>, Columns, const AggregateFunction*>
    Prepare(const std::string& func_name, const std::vector<SortColumnType>& sort_column_types,
            const std::vector<DatumStruct>& input) {
        auto num_columns = sort_column_types.size() + 1;

        Columns columns;
        std::vector<FunctionContext::TypeDesc> arg_types;
        std::vector<bool> is_asc_order;
        std::vector<bool> nulls_first;

        auto add_column = [&](LogicalType type) {
            arg_types.push_back(TypeDescriptor::from_logical_type(type));
            columns.push_back(ColumnHelper::create_column(TypeDescriptor::from_logical_type(type), true));
        };
        add_column(LT);
        for (auto sort_column_type : sort_column_types) {
            add_column(sort_column_type.type);
            is_asc_order.push_back(sort_column_type.is_asc_order);
            nulls_first.push_back(sort_column_type.nulls_first);
        }
        auto return_type = TypeDescriptor::from_logical_type(LT);

        std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
        local_ctx->set_is_asc_order(is_asc_order);
        local_ctx->set_nulls_first(nulls_first);

        const AggregateFunction* func = get_aggregate_function(func_name, LT, LT, false);

        for (const auto& datum_struct : input) {
            DCHECK_EQ(datum_struct.size(), num_columns);
            for (int i = 0; i < num_columns; ++i) {
                if (datum_struct[i].is_null()) {
                    columns[i]->append_nulls(1);
                } else {
                    columns[i]->append_datum(datum_struct[i]);
                }
            }
        }

        return {std::move(local_ctx), std::move(columns), func};
    }

    template <LogicalType LT>
    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*>
    RunUpdate(const std::string& func_name, const std::vector<SortColumnType>& sort_column_types,
              const std::vector<DatumStruct>& input) {
        auto [local_ctx, columns, func] = Prepare<LT>(func_name, sort_column_types, input);
        auto state = ManagedAggrState::create(local_ctx.get(), func);

        std::vector<const Column*> raw_columns;
        for (const auto& column : columns) {
            raw_columns.push_back(column.get());
        }
        func->update_batch_single_state(local_ctx.get(), input.size(), raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    template <LogicalType LT>
    void RunFunction(const std::string& func_name, const std::vector<SortColumnType>& sort_column_types,
                     const std::vector<DatumStruct>& input, const Datum& expected) {
        auto [local_ctx, state, func] = RunUpdate<LT>(func_name, sort_column_types, input);

        // Get the result
        auto result = ColumnHelper::create_column(TypeDescriptor::from_logical_type(LT), true);
        func->finalize_to_column(local_ctx.get(), state->state(), result.get());

        ASSERT_EQ(result->size(), 1);
        if (expected.is_null()) {
            EXPECT_TRUE(result->is_null(0));
        } else {
            ASSERT_FALSE(result->is_null(0));
            EXPECT_EQ(result->get(0).get<RunTimeCppType<LT>>(), expected.get<RunTimeCppType<LT>>());
        }
    }

    template <LogicalType LT>
    void Run(const std::vector<SortColumnType>& sort_column_types, const std::vector<DatumStruct>& input,
             const Datum& first_expected, const Datum& last_expected) {
        RunFunction<LT>("celonis_sorted_first", sort_column_types, input, first_expected);
        RunFunction<LT>("celonis_sorted_last", sort_column_types, input, last_expected);
    }
};

TEST_F(CelonisSortedFirstLastTest, null_value) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, true}};

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{"A", 4});
    input.emplace_back(DatumStruct{"B", 3});
    input.emplace_back(DatumStruct{"C", 2});
    input.emplace_back(DatumStruct{kNullDatum, 1});

    Run<TYPE_VARCHAR>(sort_column_types, input, "C", "A");
}

TEST_F(CelonisSortedFirstLastTest, null_value_only) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, true}};

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{kNullDatum, 1});
    input.emplace_back(DatumStruct{kNullDatum, 2});

    Run<TYPE_VARCHAR>(sort_column_types, input, kNullDatum, kNullDatum);
}

TEST_F(CelonisSortedFirstLastTest, no_sort) {
    std::vector<SortColumnType> sort_column_types;

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{"A"});
    input.emplace_back(DatumStruct{"B"});
    input.emplace_back(DatumStruct{"C"});

    Run<TYPE_VARCHAR>(sort_column_types, input, "A", "A");
}

TEST_F(CelonisSortedFirstLastTest, multiple_sort) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, true},
                                                     {TYPE_INT, true, true}};

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{"A", 2, 2});
    input.emplace_back(DatumStruct{"B", 2, 1});
    input.emplace_back(DatumStruct{"C", 1, 1});
    input.emplace_back(DatumStruct{"D", 1, 2});

    Run<TYPE_VARCHAR>(sort_column_types, input, "C", "A");
}

TEST_F(CelonisSortedFirstLastTest, varchar_int_desc_sort) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_VARCHAR, true, true},
                                                     {TYPE_INT, false, true}};

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{1, "A", 1});
    input.emplace_back(DatumStruct{2, "B", 2});
    input.emplace_back(DatumStruct{3, "A", 2});
    input.emplace_back(DatumStruct{4, "A", 0});

    Run<TYPE_INT>(sort_column_types, input, 3, 2);
}

TEST_F(CelonisSortedFirstLastTest, nulls_first) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_DOUBLE, true, true}};

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{1, 10.0});
    input.emplace_back(DatumStruct{2, kNullDatum});
    input.emplace_back(DatumStruct{3, 20.0});
    input.emplace_back(DatumStruct{4, 30.0});

    Run<TYPE_INT>(sort_column_types, input, 1, 4);
}

TEST_F(CelonisSortedFirstLastTest, null_order_only) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, false}};

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{1, kNullDatum});
    Run<TYPE_INT>(sort_column_types, input, kNullDatum, kNullDatum);

    input.emplace_back(DatumStruct{2, kNullDatum});
    input.emplace_back(DatumStruct{3, kNullDatum});
    input.emplace_back(DatumStruct{4, kNullDatum});
    Run<TYPE_INT>(sort_column_types, input, kNullDatum, kNullDatum);
}

TEST_F(CelonisSortedFirstLastTest, desc_nulls_last) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, false, false}};

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{1, 10});
    input.emplace_back(DatumStruct{2, kNullDatum});
    input.emplace_back(DatumStruct{3, 20});
    input.emplace_back(DatumStruct{4, 30});

    Run<TYPE_INT>(sort_column_types, input, 4, 1);
}

TEST_F(CelonisSortedFirstLastTest, int128_t) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_LARGEINT, true, true}};

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{kNullDatum, Datum(int128_t(1234567890123456781))});
    input.emplace_back(DatumStruct{Datum(int128_t(0)), Datum(int128_t(1234567890123456784))});
    input.emplace_back(DatumStruct{Datum(int128_t(1)), Datum(int128_t(1234567890123456783))});
    input.emplace_back(DatumStruct{Datum(int128_t(2)), Datum(int128_t(1234567890123456782))});

    Run<TYPE_LARGEINT>(sort_column_types, input, int128_t(2), int128_t(0));
}

TEST_F(CelonisSortedFirstLastTest, serialize_and_merge) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, true},
                                                     {TYPE_VARCHAR, true, true}};

    std::vector<DatumStruct> input1;
    input1.emplace_back(DatumStruct{"A", 2, "2"});
    input1.emplace_back(DatumStruct{"B", 1, "4"});

    std::vector<DatumStruct> input2;
    input2.emplace_back(DatumStruct{"C", 3, "3"});
    input2.emplace_back(DatumStruct{"D", 2, "1"});

    auto [local_ctx1, state1, func1] = RunUpdate<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input1);
    auto [local_ctx2, state2, func2] = RunUpdate<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input2);

    // Serialize
    auto serde_col = BinaryColumn::create();
    func2->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());
    ASSERT_EQ(serde_col->size(), 1);
    EXPECT_GT(serde_col->get_slice(0).size, 0);

    // Merge
    func1->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    func1->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    ASSERT_EQ(result->size(), 1);
    ASSERT_FALSE(result->is_null(0));
    EXPECT_EQ(result->get(0).get_slice(), "B");
}

TEST_F(CelonisSortedFirstLastTest, serialize_and_merge_no_row) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, true}};

    std::vector<DatumStruct> input1;
    input1.emplace_back(DatumStruct{kNullDatum, 2});

    std::vector<DatumStruct> input2;
    input2.emplace_back(DatumStruct{kNullDatum, 3});

    auto [local_ctx1, state1, func1] = RunUpdate<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input1);
    auto [local_ctx2, state2, func2] = RunUpdate<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input2);

    // Serialize
    auto serde_col = BinaryColumn::create();
    func2->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());
    ASSERT_EQ(serde_col->size(), 1);
    EXPECT_EQ(serde_col->get_slice(0).size, 0);

    // Merge
    func1->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    auto& state = *reinterpret_cast<const CelonisSortedFirstLastAggregateState<true>*>(state1->state());
    EXPECT_TRUE(state.buffer.empty());

    // Get the result
    auto result = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_INT), true);
    func1->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    ASSERT_EQ(result->size(), 1);
    EXPECT_TRUE(result->is_null(0));
}

TEST_F(CelonisSortedFirstLastTest, serialize_and_merge_no_row_nullable) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, true}};

    std::vector<DatumStruct> input1;
    input1.emplace_back(DatumStruct{kNullDatum, 2});

    std::vector<DatumStruct> input2;
    input2.emplace_back(DatumStruct{kNullDatum, 3});

    auto [local_ctx1, state1, func1] = RunUpdate<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input1);
    auto [local_ctx2, state2, func2] = RunUpdate<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input2);

    // Serialize
    ColumnPtr serde_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARBINARY), true);
    func2->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());
    ASSERT_EQ(serde_col->size(), 1);
    EXPECT_TRUE(down_cast<NullableColumn*>(serde_col.get())->is_null(0));

    // Merge
    func1->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    auto& state = *reinterpret_cast<const CelonisSortedFirstLastAggregateState<true>*>(state1->state());
    EXPECT_TRUE(state.buffer.empty());

    // Get the result
    auto result = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_INT), true);
    func1->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    ASSERT_EQ(result->size(), 1);
    EXPECT_TRUE(result->is_null(0));
}

TEST_F(CelonisSortedFirstLastTest, serialize_and_merge_null_sort) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, true},
                                                     {TYPE_VARCHAR, true, true}};

    std::vector<DatumStruct> input1;
    input1.emplace_back(DatumStruct{"A", 2, "2"});
    input1.emplace_back(DatumStruct{"B", 1, "4"});

    std::vector<DatumStruct> input2;
    input2.emplace_back(DatumStruct{"C", 3, "3"});
    input2.emplace_back(DatumStruct{"D", 2, kNullDatum});

    auto [local_ctx1, state1, func1] = RunUpdate<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input1);
    auto [local_ctx2, state2, func2] = RunUpdate<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input2);

    // Serialize
    auto serde_col = BinaryColumn::create();
    func2->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());
    ASSERT_EQ(serde_col->size(), 1);
    EXPECT_GT(serde_col->get_slice(0).size, 0);

    // Merge
    func1->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    func1->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    ASSERT_EQ(result->size(), 1);
    ASSERT_FALSE(result->is_null(0));
    EXPECT_EQ(result->get(0).get_slice(), "B");
}

TEST_F(CelonisSortedFirstLastTest, serialize_and_merge_to_new_state) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, true},
                                                     {TYPE_VARCHAR, true, true}};

    std::vector<DatumStruct> input1;

    std::vector<DatumStruct> input2;
    input2.emplace_back(DatumStruct{"A", 2, "2"});
    input2.emplace_back(DatumStruct{"B", 1, "4"});
    input2.emplace_back(DatumStruct{"C", 3, "3"});
    input2.emplace_back(DatumStruct{"D", 2, "1"});

    auto [local_ctx1, state1, func1] = RunUpdate<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input1);
    auto [local_ctx2, state2, func2] = RunUpdate<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input2);

    // Serialize
    auto serde_col = BinaryColumn::create();
    func2->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());
    ASSERT_EQ(serde_col->size(), 1);
    EXPECT_GT(serde_col->get_slice(0).size, 0);

    // Merge
    func1->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    func1->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    ASSERT_EQ(result->size(), 1);
    ASSERT_FALSE(result->is_null(0));
    EXPECT_EQ(result->get(0).get_slice(), "B");
}

TEST_F(CelonisSortedFirstLastTest, convert_to_serialize_format) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, true},
                                                     {TYPE_VARCHAR, true, true}};

    std::vector<DatumStruct> input1;
    input1.emplace_back(DatumStruct{"A", 2, "2"});
    input1.emplace_back(DatumStruct{"B", 1, "1"});
    input1.emplace_back(DatumStruct{kNullDatum, 0, "0"});
    input1.emplace_back(DatumStruct{"D", 1, "3"});

    auto [local_ctx1, columns1, func1] = Prepare<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input1);

    // Convert to serialize format
    ColumnPtr serde_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARBINARY), false);
    func1->convert_to_serialize_format(local_ctx1.get(), columns1, 4, &serde_col);
    ASSERT_EQ(serde_col->size(), 4);
    auto* serde_binary_column = down_cast<BinaryColumn*>(serde_col.get());
    EXPECT_GT(serde_binary_column->get_slice(0).size, 0);
    EXPECT_GT(serde_binary_column->get_slice(1).size, 0);
    EXPECT_EQ(serde_binary_column->get_slice(2).size, 0);
    EXPECT_GT(serde_binary_column->get_slice(3).size, 0);

    // Merge
    std::vector<DatumStruct> input2;
    auto [local_ctx2, columns2, func2] = Prepare<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input2);
    auto state2 = ManagedAggrState::create(local_ctx2.get(), func2);
    func2->merge(local_ctx2.get(), serde_col.get(), state2->state(), 0);
    func2->merge(local_ctx2.get(), serde_col.get(), state2->state(), 1);
    func2->merge(local_ctx2.get(), serde_col.get(), state2->state(), 2);
    func2->merge(local_ctx2.get(), serde_col.get(), state2->state(), 3);

    // Get the result
    auto result = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    func2->finalize_to_column(local_ctx2.get(), state2->state(), result.get());

    ASSERT_EQ(result->size(), 1);
    ASSERT_FALSE(result->is_null(0));
    EXPECT_EQ(result->get(0).get_slice(), "B");
}

TEST_F(CelonisSortedFirstLastTest, convert_to_serialize_format_nullable) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, true},
                                                     {TYPE_VARCHAR, true, true}};

    std::vector<DatumStruct> input1;
    input1.emplace_back(DatumStruct{"A", 1, "2"});
    input1.emplace_back(DatumStruct{kNullDatum, 0, "0"});
    input1.emplace_back(DatumStruct{"C", 1, "1"});
    input1.emplace_back(DatumStruct{"D", 2, "3"});

    auto [local_ctx1, columns1, func1] = Prepare<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input1);

    // Convert to serialize format
    ColumnPtr serde_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARBINARY), true);
    func1->convert_to_serialize_format(local_ctx1.get(), columns1, 4, &serde_col);
    ASSERT_EQ(serde_col->size(), 4);
    auto* serde_nullable_column = down_cast<NullableColumn*>(serde_col.get());
    EXPECT_FALSE(serde_nullable_column->is_null(0));
    EXPECT_TRUE(serde_nullable_column->is_null(1));
    EXPECT_FALSE(serde_nullable_column->is_null(2));
    EXPECT_FALSE(serde_nullable_column->is_null(3));

    // Merge
    std::vector<DatumStruct> input2;
    auto [local_ctx2, columns2, func2] = Prepare<TYPE_VARCHAR>("celonis_sorted_first", sort_column_types, input2);
    auto state2 = ManagedAggrState::create(local_ctx2.get(), func2);
    func2->merge(local_ctx2.get(), serde_col.get(), state2->state(), 0);
    func2->merge(local_ctx2.get(), serde_col.get(), state2->state(), 1);
    func2->merge(local_ctx2.get(), serde_col.get(), state2->state(), 2);
    func2->merge(local_ctx2.get(), serde_col.get(), state2->state(), 3);

    // Get the result
    auto result = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    func2->finalize_to_column(local_ctx2.get(), state2->state(), result.get());

    ASSERT_EQ(result->size(), 1);
    ASSERT_FALSE(result->is_null(0));
    EXPECT_EQ(result->get(0).get_slice(), "C");
}

} // namespace starrocks

