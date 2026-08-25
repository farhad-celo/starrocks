// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "exprs/celonis/agg/multi_array_agg_v3.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/aggregate_state_allocator.h"
#include "exprs/celonis/anyval_util.h"
#include "exprs/function_context.h"
#include "gutil/strings/strcat.h"
#include "runtime/mem_pool.h"
#include "runtime/memory/counting_allocator.h"
#include "runtime/runtime_state.h"

namespace starrocks {

namespace {

class ManagedAggrState {
public:
    ~ManagedAggrState() { _func->destroy(_ctx, _state); }

    static std::unique_ptr<ManagedAggrState> create(FunctionContext* ctx, const AggregateFunction* func) {
        return std::make_unique<ManagedAggrState>(ctx, func);
    }

    AggDataPtr state() { return _state; }

    ManagedAggrState(FunctionContext* ctx, const AggregateFunction* func) : _ctx(ctx), _func(func) {
        _state = _mem_pool.allocate_aligned(func->size(), func->alignof_size());
        _func->create(_ctx, _state);
    }

private:
    FunctionContext* _ctx;
    const AggregateFunction* _func;
    MemPool _mem_pool;
    AggDataPtr _state;
};

TypeDescriptor logical_types_to_struct_type(const std::vector<LogicalType>& logical_types) {
    TypeDescriptor struct_type;
    struct_type.type = LogicalType::TYPE_STRUCT;
    for (int i = 0; i < logical_types.size(); ++i) {
        TypeDescriptor array_type;
        array_type.type = LogicalType::TYPE_ARRAY;
        array_type.children.emplace_back(logical_types[i]);
        struct_type.children.emplace_back(array_type);
        struct_type.field_names.emplace_back(StrCat("col", i));
    }
    return struct_type;
}

std::vector<FunctionContext::TypeDesc> to_arg_types(const std::vector<LogicalType>& logical_types) {
    std::vector<FunctionContext::TypeDesc> arg_types;
    for (auto lt : logical_types) {
        arg_types.emplace_back(CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(lt)));
    }
    return arg_types;
}

// The struct-of-arrays result of one group, as datums.
DatumStruct finalized_row(const ColumnPtr& res_col) {
    return ColumnHelper::get_data_column(res_col.get())->get(0).get_struct();
}

} // namespace

class CelonisMultiArrayAggV3Test : public testing::Test {
public:
    void SetUp() override {
        _allocator = std::make_unique<CountingAllocatorWithHook>();
        _alloc_setter = std::make_unique<ThreadLocalAggregateStateAllocatorSetter>(_allocator.get());
    }

    void TearDown() override {
        tls_agg_state_allocator = nullptr;
        _alloc_setter.reset();
        _allocator.reset();
    }

private:
    std::unique_ptr<CountingAllocatorWithHook> _allocator;
    std::unique_ptr<ThreadLocalAggregateStateAllocatorSetter> _alloc_setter;
};

