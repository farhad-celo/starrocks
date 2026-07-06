#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "column/column_builder.h"
#include "column/fixed_length_column.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/agg/factory_calendar.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "testutil/function_utils.h"

namespace starrocks {

class CelonisAggregateTest : public testing::Test {
public:
    CelonisAggregateTest() = default;

    void SetUp() override {
        utils = new FunctionUtils();
        ctx = utils->get_fn_ctx();
    }

    void TearDown() override { delete utils; }

private:
    FunctionUtils *utils{};
    FunctionContext *ctx{};
};

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

TEST_F(CelonisAggregateTest, test_celonis_make_factory_calendar) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};

    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    local_ctx->set_runtime_state(runtime_state.get());

    const AggregateFunction *agg_func = get_aggregate_function("celonis_make_factory_calendar", TYPE_BIGINT, TYPE_ARRAY,
                                                               false);
    TypeDescriptor type_timestamp;
    type_timestamp.type = LogicalType::TYPE_DATETIME;
    TypeDescriptor type_varchar;
    type_varchar.type = LogicalType::TYPE_VARCHAR;
    TypeDescriptor type_boolean;
    type_boolean.type = LogicalType::TYPE_BOOLEAN;
    TypeDescriptor type_struct;
    type_struct.type = LogicalType::TYPE_STRUCT;
    type_struct.children.emplace_back(type_timestamp);
    type_struct.children.emplace_back(type_timestamp);
    type_struct.children.emplace_back(type_varchar);
    type_struct.children.emplace_back(type_boolean);
    type_struct.field_names.emplace_back("start_timestamp");
    type_struct.field_names.emplace_back("end_timestamp");
    type_struct.field_names.emplace_back("calendar_id");
    type_struct.field_names.emplace_back("is_calendar_id_null");

    TypeDescriptor type_array_char;
    type_array_char.type = LogicalType::TYPE_ARRAY;
    type_array_char.children.emplace_back(LogicalType::TYPE_VARCHAR);

    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);
    // NULL calendar_id
    {
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        start_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        start_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));

        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);

        std::vector<const Column *> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState *) (state->state());
        EXPECT_EQ(2, agg_state->start_timestamp->size());
        EXPECT_EQ(2, agg_state->end_timestamp->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(start_timestamp_column->debug_string(), agg_state->start_timestamp->debug_string());
        EXPECT_EQ(end_timestamp_column->debug_string(), agg_state->end_timestamp->debug_string());
        EXPECT_EQ("['', '']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[1, 1]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 01:00:00,calendar_id:'',is_calendar_id_null:1}, {start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 02:00:00,calendar_id:'',is_calendar_id_null:1}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(start_timestamp_column);
        columns.push_back(end_timestamp_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, start_timestamp_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                R"([{start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 01:00:00,calendar_id:'',is_calendar_id_null:1}, {start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 02:00:00,calendar_id:'',is_calendar_id_null:1}])",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(
                R"(['{"factoryCalendar":{"entries":[{"startDate":"0","endDate":"3600000"},{"startDate":"0","endDate":"7200000"}]}}'])",
                res_array_col->debug_string());
    }
    // mixed NULL and non-NULL start/end, NULL calendar_id
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        start_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        start_timestamp_column->append_datum(kNullDatum);
        start_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        start_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        ASSERT_TRUE(start_timestamp_column->is_nullable() && start_timestamp_column->is_null(1));

        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));
        end_timestamp_column->append_datum(kNullDatum);
        ASSERT_TRUE(end_timestamp_column->is_nullable() && end_timestamp_column->is_null(3));

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);

        std::vector<const Column *> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        ASSERT_TRUE(start_timestamp_column->size() == 4);
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState *) (state->state());
        EXPECT_EQ(2, agg_state->start_timestamp->size());
        EXPECT_EQ(2, agg_state->end_timestamp->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ("[1970-01-01 00:00:00, 1970-01-01 00:00:00]", agg_state->start_timestamp->debug_string());
        EXPECT_EQ("[1970-01-01 01:00:00, 1970-01-01 02:00:00]", agg_state->end_timestamp->debug_string());
        EXPECT_EQ("['', '']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[1, 1]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 01:00:00,calendar_id:'',is_calendar_id_null:1}, {start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 02:00:00,calendar_id:'',is_calendar_id_null:1}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(start_timestamp_column);
        columns.push_back(end_timestamp_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, start_timestamp_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                R"([{start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 01:00:00,calendar_id:'',is_calendar_id_null:1}, {start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 02:00:00,calendar_id:'',is_calendar_id_null:1}])",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(
                R"(['{"factoryCalendar":{"entries":[{"startDate":"0","endDate":"3600000"},{"startDate":"0","endDate":"7200000"}]}}'])",
                res_array_col->debug_string());
    }
    // non-NULL calendar_id
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        start_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        start_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));

        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("id1");
        calendar_id_column->append_datum("id2");

        std::vector<const Column *> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState *) (state->state());
        EXPECT_EQ(2, agg_state->start_timestamp->size());
        EXPECT_EQ(2, agg_state->end_timestamp->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(start_timestamp_column->debug_string(), agg_state->start_timestamp->debug_string());
        EXPECT_EQ(end_timestamp_column->debug_string(), agg_state->end_timestamp->debug_string());
        EXPECT_EQ("['id1', 'id2']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[0, 0]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 01:00:00,calendar_id:'id1',is_calendar_id_null:0}, {start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 02:00:00,calendar_id:'id2',is_calendar_id_null:0}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(start_timestamp_column);
        columns.push_back(end_timestamp_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, start_timestamp_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                R"([{start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 01:00:00,calendar_id:'id1',is_calendar_id_null:0}, {start_timestamp:1970-01-01 00:00:00,end_timestamp:1970-01-01 02:00:00,calendar_id:'id2',is_calendar_id_null:0}])",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(
                R"(['{"factoryCalendar":{"entries":[{"startDate":"0","endDate":"3600000","calendarId":"id1"},{"startDate":"0","endDate":"7200000","calendarId":"id2"}]}}'])",
                res_array_col->debug_string());
    }
    // empty input
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);

        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);

        std::vector<const Column *> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState *) (state->state());
        EXPECT_EQ(0, agg_state->start_timestamp->size());
        EXPECT_EQ(0, agg_state->end_timestamp->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(start_timestamp_column);
        columns.push_back(end_timestamp_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, start_timestamp_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("['{}']", res_array_col->debug_string());
    }
    // no valid rows, row one has NULL start timestamp, row two has NULL end timestamp.
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        start_timestamp_column->append_datum(kNullDatum);
        start_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));

        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        end_timestamp_column->append_datum(kNullDatum);


        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("id1");
        calendar_id_column->append_datum("id2");

        std::vector<const Column *> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState *) (state->state());
        EXPECT_EQ(0, agg_state->start_timestamp->size());
        EXPECT_EQ(0, agg_state->end_timestamp->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(start_timestamp_column);
        columns.push_back(end_timestamp_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, start_timestamp_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("['{}']", res_array_col->debug_string());
    }
    // no valid rows, both row one and row two have NULL start timestamp.
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        start_timestamp_column->append_datum(kNullDatum);
        start_timestamp_column->append_datum(kNullDatum);

        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));


        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("id1");
        calendar_id_column->append_datum("id2");

        std::vector<const Column *> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState *) (state->state());
        EXPECT_EQ(0, agg_state->start_timestamp->size());
        EXPECT_EQ(0, agg_state->end_timestamp->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(start_timestamp_column);
        columns.push_back(end_timestamp_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, start_timestamp_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("['{}']", res_array_col->debug_string());
    }
    // negative timestamp
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        start_timestamp_column->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
        start_timestamp_column->append_datum(TimestampValue::create(1969, 12, 30, 0, 0, 0));

        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));


        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("id1");
        calendar_id_column->append_datum("id2");

        std::vector<const Column *> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState *) (state->state());
        EXPECT_EQ(2, agg_state->start_timestamp->size());
        EXPECT_EQ(2, agg_state->end_timestamp->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(start_timestamp_column->debug_string(), agg_state->start_timestamp->debug_string());
        EXPECT_EQ(end_timestamp_column->debug_string(), agg_state->end_timestamp->debug_string());
        EXPECT_EQ(calendar_id_column->debug_string(), agg_state->calendar_id->debug_string());
        EXPECT_EQ("[0, 0]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{start_timestamp:1969-12-31 00:00:00,end_timestamp:1970-01-01 01:00:00,calendar_id:'id1',is_calendar_id_null:0}, {start_timestamp:1969-12-30 00:00:00,end_timestamp:1970-01-01 02:00:00,calendar_id:'id2',is_calendar_id_null:0}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(start_timestamp_column);
        columns.push_back(end_timestamp_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, start_timestamp_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{start_timestamp:1969-12-31 00:00:00,end_timestamp:1970-01-01 01:00:00,calendar_id:'id1',is_calendar_id_null:0}, {start_timestamp:1969-12-30 00:00:00,end_timestamp:1970-01-01 02:00:00,calendar_id:'id2',is_calendar_id_null:0}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(
                R"(['{"factoryCalendar":{"entries":[{"startDate":"-86400000","endDate":"3600000","calendarId":"id1"},{"startDate":"-172800000","endDate":"7200000","calendarId":"id2"}]}}'])",
                res_array_col->debug_string());
    }
    // resultant calendar is longer than 1M.
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        const int64_t n_rows = 20000;
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        for (auto i = 0; i < n_rows; ++i) {
            start_timestamp_column->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
            end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
            calendar_id_column->append_datum("id1");
        }

        std::vector<const Column *> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState *) (state->state());
        EXPECT_EQ(n_rows, agg_state->start_timestamp->size());
        EXPECT_EQ(n_rows, agg_state->end_timestamp->size());
        EXPECT_EQ(n_rows, agg_state->calendar_id->size());
        EXPECT_EQ(n_rows, agg_state->is_calendar_id_null->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_GT(res_array_col->debug_string().size(), 1000000);
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(2, res_array_col->get(0).get_array().size());
        EXPECT_LT(res_array_col->get(0).get_array()[0].get_slice().to_string().size(), 1000000);
        EXPECT_LT(res_array_col->get(0).get_array()[1].get_slice().to_string().size(), 1000000);
    }
}

} // namespace starrocks
