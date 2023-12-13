#include <gtest/gtest.h>

#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/sorted_first_last.h"
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
    void RunFunction(const std::string& func_name, const std::vector<SortColumnType>& sort_column_types,
                     const std::vector<DatumStruct>& input, const Datum& expected) {
        auto num_columns = sort_column_types.size() + 1;

        Columns columns;
        std::vector<const Column*> raw_columns;
        std::vector<FunctionContext::TypeDesc> arg_types;
        std::vector<bool> is_asc_order;
        std::vector<bool> nulls_first;

        auto add_column = [&](LogicalType type) {
            arg_types.push_back(AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(type)));
            columns.push_back(ColumnHelper::create_column(TypeDescriptor::from_logical_type(type), true));
            raw_columns.push_back(columns.back().get());
        };
        add_column(LT);
        for (auto sort_column_type : sort_column_types) {
            add_column(sort_column_type.type);
            is_asc_order.push_back(sort_column_type.is_asc_order);
            nulls_first.push_back(sort_column_type.nulls_first);
        }
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(LT));

        std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
        local_ctx->set_is_asc_order(is_asc_order);
        local_ctx->set_nulls_first(nulls_first);

        const AggregateFunction* func = get_aggregate_function(func_name, LT, LT, false);

        auto state = ManagedAggrState::create(local_ctx.get(), func);

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
        func->update_batch_single_state(local_ctx.get(), input.size(), raw_columns.data(), state->state());

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

    Run<TYPE_INT>(sort_column_types, input, 2, 4);
}

TEST_F(CelonisSortedFirstLastTest, nulls_last) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, true, false}};

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{1, 10});
    input.emplace_back(DatumStruct{2, kNullDatum});
    input.emplace_back(DatumStruct{3, 20});
    input.emplace_back(DatumStruct{4, 30});

    Run<TYPE_INT>(sort_column_types, input, 1, 2);
}

TEST_F(CelonisSortedFirstLastTest, desc_nulls_last) {
    std::vector<SortColumnType> sort_column_types = {{TYPE_INT, false, false}};

    std::vector<DatumStruct> input;
    input.emplace_back(DatumStruct{1, 10});
    input.emplace_back(DatumStruct{2, kNullDatum});
    input.emplace_back(DatumStruct{3, 20});
    input.emplace_back(DatumStruct{4, 30});

    Run<TYPE_INT>(sort_column_types, input, 4, 2);
}

TEST_F(CelonisSortedFirstLastTest, serialize_and_merge) {
    std::vector<FunctionContext::TypeDesc> arg_types1 = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto arg_types2 = arg_types1;
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::vector<bool> is_asc_order{true, true};
    std::vector<bool> nulls_first{true, true};

    std::unique_ptr<FunctionContext> local_ctx1(FunctionContext::create_test_context(std::move(arg_types1), return_type));
    local_ctx1->set_is_asc_order(is_asc_order);
    local_ctx1->set_nulls_first(nulls_first);
    std::unique_ptr<FunctionContext> local_ctx2(FunctionContext::create_test_context(std::move(arg_types2), return_type));
    local_ctx2->set_is_asc_order(is_asc_order);
    local_ctx2->set_nulls_first(nulls_first);

    const AggregateFunction* func = get_aggregate_function("celonis_sorted_first", TYPE_VARCHAR, TYPE_VARCHAR, false);

    auto col1 = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    col1->append_datum("A");
    col1->append_datum("B");

    auto sort1_col0 = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_INT), true);
    sort1_col0->append_datum(2);
    sort1_col0->append_datum(1);

    auto sort1_col1 = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    sort1_col1->append_datum("2");
    sort1_col1->append_datum("4");

    std::vector<const Column*> raw_columns1;
    raw_columns1.resize(3);
    raw_columns1[0] = col1.get();
    raw_columns1[1] = sort1_col0.get();
    raw_columns1[2] = sort1_col1.get();

    auto col2 = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    col2->append_datum("C");
    col2->append_datum("D");

    auto sort2_col0 = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_INT), true);
    sort2_col0->append_datum(3);
    sort2_col0->append_datum(2);

    auto sort2_col1 = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    sort2_col1->append_datum("3");
    sort2_col1->append_datum("1");

    std::vector<const Column*> raw_columns2;
    raw_columns2.resize(3);
    raw_columns2[0] = col2.get();
    raw_columns2[1] = sort2_col0.get();
    raw_columns2[2] = sort2_col1.get();

    auto state1 = ManagedAggrState::create(local_ctx1.get(), func);
    func->update_batch_single_state(local_ctx1.get(), col1->size(), raw_columns1.data(), state1->state());

    auto state2 = ManagedAggrState::create(local_ctx2.get(), func);
    func->update_batch_single_state(local_ctx2.get(), col2->size(), raw_columns2.data(), state2->state());

    // Serialize
    TypeDescriptor serde_type;
    serde_type.type = LogicalType::TYPE_STRUCT;
    serde_type.children.emplace_back(TypeDescriptor(LogicalType::TYPE_VARCHAR));
    serde_type.children.emplace_back(TypeDescriptor(LogicalType::TYPE_INT));
    serde_type.children.emplace_back(TypeDescriptor(LogicalType::TYPE_VARCHAR));
    serde_type.field_names.emplace_back("col0");
    serde_type.field_names.emplace_back("col1");
    serde_type.field_names.emplace_back("col2");
    auto serde_col = ColumnHelper::create_column(serde_type, true);
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    auto& serde_fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(serde_col.get()))->fields();
    ASSERT_EQ(serde_fields.size(), 3);
    EXPECT_EQ(serde_fields[0]->get(0).get_slice(), "D");
    EXPECT_EQ(serde_fields[1]->get(0).get_int32(), 2);
    EXPECT_EQ(serde_fields[2]->get(0).get_slice(), "1");

    // Merge
    func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    auto& state = *reinterpret_cast<const CelonisSortedFirstLastAggregateState<true>*>(state1->state());
    EXPECT_EQ(state.data_columns->size(), 3);

    // Get the result
    auto result = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    ASSERT_EQ(result->size(), 1);
    ASSERT_FALSE(result->is_null(0));
    EXPECT_EQ(result->get(0).get_slice(), "B");
}