// Mirror of test_multi_array_agg_v2 in aggregate_test.cpp, same data and same expected output.
//
// The expectation is deliberately identical to V2's even though V3 stores rows in reverse
// insertion order: the ORDER BY column here holds distinct values, so the finalize sort maps
// either storage order onto the same sequence. Ordering only diverges without ORDER BY -- see
// no_order_by_exposes_reverse_storage_order below.
TEST_F(CelonisMultiArrayAggV3Test, update_serialize_merge_finalize) {
    auto arg_types = to_arg_types({TYPE_VARCHAR, TYPE_VARCHAR, TYPE_INT});
    auto return_type =
            CelonisAnyValUtil::column_type_to_type_desc(logical_types_to_struct_type({TYPE_VARCHAR, TYPE_VARCHAR}));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    MemPool mem_pool;
    std::unique_ptr<FunctionContext> local_ctx(
            FunctionContext::create_test_context(&mem_pool, std::move(arg_types), return_type));
    local_ctx->set_is_asc_order({false});
    local_ctx->set_nulls_first({true});
    local_ctx->set_runtime_state(runtime_state.get());
    local_ctx->set_multi_array_agg_column_serialization_size({0, 0, 0});

    const AggregateFunction* agg_func = get_aggregate_function("multi_array_agg_v3", TYPE_VARCHAR, TYPE_STRUCT, false);
    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);

    auto char_type = TypeDescriptor::create_varchar_type(30);
    ColumnPtr char_column_1 = ColumnHelper::create_column(char_type, true);
    char_column_1->append_datum(Datum());
    char_column_1->append_datum("A");
    char_column_1->append_datum("B");
    char_column_1->append_datum(Datum());
    char_column_1->append_datum("C");
    char_column_1->append_datum(Datum());
    char_column_1->append_datum("D");
    char_column_1->append_datum("E");
    char_column_1->append_datum(Datum());
    char_column_1->append_datum("F");
    char_column_1->append_datum("G");

    ColumnPtr char_column_2 = ColumnHelper::create_column(char_type, true);
    char_column_2->append_datum(Datum());
    char_column_2->append_datum("a");
    char_column_2->append_datum("b");
    char_column_2->append_datum(Datum());
    char_column_2->append_datum("c");
    char_column_2->append_datum(Datum());
    char_column_2->append_datum("d");
    char_column_2->append_datum("e");
    char_column_2->append_datum(Datum());
    char_column_2->append_datum("f");
    char_column_2->append_datum("g");

    auto int_type = TypeDescriptor::from_logical_type(LogicalType::TYPE_INT);
    ColumnPtr int_column = ColumnHelper::create_column(int_type, true);
    int_column->append_datum(Datum());
    int_column->append_datum(1);
    int_column->append_datum(9);
    int_column->append_datum(2);
    int_column->append_datum(3);
    int_column->append_datum(8);
    int_column->append_datum(4);
    int_column->append_datum(10);
    int_column->append_datum(5);
    int_column->append_datum(6);
    int_column->append_datum(7);

    std::vector<ColumnPtr> columns{char_column_1, char_column_2, int_column};
    std::vector<const Column*> raw_columns{char_column_1.get(), char_column_2.get(), int_column.get()};

    const std::string expected =
            "[{col0:[NULL,'E','B',NULL,'G','F',NULL,'D','C',NULL,'A'],col1:[NULL,'e','b',NULL,'g','f',NULL,'d'"
            ",'c',NULL,'a']}]";

    // update -> serialize -> merge -> finalize
    agg_func->update_batch_single_state(local_ctx.get(), int_column->size(), raw_columns.data(), state->state());

    ColumnPtr serialized_col = ColumnHelper::create_column(TypeDescriptor(LogicalType::TYPE_VARBINARY), true);
    agg_func->serialize_to_column(local_ctx.get(), state->state(), serialized_col.get());

    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    agg_func->merge_batch_single_state(local_ctx.get(), state->state(), serialized_col.get(), 0,
                                       serialized_col->size());
    ColumnPtr res_col = ColumnHelper::create_column(logical_types_to_struct_type({TYPE_VARCHAR, TYPE_VARCHAR}), true);
    agg_func->finalize_to_column(local_ctx.get(), state->state(), res_col.get());
    EXPECT_EQ(res_col->debug_string(), expected);

    // convert_to_serialize_format -> merge -> finalize
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    serialized_col->resize(0);
    agg_func->convert_to_serialize_format(local_ctx.get(), columns, int_column->size(), &serialized_col);
    agg_func->merge_batch_single_state(local_ctx.get(), state->state(), serialized_col.get(), 0,
                                       serialized_col->size());
    res_col->resize(0);
    agg_func->finalize_to_column(local_ctx.get(), state->state(), res_col.get());
    EXPECT_EQ(res_col->debug_string(), expected);
}