TEST_F(CelonisSortedFirstLastTest, serialize_and_merge_null) {
    std::vector<FunctionContext::TypeDesc> arg_types1 = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT))};
    auto arg_types2 = arg_types1;
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT));
    std::vector<bool> is_asc_order{};
    std::vector<bool> nulls_first{};

    std::unique_ptr<FunctionContext> local_ctx1(FunctionContext::create_test_context(std::move(arg_types1), return_type));
    local_ctx1->set_is_asc_order(is_asc_order);
    local_ctx1->set_nulls_first(nulls_first);
    std::unique_ptr<FunctionContext> local_ctx2(FunctionContext::create_test_context(std::move(arg_types2), return_type));
    local_ctx2->set_is_asc_order(is_asc_order);
    local_ctx2->set_nulls_first(nulls_first);

    const AggregateFunction* func = get_aggregate_function("celonis_sorted_first", TYPE_INT, TYPE_INT, false);

    auto col1 = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_INT), true);
    col1->append_datum(kNullDatum);

    std::vector<const Column*> raw_columns1;
    raw_columns1.push_back(col1.get());

    auto col2 = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_INT), true);
    col2->append_datum(kNullDatum);

    std::vector<const Column*> raw_columns2;
    raw_columns2.push_back(col2.get());

    auto state1 = ManagedAggrState::create(local_ctx1.get(), func);
    func->update_batch_single_state(local_ctx1.get(), col1->size(), raw_columns1.data(), state1->state());

    auto state2 = ManagedAggrState::create(local_ctx2.get(), func);
    func->update_batch_single_state(local_ctx2.get(), col2->size(), raw_columns2.data(), state2->state());

    // Serialize
    TypeDescriptor serde_type;
    serde_type.type = LogicalType::TYPE_STRUCT;
    serde_type.children.emplace_back(TypeDescriptor(LogicalType::TYPE_INT));
    serde_type.field_names.emplace_back("col1");
    auto serde_col = ColumnHelper::create_column(serde_type, true);
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    EXPECT_TRUE(serde_col->is_null(0));
    auto& serde_fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(serde_col.get()))->fields();
    EXPECT_EQ(serde_fields.size(), 1);

    // Merge
    func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    auto& state = *reinterpret_cast<const CelonisSortedFirstLastAggregateState<true>*>(state1->state());
    EXPECT_EQ(state.data_columns->size(), 1);

    // Get the result
    auto result = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_INT), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    ASSERT_EQ(result->size(), 1);
    EXPECT_TRUE(result->is_null(0));
}

} // namespace starrocks