// Mirror of test_multi_array_agg_v2_session_size_limit. Note this uses the MemPool overload of
// create_test_context, unlike the V2 test: V3 allocates each row from ctx->mem_pool().
TEST_F(CelonisMultiArrayAggV3Test, session_size_limit) {
    auto arg_types = to_arg_types({TYPE_VARCHAR, TYPE_INT});
    auto return_type = CelonisAnyValUtil::column_type_to_type_desc(logical_types_to_struct_type({TYPE_VARCHAR}));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    MemPool mem_pool;
    std::unique_ptr<FunctionContext> local_ctx(
            FunctionContext::create_test_context(&mem_pool, std::move(arg_types), return_type));
    local_ctx->set_is_asc_order({false});
    local_ctx->set_nulls_first({true});
    local_ctx->set_runtime_state(runtime_state.get());
    local_ctx->set_multi_array_agg_column_serialization_size({0, 0});

    // Limit each aggregated array to 2 elements.
    local_ctx->set_multi_array_agg_max_array_length(2);

    const AggregateFunction* agg_func = get_aggregate_function("multi_array_agg_v3", TYPE_VARCHAR, TYPE_STRUCT, false);
    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);

    auto char_type = TypeDescriptor::create_varchar_type(30);
    ColumnPtr char_column = ColumnHelper::create_column(char_type, true);
    char_column->append_datum("a");
    char_column->append_datum("b");
    char_column->append_datum("c");
    char_column->append_datum("d");
    char_column->append_datum("e");

    auto int_type = TypeDescriptor::from_logical_type(LogicalType::TYPE_INT);
    ColumnPtr int_column = ColumnHelper::create_column(int_type, true);
    int_column->append_datum(5);
    int_column->append_datum(4);
    int_column->append_datum(3);
    int_column->append_datum(2);
    int_column->append_datum(1);

    std::vector<const Column*> raw_columns{char_column.get(), int_column.get()};

    ASSERT_FALSE(local_ctx->has_error());
    agg_func->update_batch_single_state(local_ctx.get(), char_column->size(), raw_columns.data(), state->state());
    ASSERT_TRUE(local_ctx->has_error());
    EXPECT_NE(nullptr, ::strstr(local_ctx->error_msg(), "size limit (2) of multi_array_agg_v3 is reached"));
}

// update() accepts a group of exactly `limit` rows, so merge() of the resulting intermediate has to
// accept it too, otherwise two-phase aggregation fails on the documented maximum.
TEST_F(CelonisMultiArrayAggV3Test, merge_accepts_group_exactly_on_size_limit) {
    auto arg_types = to_arg_types({TYPE_VARCHAR, TYPE_INT});
    auto return_type = CelonisAnyValUtil::column_type_to_type_desc(logical_types_to_struct_type({TYPE_VARCHAR}));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    MemPool mem_pool;
    std::unique_ptr<FunctionContext> local_ctx(
            FunctionContext::create_test_context(&mem_pool, std::move(arg_types), return_type));
    local_ctx->set_is_asc_order({false});
    local_ctx->set_nulls_first({true});
    local_ctx->set_runtime_state(runtime_state.get());
    local_ctx->set_multi_array_agg_column_serialization_size({0, 0});
    local_ctx->set_multi_array_agg_max_array_length(2);

    const AggregateFunction* agg_func = get_aggregate_function("multi_array_agg_v3", TYPE_VARCHAR, TYPE_STRUCT, false);
    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);

    auto char_type = TypeDescriptor::create_varchar_type(30);
    ColumnPtr char_column = ColumnHelper::create_column(char_type, true);
    char_column->append_datum("a");
    char_column->append_datum("b");

    auto int_type = TypeDescriptor::from_logical_type(LogicalType::TYPE_INT);
    ColumnPtr int_column = ColumnHelper::create_column(int_type, true);
    int_column->append_datum(2);
    int_column->append_datum(1);

    std::vector<const Column*> raw_columns{char_column.get(), int_column.get()};

    // Phase 1: exactly `limit` rows.
    agg_func->update_batch_single_state(local_ctx.get(), char_column->size(), raw_columns.data(), state->state());
    ASSERT_FALSE(local_ctx->has_error());

    ColumnPtr serialized_col = ColumnHelper::create_column(TypeDescriptor(LogicalType::TYPE_VARBINARY), true);
    agg_func->serialize_to_column(local_ctx.get(), state->state(), serialized_col.get());
    ASSERT_FALSE(local_ctx->has_error());

    // Phase 2: merging that intermediate back must not trip the limit.
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    agg_func->merge_batch_single_state(local_ctx.get(), state->state(), serialized_col.get(), 0,
                                       serialized_col->size());
    ASSERT_FALSE(local_ctx->has_error());

    ColumnPtr res_col = ColumnHelper::create_column(logical_types_to_struct_type({TYPE_VARCHAR}), true);
    agg_func->finalize_to_column(local_ctx.get(), state->state(), res_col.get());
    EXPECT_EQ(res_col->debug_string(), "[{col0:['a','b']}]");

    // A second merge of the same blob puts the total past the limit and must be rejected.
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    agg_func->merge_batch_single_state(local_ctx.get(), state->state(), serialized_col.get(), 0,
                                       serialized_col->size());
    ASSERT_FALSE(local_ctx->has_error());
    agg_func->merge_batch_single_state(local_ctx.get(), state->state(), serialized_col.get(), 0,
                                       serialized_col->size());
    ASSERT_TRUE(local_ctx->has_error());
    EXPECT_NE(nullptr, ::strstr(local_ctx->error_msg(), "size limit (2) of multi_array_agg_v3 is reached"));
}

// Without ORDER BY nothing sorts the rows, so the arrays come back in storage order. V3
// front-inserts its nodes, so that is the reverse of V2's insertion order. This is the one
// user-visible behavioural difference between the two implementations.
TEST_F(CelonisMultiArrayAggV3Test, no_order_by_exposes_reverse_storage_order) {
    auto arg_types = to_arg_types({TYPE_VARCHAR});
    auto return_type = CelonisAnyValUtil::column_type_to_type_desc(logical_types_to_struct_type({TYPE_VARCHAR}));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    MemPool mem_pool;
    std::unique_ptr<FunctionContext> local_ctx(
            FunctionContext::create_test_context(&mem_pool, std::move(arg_types), return_type));
    local_ctx->set_runtime_state(runtime_state.get());
    local_ctx->set_multi_array_agg_column_serialization_size({0});

    auto char_type = TypeDescriptor::create_varchar_type(30);
    ColumnPtr char_column = ColumnHelper::create_column(char_type, true);
    char_column->append_datum("a");
    char_column->append_datum("b");
    char_column->append_datum("c");
    char_column->append_datum("d");
    std::vector<const Column*> raw_columns{char_column.get()};

    auto finalize_with = [&](const char* func_name) {
        const AggregateFunction* agg_func = get_aggregate_function(func_name, TYPE_VARCHAR, TYPE_STRUCT, false);
        auto state = ManagedAggrState::create(local_ctx.get(), agg_func);
        agg_func->update_batch_single_state(local_ctx.get(), char_column->size(), raw_columns.data(), state->state());
        ColumnPtr res_col = ColumnHelper::create_column(logical_types_to_struct_type({TYPE_VARCHAR}), true);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_col.get());
        return res_col->debug_string();
    };

    EXPECT_EQ(finalize_with("multi_array_agg_v3"), "[{col0:['d','c','b','a']}]");
    EXPECT_EQ(finalize_with("multi_array_agg_v2"), "[{col0:['a','b','c','d']}]");
}

// Exercises the fixed-width dict encoding (_serialize_fixed_length / _deserialize_fixed_length),
// which the V2 tests never cover -- they pass a serialization size of 0 for every field. Widths
// 1/2/3 are tested at their maximum representable value.
TEST_F(CelonisMultiArrayAggV3Test, dict_encoded_fields_roundtrip) {
    auto arg_types = to_arg_types({TYPE_INT, TYPE_INT, TYPE_INT});
    auto return_type =
            CelonisAnyValUtil::column_type_to_type_desc(logical_types_to_struct_type({TYPE_INT, TYPE_INT, TYPE_INT}));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    MemPool mem_pool;
    std::unique_ptr<FunctionContext> local_ctx(
            FunctionContext::create_test_context(&mem_pool, std::move(arg_types), return_type));
    local_ctx->set_runtime_state(runtime_state.get());
    local_ctx->set_multi_array_agg_column_serialization_size({1, 2, 3});

    auto int_type = TypeDescriptor::from_logical_type(LogicalType::TYPE_INT);
    ColumnPtr c0 = ColumnHelper::create_column(int_type, true);
    ColumnPtr c1 = ColumnHelper::create_column(int_type, true);
    ColumnPtr c2 = ColumnHelper::create_column(int_type, true);
    // row 0: mid-range, row 1: zeroes, row 2: max for each width, row 3: a null in the 1-byte field
    c0->append_datum(200);
    c1->append_datum(60000);
    c2->append_datum(1000000);
    c0->append_datum(0);
    c1->append_datum(0);
    c2->append_datum(0);
    c0->append_datum(255);
    c1->append_datum(65535);
    c2->append_datum(16777215);
    c0->append_datum(Datum());
    c1->append_datum(5);
    c2->append_datum(5);

    std::vector<const Column*> raw_columns{c0.get(), c1.get(), c2.get()};

    const AggregateFunction* agg_func = get_aggregate_function("multi_array_agg_v3", TYPE_INT, TYPE_STRUCT, false);
    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);
    agg_func->update_batch_single_state(local_ctx.get(), c0->size(), raw_columns.data(), state->state());

    // Round-trip through the serialized blob so both the write and read halves are covered.
    ColumnPtr serialized_col = ColumnHelper::create_column(TypeDescriptor(LogicalType::TYPE_VARBINARY), true);
    agg_func->serialize_to_column(local_ctx.get(), state->state(), serialized_col.get());
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    agg_func->merge_batch_single_state(local_ctx.get(), state->state(), serialized_col.get(), 0,
                                       serialized_col->size());

    ColumnPtr res_col = ColumnHelper::create_column(logical_types_to_struct_type({TYPE_INT, TYPE_INT, TYPE_INT}), true);
    agg_func->finalize_to_column(local_ctx.get(), state->state(), res_col.get());
    ASSERT_FALSE(local_ctx->has_error());

    // No ORDER BY, so rows come back reversed: row 3, 2, 1, 0.
    DatumStruct row = finalized_row(res_col);
    ASSERT_EQ(3, row.size());
    const DatumArray& f0 = row[0].get_array();
    const DatumArray& f1 = row[1].get_array();
    const DatumArray& f2 = row[2].get_array();
    ASSERT_EQ(4, f0.size());
    ASSERT_EQ(4, f1.size());
    ASSERT_EQ(4, f2.size());

    EXPECT_TRUE(f0[0].is_null());
    EXPECT_EQ(255, f0[1].get_int32());
    EXPECT_EQ(0, f0[2].get_int32());
    EXPECT_EQ(200, f0[3].get_int32());

    EXPECT_EQ(5, f1[0].get_int32());
    EXPECT_EQ(65535, f1[1].get_int32());
    EXPECT_EQ(0, f1[2].get_int32());
    EXPECT_EQ(60000, f1[3].get_int32());

    EXPECT_EQ(5, f2[0].get_int32());
    EXPECT_EQ(16777215, f2[1].get_int32());
    EXPECT_EQ(0, f2[2].get_int32());
    EXPECT_EQ(1000000, f2[3].get_int32());
}

// serialize_to_column hands the rows off and clears the state, so the same state must be reusable
// for a fresh group. Guards _clear_list: a stale num_rows or list head here would leak the first
// group's rows into the second.
TEST_F(CelonisMultiArrayAggV3Test, state_is_reusable_after_serialize) {
    auto arg_types = to_arg_types({TYPE_VARCHAR});
    auto return_type = CelonisAnyValUtil::column_type_to_type_desc(logical_types_to_struct_type({TYPE_VARCHAR}));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    MemPool mem_pool;
    std::unique_ptr<FunctionContext> local_ctx(
            FunctionContext::create_test_context(&mem_pool, std::move(arg_types), return_type));
    local_ctx->set_runtime_state(runtime_state.get());
    local_ctx->set_multi_array_agg_column_serialization_size({0});

    auto char_type = TypeDescriptor::create_varchar_type(30);
    ColumnPtr first = ColumnHelper::create_column(char_type, true);
    first->append_datum("a");
    first->append_datum("b");
    ColumnPtr second = ColumnHelper::create_column(char_type, true);
    second->append_datum("z");

    const AggregateFunction* agg_func = get_aggregate_function("multi_array_agg_v3", TYPE_VARCHAR, TYPE_STRUCT, false);
    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);

    std::vector<const Column*> first_cols{first.get()};
    agg_func->update_batch_single_state(local_ctx.get(), first->size(), first_cols.data(), state->state());

    ColumnPtr serialized_col = ColumnHelper::create_column(TypeDescriptor(LogicalType::TYPE_VARBINARY), true);
    agg_func->serialize_to_column(local_ctx.get(), state->state(), serialized_col.get());

    // Same state, new group.
    std::vector<const Column*> second_cols{second.get()};
    agg_func->update_batch_single_state(local_ctx.get(), second->size(), second_cols.data(), state->state());

    ColumnPtr res_col = ColumnHelper::create_column(logical_types_to_struct_type({TYPE_VARCHAR}), true);
    agg_func->finalize_to_column(local_ctx.get(), state->state(), res_col.get());
    EXPECT_EQ(res_col->debug_string(), "[{col0:['z']}]");
}

} // namespace starrocks
