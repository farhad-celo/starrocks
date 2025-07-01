#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "../util.h"
#include "column/column_builder.h"
#include "column/fixed_length_column.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/base64.h"
#include "exprs/celonis/agg/factory_calendar.h"
#include "exprs/celonis/agg/linear_regression.h"
#include "exprs/celonis/agg/multi_array_agg.h"
#include "exprs/celonis/agg/weekday_calendar.h"
#include "exprs/celonis/agg/workday_calendar.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"
#include "gutil/strings/strcat.h"
#include "modules/query/calendars.pb.h"
#include "runtime/mem_pool.h"
#include "testutil/function_utils.h"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <google/protobuf/util/json_util.h>

namespace starrocks {

class CelonisAggregateTest : public testing::Test {
public:
    CelonisAggregateTest() = default;

    void SetUp() override {
        utils = new FunctionUtils();
        ctx = utils->get_fn_ctx();
    }

    void TearDown() override { delete utils; }

    bool parse_model(const std::string& model, double& intercept, double& slope) {
        std::istringstream iss(model);
        char delim;
        if (!(iss >> intercept >> delim >> slope)) {
            return false;
        }
        return true;
    }

    bool parse_model(const std::string& model, double& intercept, std::vector<double>& coefficients) {
        std::vector<std::string> parts;
        boost::split(parts, model, boost::is_any_of(":"));
        if (parts.size() < 2) {
            return false;
        }
        for (size_t i = 0; i < parts.size(); ++i) {
            try {
                auto value = boost::lexical_cast<double>(parts[i]);
                if (i == 0) {
                    intercept = value;
                } else {
                    coefficients.push_back(value);
                }
            } catch (const boost::bad_lexical_cast& e) {
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> to_calendar_json_string(const std::string& encoded_string) {
        std::unique_ptr<char[]> decoded_buffer(new char[encoded_string.length()]);
        int decoded_len = base64_decode3(encoded_string.data(), encoded_string.length(), decoded_buffer.get());
        // Check if the decoding was successful before attempting to parse.
        if (decoded_len < 0) {
            return std::nullopt;
        }
        std::string_view payload(decoded_buffer.get(), decoded_len);
        const char format_flag = !payload.empty() ? payload[0] : '\0';
        ::celonis::accelerator::Calendar calendar_proto;
        bool success = true;
        if (format_flag == ZLIB_COMPRESSED_FLAG) {
            std::string decompressed_data;
            if (!decompress_string(payload.substr(1), decompressed_data)) {
                return std::nullopt;
            }
            success = calendar_proto.ParseFromString(decompressed_data);
        } else if (format_flag == UNCOMPRESSED_FLAG) {
            auto protobuf_payload = payload.substr(1);
            success = calendar_proto.ParseFromArray(protobuf_payload.data(), protobuf_payload.size());
        } else {
            success = calendar_proto.ParseFromArray(payload.data(), payload.size());
        }
        if (!success) {
            return std::nullopt;
        }
        std::string calendar_json;
        google::protobuf::util::MessageToJsonString(calendar_proto, &calendar_json);
        return calendar_json;
    }

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

TEST_F(CelonisAggregateTest, test_celonis_make_workday_calendar) {
    const bool enable_workday_mask_in_workday_calendar = config::enable_workday_mask_in_workday_calendar;
    config::enable_workday_mask_in_workday_calendar = false;
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};

    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    local_ctx->set_runtime_state(runtime_state.get());

    const AggregateFunction* agg_func = get_aggregate_function("celonis_make_workday_calendar", TYPE_BIGINT, TYPE_ARRAY,
                                                               false);
    TypeDescriptor type_struct;
    type_struct.type = LogicalType::TYPE_STRUCT;
    type_struct.children.emplace_back(celonis::array_type(TYPE_BIGINT));
    type_struct.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
    type_struct.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
    type_struct.children.emplace_back(celonis::array_type(TYPE_BOOLEAN));
    type_struct.field_names.emplace_back("year");
    type_struct.field_names.emplace_back("is_workdays");
    type_struct.field_names.emplace_back("calendar_id");
    type_struct.field_names.emplace_back("is_calendar_id_null");

    TypeDescriptor type_array_char;
    type_array_char.type = LogicalType::TYPE_ARRAY;
    type_array_char.children.emplace_back(LogicalType::TYPE_VARCHAR);

    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);
    // NULL calendar_id
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        year_column->append_datum(1970L);
        year_column->append_datum(1971L);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        is_workdays_column->append_datum("0101");
        is_workdays_column->append_datum("1010");

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->year->size());
        EXPECT_EQ(2, agg_state->is_workdays->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(year_column->debug_string(), agg_state->year->debug_string());
        EXPECT_EQ(is_workdays_column->debug_string(), agg_state->is_workdays->debug_string());
        EXPECT_EQ("['', '']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[1, 1]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"workdayCalendar":{"entries":[{"year":"1970","isWorkday":[false,true,false,true]},{"year":"1971","isWorkday":[true,false,true,false]}]}})");
    }
    // mixed NULL and non-NULL year, is_workdays, NULL calendar_id
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        year_column->append_datum(1970L);
        year_column->append_datum(kNullDatum);
        year_column->append_datum(1971L);
        year_column->append_datum(1972L);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        is_workdays_column->append_datum("0101");
        is_workdays_column->append_datum("1100");
        is_workdays_column->append_datum("1010");
        is_workdays_column->append_datum(kNullDatum);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->year->size());
        EXPECT_EQ(2, agg_state->is_workdays->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ("[1970, 1971]", agg_state->year->debug_string());
        EXPECT_EQ("['0101', '1010']", agg_state->is_workdays->debug_string());
        EXPECT_EQ("['', '']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[1, 1]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"workdayCalendar":{"entries":[{"year":"1970","isWorkday":[false,true,false,true]},{"year":"1971","isWorkday":[true,false,true,false]}]}})");
    }
    // non-NULL calendar_id
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        year_column->append_datum(1970L);
        year_column->append_datum(1971L);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        is_workdays_column->append_datum("0101");
        is_workdays_column->append_datum("1010");

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("id1");
        calendar_id_column->append_datum("id2");

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->year->size());
        EXPECT_EQ(2, agg_state->is_workdays->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(year_column->debug_string(), agg_state->year->debug_string());
        EXPECT_EQ(is_workdays_column->debug_string(), agg_state->is_workdays->debug_string());
        EXPECT_EQ("['id1', 'id2']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[0, 0]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['id1','id2'],is_calendar_id_null:[0,0]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['id1','id2'],is_calendar_id_null:[0,0]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"workdayCalendar":{"entries":[{"year":"1970","isWorkday":[false,true,false,true],"calendarId":"id1"},{"year":"1971","isWorkday":[true,false,true,false],"calendarId":"id2"}]}})");
    }
    // result is sorted
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        year_column->append_datum(1971L);
        year_column->append_datum(1970L);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        is_workdays_column->append_datum("1010");
        is_workdays_column->append_datum("0101");

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("id2");
        calendar_id_column->append_datum("id1");

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->year->size());
        EXPECT_EQ(2, agg_state->is_workdays->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(year_column->debug_string(), agg_state->year->debug_string());
        EXPECT_EQ(is_workdays_column->debug_string(), agg_state->is_workdays->debug_string());
        EXPECT_EQ("['id2', 'id1']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[0, 0]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{year:[1971,1970],is_workdays:['1010','0101'],calendar_id:['id2','id1'],is_calendar_id_null:[0,0]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  R"({"workdayCalendar":{"entries":[{"year":"1970","isWorkday":[false,true,false,true],"calendarId":"id1"},{"year":"1971","isWorkday":[true,false,true,false],"calendarId":"id2"}]}})");
    }
    // empty input
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(0, agg_state->year->size());
        EXPECT_EQ(0, agg_state->is_workdays->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("[[]]", res_array_col->debug_string());
    }
    // no valid rows, row one has NULL year, row two has NULL is_workdays
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        year_column->append_datum(kNullDatum);
        year_column->append_datum(1970L);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        is_workdays_column->append_datum("1010");
        is_workdays_column->append_datum(kNullDatum);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);


        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(0, agg_state->year->size());
        EXPECT_EQ(0, agg_state->is_workdays->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("[[]]", res_array_col->debug_string());
    }
    // no valid rows, both rows contain NULL year
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        year_column->append_datum(kNullDatum);
        year_column->append_datum(kNullDatum);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        is_workdays_column->append_datum("1010");
        is_workdays_column->append_datum("0101");

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);


        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(0, agg_state->year->size());
        EXPECT_EQ(0, agg_state->is_workdays->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("[[]]", res_array_col->debug_string());
    }
    // resultant calendar is longer than 1M.
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        const int64_t n_rows = 400000;
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        year_column->reserve(n_rows);
        is_workdays_column->reserve(n_rows);
        calendar_id_column->reserve(n_rows);
        for (auto i = 0; i < n_rows; ++i) {
            year_column->append_datum(i + 1970L);
            is_workdays_column->append_datum("0000000000");
            calendar_id_column->append_datum("id1");
        }

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(n_rows, agg_state->year->size());
        EXPECT_EQ(n_rows, agg_state->is_workdays->size());
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
    config::enable_workday_mask_in_workday_calendar = enable_workday_mask_in_workday_calendar;
}

TEST_F(CelonisAggregateTest, test_celonis_make_workday_calendar_with_workday_mask_enabled) {
    const bool enable_workday_mask_in_workday_calendar = config::enable_workday_mask_in_workday_calendar;
    config::enable_workday_mask_in_workday_calendar = true;
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};

    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    local_ctx->set_runtime_state(runtime_state.get());

    const AggregateFunction* agg_func = get_aggregate_function("celonis_make_workday_calendar", TYPE_BIGINT, TYPE_ARRAY,
                                                               false);
    TypeDescriptor type_struct;
    type_struct.type = LogicalType::TYPE_STRUCT;
    type_struct.children.emplace_back(celonis::array_type(TYPE_BIGINT));
    type_struct.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
    type_struct.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
    type_struct.children.emplace_back(celonis::array_type(TYPE_BOOLEAN));
    type_struct.field_names.emplace_back("year");
    type_struct.field_names.emplace_back("is_workdays");
    type_struct.field_names.emplace_back("calendar_id");
    type_struct.field_names.emplace_back("is_calendar_id_null");

    TypeDescriptor type_array_char;
    type_array_char.type = LogicalType::TYPE_ARRAY;
    type_array_char.children.emplace_back(LogicalType::TYPE_VARCHAR);

    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);
    // NULL calendar_id
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        year_column->append_datum(1970L);
        year_column->append_datum(1971L);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        is_workdays_column->append_datum("0101");
        is_workdays_column->append_datum("1010");

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->year->size());
        EXPECT_EQ(2, agg_state->is_workdays->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(year_column->debug_string(), agg_state->year->debug_string());
        EXPECT_EQ(is_workdays_column->debug_string(), agg_state->is_workdays->debug_string());
        EXPECT_EQ("['', '']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[1, 1]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  "{\"workdayCalendar\":{\"entries\":[{\"year\":\"1970\",\"workdayMask\":\"Cg==\"},{\"year\":\"1971\",\"workdayMask\":\"BQ==\"}]}}");
    }
    // mixed NULL and non-NULL year, is_workdays, NULL calendar_id
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        year_column->append_datum(1970L);
        year_column->append_datum(kNullDatum);
        year_column->append_datum(1971L);
        year_column->append_datum(1972L);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        is_workdays_column->append_datum("0101");
        is_workdays_column->append_datum("1100");
        is_workdays_column->append_datum("1010");
        is_workdays_column->append_datum(kNullDatum);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->year->size());
        EXPECT_EQ(2, agg_state->is_workdays->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ("[1970, 1971]", agg_state->year->debug_string());
        EXPECT_EQ("['0101', '1010']", agg_state->is_workdays->debug_string());
        EXPECT_EQ("['', '']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[1, 1]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  "{\"workdayCalendar\":{\"entries\":[{\"year\":\"1970\",\"workdayMask\":\"Cg==\"},{\"year\":\"1971\",\"workdayMask\":\"BQ==\"}]}}");
    }
    // non-NULL calendar_id
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        year_column->append_datum(1970L);
        year_column->append_datum(1971L);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        is_workdays_column->append_datum("0101");
        is_workdays_column->append_datum("1010");

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("id1");
        calendar_id_column->append_datum("id2");

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->year->size());
        EXPECT_EQ(2, agg_state->is_workdays->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(year_column->debug_string(), agg_state->year->debug_string());
        EXPECT_EQ(is_workdays_column->debug_string(), agg_state->is_workdays->debug_string());
        EXPECT_EQ("['id1', 'id2']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[0, 0]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['id1','id2'],is_calendar_id_null:[0,0]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{year:[1970,1971],is_workdays:['0101','1010'],calendar_id:['id1','id2'],is_calendar_id_null:[0,0]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(),
                  "{\"workdayCalendar\":{\"entries\":[{\"year\":\"1970\",\"calendarId\":\"id1\",\"workdayMask\":\"Cg==\"},{\"year\":\"1971\",\"calendarId\":\"id2\",\"workdayMask\":\"BQ==\"}]}}");
    }
    // empty input
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(0, agg_state->year->size());
        EXPECT_EQ(0, agg_state->is_workdays->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("[[]]", res_array_col->debug_string());
    }
    // no valid rows, row one has NULL year, row two has NULL is_workdays
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        year_column->append_datum(kNullDatum);
        year_column->append_datum(1970L);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        is_workdays_column->append_datum("1010");
        is_workdays_column->append_datum(kNullDatum);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);


        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(0, agg_state->year->size());
        EXPECT_EQ(0, agg_state->is_workdays->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("[[]]", res_array_col->debug_string());
    }
    // no valid rows, both rows contain NULL year
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto year_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        year_column->append_datum(kNullDatum);
        year_column->append_datum(kNullDatum);

        auto is_workdays_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        is_workdays_column->append_datum("1010");
        is_workdays_column->append_datum("0101");

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);


        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = year_column.get();
        raw_columns[1] = is_workdays_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), year_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WorkdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(0, agg_state->year->size());
        EXPECT_EQ(0, agg_state->is_workdays->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(year_column);
        columns.push_back(is_workdays_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, year_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("[[]]", res_array_col->debug_string());
    }
    config::enable_workday_mask_in_workday_calendar = enable_workday_mask_in_workday_calendar;
}

TEST_F(CelonisAggregateTest, test_celonis_make_factory_calendar) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DATETIME)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};

    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    local_ctx->set_runtime_state(runtime_state.get());

    const AggregateFunction* agg_func = get_aggregate_function("celonis_make_factory_calendar", TYPE_BIGINT, TYPE_ARRAY,
                                                               false);
    TypeDescriptor type_struct;
    type_struct.type = LogicalType::TYPE_STRUCT;
    type_struct.children.emplace_back(celonis::array_type(TYPE_DATETIME));
    type_struct.children.emplace_back(celonis::array_type(TYPE_DATETIME));
    type_struct.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
    type_struct.children.emplace_back(celonis::array_type(TYPE_BOOLEAN));
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

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState*) (state->state());
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
                "[{start_timestamp:[1970-01-01 00:00:00,1970-01-01 00:00:00],end_timestamp:[1970-01-01 01:00:00,1970-01-01 02:00:00],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
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
                R"([{start_timestamp:[1970-01-01 00:00:00,1970-01-01 00:00:00],end_timestamp:[1970-01-01 01:00:00,1970-01-01 02:00:00],calendar_id:['',''],is_calendar_id_null:[1,1]}])",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"factoryCalendar":{"entries":[{"startDate":"0","endDate":"3600000"},{"startDate":"0","endDate":"7200000"}]}})");
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

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        ASSERT_TRUE(start_timestamp_column->size() == 4);
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState*) (state->state());
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
                "[{start_timestamp:[1970-01-01 00:00:00,1970-01-01 00:00:00],end_timestamp:[1970-01-01 01:00:00,1970-01-01 02:00:00],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
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
                R"([{start_timestamp:[1970-01-01 00:00:00,1970-01-01 00:00:00],end_timestamp:[1970-01-01 01:00:00,1970-01-01 02:00:00],calendar_id:['',''],is_calendar_id_null:[1,1]}])",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"factoryCalendar":{"entries":[{"startDate":"0","endDate":"3600000"},{"startDate":"0","endDate":"7200000"}]}})");
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

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState*) (state->state());
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
                "[{start_timestamp:[1970-01-01 00:00:00,1970-01-01 00:00:00],end_timestamp:[1970-01-01 01:00:00,1970-01-01 02:00:00],calendar_id:['id1','id2'],is_calendar_id_null:[0,0]}]",
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
                R"([{start_timestamp:[1970-01-01 00:00:00,1970-01-01 00:00:00],end_timestamp:[1970-01-01 01:00:00,1970-01-01 02:00:00],calendar_id:['id1','id2'],is_calendar_id_null:[0,0]}])",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"factoryCalendar":{"entries":[{"startDate":"0","endDate":"3600000","calendarId":"id1"},{"startDate":"0","endDate":"7200000","calendarId":"id2"}]}})");
    }
    // empty input
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);

        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState*) (state->state());
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
        EXPECT_EQ("[[]]", res_array_col->debug_string());
    }
    // no valid rows, row one has NULL start timestamp, row two has NULL end timestamp, row 3 has start > end.
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        start_timestamp_column->append_datum(kNullDatum);
        start_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));
        start_timestamp_column->append_datum(TimestampValue::create(1972, 1, 1, 0, 0, 0));

        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), true);
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
        end_timestamp_column->append_datum(kNullDatum);
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 0, 0, 0));


        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("id1");
        calendar_id_column->append_datum("id2");
        calendar_id_column->append_datum("id3");

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState*) (state->state());
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
        EXPECT_EQ("[[]]", res_array_col->debug_string());
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

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState*) (state->state());
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
        EXPECT_EQ("[[]]", res_array_col->debug_string());
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

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState*) (state->state());
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
                "[{start_timestamp:[1969-12-31 00:00:00,1969-12-30 00:00:00],end_timestamp:[1970-01-01 01:00:00,1970-01-01 02:00:00],calendar_id:['id1','id2'],is_calendar_id_null:[0,0]}]",
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
                "[{start_timestamp:[1969-12-31 00:00:00,1969-12-30 00:00:00],end_timestamp:[1970-01-01 01:00:00,1970-01-01 02:00:00],calendar_id:['id1','id2'],is_calendar_id_null:[0,0]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"factoryCalendar":{"entries":[{"startDate":"-86400000","endDate":"3600000","calendarId":"id1"},{"startDate":"-172800000","endDate":"7200000","calendarId":"id2"}]}})");
    }
    // The result is sorted
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        start_timestamp_column->append_datum(TimestampValue::create(1969, 12, 30, 0, 0, 0));
        start_timestamp_column->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));

        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 2, 0, 0));
        end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));


        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("id2");
        calendar_id_column->append_datum("id1");

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->start_timestamp->size());
        EXPECT_EQ(2, agg_state->end_timestamp->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(start_timestamp_column->debug_string(), agg_state->start_timestamp->debug_string());
        EXPECT_EQ(end_timestamp_column->debug_string(), agg_state->end_timestamp->debug_string());
        EXPECT_EQ(calendar_id_column->debug_string(), agg_state->calendar_id->debug_string());
        EXPECT_EQ("[0, 0]", agg_state->is_calendar_id_null->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"factoryCalendar":{"entries":[{"startDate":"-86400000","endDate":"3600000","calendarId":"id1"},{"startDate":"-172800000","endDate":"7200000","calendarId":"id2"}]}})");
    }
    // resultant calendar is longer than 1M.
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        const int64_t n_rows = 40000;
        auto start_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);
        auto end_timestamp_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DATETIME), false);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        for (auto i = 0; i < n_rows; ++i) {
            start_timestamp_column->append_datum(TimestampValue::create(1969, 12, 31, 0, 0, 0));
            end_timestamp_column->append_datum(TimestampValue::create(1970, 1, 1, 1, 0, 0));
            calendar_id_column->append_datum("id1");
        }

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = start_timestamp_column.get();
        raw_columns[1] = end_timestamp_column.get();
        raw_columns[2] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), start_timestamp_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (FactoryCalendarAggregateState*) (state->state());
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

TEST_F(CelonisAggregateTest, test_celonis_make_weekday_calendar_bigint_shift) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};

    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    local_ctx->set_runtime_state(runtime_state.get());

    const AggregateFunction* agg_func = get_aggregate_function("celonis_make_weekday_calendar", TYPE_BIGINT, TYPE_ARRAY,
                                                               false);
    TypeDescriptor type_struct;
    type_struct.type = LogicalType::TYPE_STRUCT;
    type_struct.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
    type_struct.children.emplace_back(celonis::array_type(TYPE_BIGINT));
    type_struct.children.emplace_back(celonis::array_type(TYPE_BIGINT));
    type_struct.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
    type_struct.children.emplace_back(celonis::array_type(TYPE_BOOLEAN));
    type_struct.field_names.emplace_back("weekday");
    type_struct.field_names.emplace_back("shift_begin");
    type_struct.field_names.emplace_back("shift_end");
    type_struct.field_names.emplace_back("calendar_id");
    type_struct.field_names.emplace_back("is_calendar_id_null");

    TypeDescriptor type_array_char;
    type_array_char.type = LogicalType::TYPE_ARRAY;
    type_array_char.children.emplace_back(LogicalType::TYPE_VARCHAR);

    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);
    // NULL calendar_id
    {
        auto weekday_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        weekday_column->append_datum("MONDAY");
        weekday_column->append_datum("FRIDAY");

        auto shift_begin_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        shift_begin_column->append_datum(123L);
        shift_begin_column->append_datum(456L);

        auto shift_end_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        shift_end_column->append_datum(123000L);
        shift_end_column->append_datum(456000L);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = weekday_column.get();
        raw_columns[1] = shift_begin_column.get();
        raw_columns[2] = shift_end_column.get();
        raw_columns[3] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), weekday_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WeekdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->weekday->size());
        EXPECT_EQ(2, agg_state->shift_begin->size());
        EXPECT_EQ(2, agg_state->shift_end->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(weekday_column->debug_string(), agg_state->weekday->debug_string());
        EXPECT_EQ(shift_begin_column->debug_string(), agg_state->shift_begin->debug_string());
        EXPECT_EQ(shift_end_column->debug_string(), agg_state->shift_end->debug_string());
        EXPECT_EQ("['', '']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[1, 1]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{weekday:['MONDAY','FRIDAY'],shift_begin:[123,456],shift_end:[123000,456000],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(weekday_column);
        columns.push_back(shift_begin_column);
        columns.push_back(shift_end_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, weekday_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{weekday:['MONDAY','FRIDAY'],shift_begin:[123,456],shift_end:[123000,456000],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"multiWeekdayCalendar":{"calendars":[{"monday":{"useDay":true,"shift":{"begin":123,"end":123000}},"friday":{"useDay":true,"shift":{"begin":456,"end":456000}}}]}})");
    }
    // mixed NULL and non-NULL weekday, NULL calendar_id
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto weekday_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        weekday_column->append_datum("TUESDAY");
        weekday_column->append_datum(kNullDatum);
        weekday_column->append_datum("THURSDAY");

        auto shift_begin_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        shift_begin_column->append_datum(123L);
        shift_begin_column->append_datum(123L);
        shift_begin_column->append_datum(456L);

        auto shift_end_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        shift_end_column->append_datum(123000L);
        shift_end_column->append_datum(123000L);
        shift_end_column->append_datum(456000L);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = weekday_column.get();
        raw_columns[1] = shift_begin_column.get();
        raw_columns[2] = shift_end_column.get();
        raw_columns[3] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), weekday_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WeekdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->weekday->size());
        EXPECT_EQ(2, agg_state->shift_begin->size());
        EXPECT_EQ(2, agg_state->shift_end->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ("['TUESDAY', 'THURSDAY']", agg_state->weekday->debug_string());
        EXPECT_EQ("[123, 456]", agg_state->shift_begin->debug_string());
        EXPECT_EQ("[123000, 456000]", agg_state->shift_end->debug_string());
        EXPECT_EQ("['', '']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[1, 1]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{weekday:['TUESDAY','THURSDAY'],shift_begin:[123,456],shift_end:[123000,456000],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(weekday_column);
        columns.push_back(shift_begin_column);
        columns.push_back(shift_end_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, weekday_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{weekday:['TUESDAY','THURSDAY'],shift_begin:[123,456],shift_end:[123000,456000],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"multiWeekdayCalendar":{"calendars":[{"tuesday":{"useDay":true,"shift":{"begin":123,"end":123000}},"thursday":{"useDay":true,"shift":{"begin":456,"end":456000}}}]}})");
    }
    // non-NULL calendar_id
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto weekday_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        weekday_column->append_datum("WEDNESDAY");
	    weekday_column->append_datum("FRIDAY");
        weekday_column->append_datum("SATURDAY");

        auto shift_begin_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        shift_begin_column->append_datum(123L);
	    shift_begin_column->append_datum(123L);
        shift_begin_column->append_datum(456L);

        auto shift_end_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        shift_end_column->append_datum(123000L);
	    shift_end_column->append_datum(123000L);
        shift_end_column->append_datum(456000L);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("DE");
 	    calendar_id_column->append_datum("DE");
        calendar_id_column->append_datum("US");

        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = weekday_column.get();
        raw_columns[1] = shift_begin_column.get();
        raw_columns[2] = shift_end_column.get();
        raw_columns[3] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), weekday_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WeekdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(3, agg_state->weekday->size());
        EXPECT_EQ(3, agg_state->shift_begin->size());
        EXPECT_EQ(3, agg_state->shift_end->size());
        EXPECT_EQ(3, agg_state->calendar_id->size());
        EXPECT_EQ(3, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(weekday_column->debug_string(), agg_state->weekday->debug_string());
        EXPECT_EQ(shift_begin_column->debug_string(), agg_state->shift_begin->debug_string());
        EXPECT_EQ(shift_end_column->debug_string(), agg_state->shift_end->debug_string());
        EXPECT_EQ("['DE', 'DE', 'US']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[0, 0, 0]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{weekday:['WEDNESDAY','FRIDAY','SATURDAY'],shift_begin:[123,123,456],shift_end:[123000,123000,456000],calendar_id:['DE','DE','US'],is_calendar_id_null:[0,0,0]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(weekday_column);
        columns.push_back(shift_begin_column);
        columns.push_back(shift_end_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, weekday_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{weekday:['WEDNESDAY','FRIDAY','SATURDAY'],shift_begin:[123,123,456],shift_end:[123000,123000,456000],calendar_id:['DE','DE','US'],is_calendar_id_null:[0,0,0]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"multiWeekdayCalendar":{"calendars":[{"wednesday":{"useDay":true,"shift":{"begin":123,"end":123000}},"friday":{"useDay":true,"shift":{"begin":123,"end":123000}},"calendarId":"DE"},{"saturday":{"useDay":true,"shift":{"begin":456,"end":456000}},"calendarId":"US"}]}})");
    }
    // empty input
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto weekday_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        auto shift_begin_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        auto shift_end_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = weekday_column.get();
        raw_columns[1] = shift_begin_column.get();
        raw_columns[2] = shift_end_column.get();
        raw_columns[3] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), weekday_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WeekdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(0, agg_state->weekday->size());
        EXPECT_EQ(0, agg_state->shift_begin->size());
        EXPECT_EQ(0, agg_state->shift_end->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(weekday_column);
        columns.push_back(shift_begin_column);
        columns.push_back(shift_end_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, weekday_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("[[]]", res_array_col->debug_string());
    }
    // no valid rows, row one has NULL shift_begin, row two has NULL shift_end.
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto weekday_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        weekday_column->append_datum("MONDAY");
        weekday_column->append_datum("FRIDAY");

        auto shift_begin_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        shift_begin_column->append_datum(kNullDatum);
        shift_begin_column->append_datum(456L);

        auto shift_end_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        shift_end_column->append_datum(123000L);
        shift_end_column->append_datum(kNullDatum);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);
        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = weekday_column.get();
        raw_columns[1] = shift_begin_column.get();
        raw_columns[2] = shift_end_column.get();
        raw_columns[3] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), weekday_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WeekdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(0, agg_state->weekday->size());
        EXPECT_EQ(0, agg_state->shift_begin->size());
        EXPECT_EQ(0, agg_state->shift_end->size());
        EXPECT_EQ(0, agg_state->calendar_id->size());
        EXPECT_EQ(0, agg_state->is_calendar_id_null->size());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(0, res_struct_col->size());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(weekday_column);
        columns.push_back(shift_begin_column);
        columns.push_back(shift_end_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, weekday_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(0, res_struct_col->size());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        // The result factory calendar does not contain any entries.
        EXPECT_EQ("[[]]", res_array_col->debug_string());
    }
    // negative shift
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto weekday_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        weekday_column->append_datum("MONDAY");
        weekday_column->append_datum("SUNDAY");

        auto shift_begin_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        shift_begin_column->append_datum(-123L);
        shift_begin_column->append_datum(456L);

        auto shift_end_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false);
        shift_end_column->append_datum(-123000L);
        shift_end_column->append_datum(456000L);

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = weekday_column.get();
        raw_columns[1] = shift_begin_column.get();
        raw_columns[2] = shift_end_column.get();
        raw_columns[3] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), weekday_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WeekdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->weekday->size());
        EXPECT_EQ(2, agg_state->shift_begin->size());
        EXPECT_EQ(2, agg_state->shift_end->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(weekday_column->debug_string(), agg_state->weekday->debug_string());
        EXPECT_EQ(shift_begin_column->debug_string(), agg_state->shift_begin->debug_string());
        EXPECT_EQ(shift_end_column->debug_string(), agg_state->shift_end->debug_string());
        EXPECT_EQ("['', '']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[1, 1]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{weekday:['MONDAY','SUNDAY'],shift_begin:[-123,456],shift_end:[-123000,456000],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(weekday_column);
        columns.push_back(shift_begin_column);
        columns.push_back(shift_end_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, weekday_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{weekday:['MONDAY','SUNDAY'],shift_begin:[-123,456],shift_end:[-123000,456000],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"multiWeekdayCalendar":{"calendars":[{"sunday":{"useDay":true,"shift":{"begin":456,"end":456000}}}]}})");
    }
}

TEST_F(CelonisAggregateTest, test_celonis_make_weekday_calendar_string_shift) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};

    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    local_ctx->set_runtime_state(runtime_state.get());

    const AggregateFunction* agg_func = get_aggregate_function("celonis_make_weekday_calendar", TYPE_BIGINT, TYPE_ARRAY,
                                                               false);
    TypeDescriptor type_struct;
    type_struct.type = LogicalType::TYPE_STRUCT;
    type_struct.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
    type_struct.children.emplace_back(celonis::array_type(TYPE_BIGINT));
    type_struct.children.emplace_back(celonis::array_type(TYPE_BIGINT));
    type_struct.children.emplace_back(celonis::array_type(TYPE_VARCHAR));
    type_struct.children.emplace_back(celonis::array_type(TYPE_BOOLEAN));
    type_struct.field_names.emplace_back("weekday");
    type_struct.field_names.emplace_back("shift_begin");
    type_struct.field_names.emplace_back("shift_end");
    type_struct.field_names.emplace_back("calendar_id");
    type_struct.field_names.emplace_back("is_calendar_id_null");

    TypeDescriptor type_array_char;
    type_array_char.type = LogicalType::TYPE_ARRAY;
    type_array_char.children.emplace_back(LogicalType::TYPE_VARCHAR);

    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);
    // NULL calendar_id
    {
        auto weekday_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        weekday_column->append_datum("MONDAY");
        weekday_column->append_datum("FRIDAY");

        auto shift_begin_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        shift_begin_column->append_datum("09:00");
        shift_begin_column->append_datum("08:00");

        auto shift_end_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        shift_end_column->append_datum("17:00");
        shift_end_column->append_datum("16:00");

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, true);
        calendar_id_column->append_datum(kNullDatum);
        calendar_id_column->append_datum(kNullDatum);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = weekday_column.get();
        raw_columns[1] = shift_begin_column.get();
        raw_columns[2] = shift_end_column.get();
        raw_columns[3] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), weekday_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WeekdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->weekday->size());
        EXPECT_EQ(2, agg_state->shift_begin->size());
        EXPECT_EQ(2, agg_state->shift_end->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(weekday_column->debug_string(), agg_state->weekday->debug_string());
        EXPECT_EQ("[32400000, 28800000]", agg_state->shift_begin->debug_string());
        EXPECT_EQ("[61200000, 57600000]", agg_state->shift_end->debug_string());
        EXPECT_EQ("['', '']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[1, 1]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{weekday:['MONDAY','FRIDAY'],shift_begin:[32400000,28800000],shift_end:[61200000,57600000],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(weekday_column);
        columns.push_back(shift_begin_column);
        columns.push_back(shift_end_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, weekday_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{weekday:['MONDAY','FRIDAY'],shift_begin:[32400000,28800000],shift_end:[61200000,57600000],calendar_id:['',''],is_calendar_id_null:[1,1]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"multiWeekdayCalendar":{"calendars":[{"monday":{"useDay":true,"shift":{"begin":32400000,"end":61200000}},"friday":{"useDay":true,"shift":{"begin":28800000,"end":57600000}}}]}})");
    }
    // Non-NULL calendar_id
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto weekday_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        weekday_column->append_datum("MONDAY");
        weekday_column->append_datum("FRIDAY");

        auto shift_begin_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        shift_begin_column->append_datum("00:00");
        shift_begin_column->append_datum("00:00");

        auto shift_end_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        shift_end_column->append_datum("24:00");
        shift_end_column->append_datum("24:00");

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("DE");
        calendar_id_column->append_datum("US");

        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = weekday_column.get();
        raw_columns[1] = shift_begin_column.get();
        raw_columns[2] = shift_end_column.get();
        raw_columns[3] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), weekday_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WeekdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(2, agg_state->weekday->size());
        EXPECT_EQ(2, agg_state->shift_begin->size());
        EXPECT_EQ(2, agg_state->shift_end->size());
        EXPECT_EQ(2, agg_state->calendar_id->size());
        EXPECT_EQ(2, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(weekday_column->debug_string(), agg_state->weekday->debug_string());
        EXPECT_EQ("[0, 0]", agg_state->shift_begin->debug_string());
        EXPECT_EQ("[86400000, 86400000]", agg_state->shift_end->debug_string());
        EXPECT_EQ("['DE', 'US']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[0, 0]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{weekday:['MONDAY','FRIDAY'],shift_begin:[0,0],shift_end:[86400000,86400000],calendar_id:['DE','US'],is_calendar_id_null:[0,0]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(weekday_column);
        columns.push_back(shift_begin_column);
        columns.push_back(shift_end_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, weekday_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{weekday:['MONDAY','FRIDAY'],shift_begin:[0,0],shift_end:[86400000,86400000],calendar_id:['DE','US'],is_calendar_id_null:[0,0]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"multiWeekdayCalendar":{"calendars":[{"monday":{"useDay":true,"shift":{"begin":0,"end":86400000}},"calendarId":"DE"},{"friday":{"useDay":true,"shift":{"begin":0,"end":86400000}},"calendarId":"US"}]}})");
    }
    // Non-NULL calendar_id with some invalid rows
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto weekday_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        weekday_column->append_datum("MONDAY");
        weekday_column->append_datum("FRIDAY");
        weekday_column->append_datum("TUESDAY");
        weekday_column->append_datum("THURSDAY");

        auto shift_begin_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        shift_begin_column->append_datum("00:00");
        shift_begin_column->append_datum("00:00");
        shift_begin_column->append_datum("HELLO");
        shift_begin_column->append_datum("00:15");

        auto shift_end_column = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
        shift_end_column->append_datum("24:00");
        shift_end_column->append_datum("24:00");
        shift_end_column->append_datum("23:15");
        shift_end_column->append_datum("75:15");

        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto calendar_id_column = ColumnHelper::create_column(char_type, false);
        calendar_id_column->append_datum("DE");
        calendar_id_column->append_datum("US");
        calendar_id_column->append_datum("DE");
        calendar_id_column->append_datum("US");

        std::vector<const Column*> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = weekday_column.get();
        raw_columns[1] = shift_begin_column.get();
        raw_columns[2] = shift_end_column.get();
        raw_columns[3] = calendar_id_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), weekday_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (WeekdayCalendarAggregateState*) (state->state());
        EXPECT_EQ(4, agg_state->weekday->size());
        EXPECT_EQ(4, agg_state->shift_begin->size());
        EXPECT_EQ(4, agg_state->shift_end->size());
        EXPECT_EQ(4, agg_state->calendar_id->size());
        EXPECT_EQ(4, agg_state->is_calendar_id_null->size());
        EXPECT_EQ(weekday_column->debug_string(), agg_state->weekday->debug_string());
        EXPECT_EQ("[0, 0, -1, 900000]", agg_state->shift_begin->debug_string());
        EXPECT_EQ("[86400000, 86400000, 83700000, -1]", agg_state->shift_end->debug_string());
        EXPECT_EQ("['DE', 'US', 'DE', 'US']", agg_state->calendar_id->debug_string());
        EXPECT_EQ("[0, 0, 0, 0]", agg_state->is_calendar_id_null->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ(
                "[{weekday:['MONDAY','FRIDAY','TUESDAY','THURSDAY'],shift_begin:[0,0,-1,900000],shift_end:[86400000,86400000,83700000,-1],calendar_id:['DE','US','DE','US'],is_calendar_id_null:[0,0,0,0]}]",
                res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(weekday_column);
        columns.push_back(shift_begin_column);
        columns.push_back(shift_end_column);
        columns.push_back(calendar_id_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, weekday_column->size(),
                                              &res_struct_col);
        EXPECT_EQ(
                "[{weekday:['MONDAY','FRIDAY','TUESDAY','THURSDAY'],shift_begin:[0,0,-1,900000],shift_end:[86400000,86400000,83700000,-1],calendar_id:['DE','US','DE','US'],is_calendar_id_null:[0,0,0,0]}]",
                res_struct_col->debug_string());

        // test finalize_to_column.
        auto res_array_col = ColumnHelper::create_column(type_array_char, false);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), res_array_col.get());
        EXPECT_EQ(1, res_array_col->size());
        EXPECT_EQ(1, res_array_col->get(0).get_array().size());
        auto json_string = to_calendar_json_string(res_array_col->get(0).get_array()[0].get_slice().to_string());
        ASSERT_TRUE(json_string.has_value());
        EXPECT_EQ(json_string.value(), R"({"multiWeekdayCalendar":{"calendars":[{"monday":{"useDay":true,"shift":{"begin":0,"end":86400000}},"calendarId":"DE"},{"friday":{"useDay":true,"shift":{"begin":0,"end":86400000}},"calendarId":"US"}]}})");
    }
}

TEST_F(CelonisAggregateTest, test_celonis_build_linear_regression_model) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            FunctionContext::TypeDesc{TYPE_ARRAY},
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_DOUBLE))};

    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    local_ctx->set_runtime_state(runtime_state.get());

    const AggregateFunction* agg_func = get_aggregate_function("celonis_build_linear_regression_model", TYPE_BIGINT,
                                                               TYPE_VARCHAR,
                                                               false);
    TypeDescriptor type_double;
    type_double.type = LogicalType::TYPE_DOUBLE;
    TypeDescriptor type_array_double;
    type_array_double.type = TYPE_ARRAY;
    type_array_double.children.resize(1);
    type_array_double.children[0].type = LogicalType::TYPE_DOUBLE;
    type_array_double.children[0].len = -1;
    TypeDescriptor type_struct;
    type_struct.type = LogicalType::TYPE_STRUCT;
    type_struct.children.emplace_back(celonis::array_type(TYPE_DOUBLE));
    type_struct.children.emplace_back(celonis::array_type(TYPE_DOUBLE));
    type_struct.field_names.emplace_back("x");
    type_struct.field_names.emplace_back("y");

    TypeDescriptor type_varchar;
    type_varchar.type = LogicalType::TYPE_VARCHAR;
    const double abs_error = 0.000001;

    auto state = ManagedAggrState::create(local_ctx.get(), agg_func);
    // No invalid rows
    {
        auto x_column = ColumnHelper::create_column(type_array_double, false);
        x_column->append_datum(DatumArray{1.0});
        x_column->append_datum(DatumArray{1.0});
        x_column->append_datum(DatumArray{2.0});
        x_column->append_datum(DatumArray{3.0});
        x_column->append_datum(DatumArray{4.0});

        auto y_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), false);
        y_column->append_datum(100.0);
        y_column->append_datum(300.0);
        y_column->append_datum(400.0);
        y_column->append_datum(300.0);
        y_column->append_datum(500.0);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = x_column.get();
        raw_columns[1] = y_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), x_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (LinearRegressionAggregateState*) (state->state());
        EXPECT_EQ(5, agg_state->x->size());
        EXPECT_EQ(5, agg_state->y->size());
        EXPECT_EQ("[[1], [1], [2], [3], [4]]", agg_state->x->debug_string());
        EXPECT_EQ("[100, 300, 400, 300, 500]", agg_state->y->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ("[{x:[1,1,2,3,4],y:[100,300,400,300,500]}]", res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(x_column);
        columns.push_back(y_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, x_column->size(),
                                              &res_struct_col);
        EXPECT_EQ("[{x:[1,1,2,3,4],y:[100,300,400,300,500]}]", res_struct_col->debug_string());

        // test finalize_to_column.
        auto varchar_col = ColumnHelper::create_column(type_varchar, true);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), varchar_col.get());
        ASSERT_EQ(1, varchar_col->size());
        const std::string model = varchar_col->get(0).get_slice().to_string();
        double intercept, slope;
        ASSERT_TRUE(parse_model(model, intercept, slope));
        EXPECT_NEAR(132.352941, intercept, abs_error);
        EXPECT_NEAR(85.294118, slope, abs_error);
    }
    // not enough input data
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto x_column = ColumnHelper::create_column(type_array_double, false);
        x_column->append_datum(DatumArray{1.0});

        auto y_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), false);
        y_column->append_datum(100.0);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = x_column.get();
        raw_columns[1] = y_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), x_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (LinearRegressionAggregateState*) (state->state());
        EXPECT_EQ(1, agg_state->x->size());
        EXPECT_EQ(1, agg_state->y->size());
        EXPECT_EQ("[[1]]", agg_state->x->debug_string());
        EXPECT_EQ("[100]", agg_state->y->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ("[{x:[1],y:[100]}]", res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(x_column);
        columns.push_back(y_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, x_column->size(),
                                              &res_struct_col);
        EXPECT_EQ("[{x:[1],y:[100]}]", res_struct_col->debug_string());

        // test finalize_to_column.
        auto varchar_col = ColumnHelper::create_column(type_varchar, true);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), varchar_col.get());
        ASSERT_EQ(1, varchar_col->size());
        EXPECT_TRUE(varchar_col->get(0).is_null());
    }
    // inconsistent length
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto x_column = ColumnHelper::create_column(type_array_double, false);
        x_column->append_datum(DatumArray{1.0});
        x_column->append_datum(DatumArray{1.0});
        x_column->append_datum(DatumArray{2.0});
        x_column->append_datum(DatumArray{3.0, 4.0});
        x_column->append_datum(DatumArray{4.0});

        auto y_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), false);
        y_column->append_datum(100.0);
        y_column->append_datum(300.0);
        y_column->append_datum(400.0);
        y_column->append_datum(300.0);
        y_column->append_datum(500.0);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = x_column.get();
        raw_columns[1] = y_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), x_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (LinearRegressionAggregateState*) (state->state());
        EXPECT_EQ(5, agg_state->x->size());
        EXPECT_EQ(5, agg_state->y->size());
        EXPECT_EQ("[[1], [1], [2], [3,4], [4]]", agg_state->x->debug_string());
        EXPECT_EQ("[100, 300, 400, 300, 500]", agg_state->y->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ("[{x:[1,1,2,3,4,4],y:[100,300,400,300,500]}]", res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(x_column);
        columns.push_back(y_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, x_column->size(),
                                              &res_struct_col);
        EXPECT_EQ("[{x:[1,1,2,3,4,4],y:[100,300,400,300,500]}]", res_struct_col->debug_string());

        // test finalize_to_column.
        auto varchar_col = ColumnHelper::create_column(type_varchar, true);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), varchar_col.get());
        ASSERT_EQ(1, varchar_col->size());
        EXPECT_TRUE(varchar_col->get(0).is_null());
    }
    // valid rows with different order
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        auto x_column = ColumnHelper::create_column(TypeDescriptor(type_array_double), false);
        x_column->append_datum(DatumArray{3.0});
        x_column->append_datum(DatumArray{4.0});
        x_column->append_datum(DatumArray{1.0});
        x_column->append_datum(DatumArray{1.0});
        x_column->append_datum(DatumArray{2.0});

        auto y_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), false);
        y_column->append_datum(300.0);
        y_column->append_datum(500.0);
        y_column->append_datum(100.0);
        y_column->append_datum(300.0);
        y_column->append_datum(400.0);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = x_column.get();
        raw_columns[1] = y_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), x_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (LinearRegressionAggregateState*) (state->state());
        EXPECT_EQ(5, agg_state->x->size());
        EXPECT_EQ(5, agg_state->y->size());
        EXPECT_EQ("[[3], [4], [1], [1], [2]]", agg_state->x->debug_string());
        EXPECT_EQ("[300, 500, 100, 300, 400]", agg_state->y->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ("[{x:[3,4,1,1,2],y:[300,500,100,300,400]}]", res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(x_column);
        columns.push_back(y_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, x_column->size(),
                                              &res_struct_col);
        EXPECT_EQ("[{x:[3,4,1,1,2],y:[300,500,100,300,400]}]", res_struct_col->debug_string());

        // test finalize_to_column.
        auto varchar_col = ColumnHelper::create_column(type_varchar, true);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), varchar_col.get());
        ASSERT_EQ(1, varchar_col->size());
        const std::string model = varchar_col->get(0).get_slice().to_string();
        double intercept, slope;
        ASSERT_TRUE(parse_model(model, intercept, slope));
        EXPECT_NEAR(132.352941, intercept, abs_error);
        EXPECT_NEAR(85.294118, slope, abs_error);
    }
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    // mixed valid and invalid rows
    {
        auto x_column = ColumnHelper::create_column(type_array_double, true);
        x_column->append_datum(kNullDatum);
        x_column->append_datum(DatumArray{kNullDatum});
        x_column->append_datum(DatumArray{3.0});
        x_column->append_datum(DatumArray{4.0});
        x_column->append_datum(DatumArray{kNullDatum});
        x_column->append_datum(DatumArray{1.0});
        x_column->append_datum(DatumArray{1.0});
        x_column->append_datum(DatumArray{2.0});
        x_column->append_datum(DatumArray{3.0});

        auto y_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), true);
        y_column->append_datum(100.0);
        y_column->append_datum(100.0);
        y_column->append_datum(300.0);
        y_column->append_datum(500.0);
        y_column->append_datum(kNullDatum);
        y_column->append_datum(100.0);
        y_column->append_datum(300.0);
        y_column->append_datum(400.0);
        y_column->append_datum(kNullDatum);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = x_column.get();
        raw_columns[1] = y_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), x_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (LinearRegressionAggregateState*) (state->state());
        EXPECT_EQ(5, agg_state->x->size());
        EXPECT_EQ(5, agg_state->y->size());
        EXPECT_EQ("[[3], [4], [1], [1], [2]]", agg_state->x->debug_string());
        EXPECT_EQ("[300, 500, 100, 300, 400]", agg_state->y->debug_string());

        // test serialize_to_column.
        auto res_struct_col = ColumnHelper::create_column(type_struct, true);
        agg_func->serialize_to_column(local_ctx.get(), state->state(), res_struct_col.get());
        EXPECT_EQ("[{x:[3,4,1,1,2],y:[300,500,100,300,400]}]", res_struct_col->debug_string());

        // test convert_to_serialize_format.
        res_struct_col->resize(0);
        std::vector<ColumnPtr> columns;
        columns.push_back(x_column);
        columns.push_back(y_column);
        agg_func->convert_to_serialize_format(local_ctx.get(), columns, x_column->size(),
                                              &res_struct_col);
        EXPECT_EQ("[{x:[3,4,1,1,2],y:[300,500,100,300,400]}]", res_struct_col->debug_string());

        // test finalize_to_column.
        auto varchar_col = ColumnHelper::create_column(type_varchar, true);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), varchar_col.get());
        ASSERT_EQ(1, varchar_col->size());
        const std::string model = varchar_col->get(0).get_slice().to_string();
        double intercept, slope;
        ASSERT_TRUE(parse_model(model, intercept, slope));
        EXPECT_NEAR(132.352941, intercept, abs_error);
        EXPECT_NEAR(85.294118, slope, abs_error);
    }
    // 2 features
    state = ManagedAggrState::create(local_ctx.get(), agg_func);
    {
        std::vector<double> x1s = {2.75, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.25, 2.25, 2.25, 2, 2, 2, 1.75, 1.75, 1.75,
                                   1.75, 1.75, 1.75, 1.75, 1.75, 1.75, 1.75, 1.75};
        std::vector<double> x2s = {5.3, 5.3, 5.3, 5.3, 5.4, 5.6, 5.5, 5.5, 5.5, 5.6, 5.7, 5.9, 6, 5.9, 5.8, 6.1, 6.2,
                                   6.1, 6.1, 6.1, 5.9, 6.2, 6.2, 6.1};
        std::vector<double> ys = {1464, 1394, 1357, 1293, 1256, 1254, 1234, 1195, 1159, 1167, 1130, 1075, 1047, 965,
                                  943, 958, 971, 949, 884, 866, 876, 822, 704, 719};
        auto x_column = ColumnHelper::create_column(type_array_double, false);
        auto y_column = ColumnHelper::create_column(TypeDescriptor(TYPE_DOUBLE), false);
        for (auto i = 0; i < 24; ++i) {
            x_column->append_datum(DatumArray{x1s[i], x2s[i]});
            y_column->append_datum(ys[i]);
        }

        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = x_column.get();
        raw_columns[1] = y_column.get();

        // test update
        agg_func->update_batch_single_state(local_ctx.get(), x_column->size(), raw_columns.data(),
                                            state->state());
        auto agg_state = (LinearRegressionAggregateState*) (state->state());
        EXPECT_EQ(24, agg_state->x->size());
        EXPECT_EQ(24, agg_state->y->size());

        // test finalize_to_column.
        auto varchar_col = ColumnHelper::create_column(type_varchar, true);
        agg_func->finalize_to_column(local_ctx.get(), state->state(), varchar_col.get());
        ASSERT_EQ(1, varchar_col->size());
        const std::string model = varchar_col->get(0).get_slice().to_string();
        double intercept;
        std::vector<double> coefficients;
        parse_model(model, intercept, coefficients);
        EXPECT_NEAR(1798.403978, intercept, abs_error);
        ASSERT_EQ(2, coefficients.size());
        EXPECT_NEAR(345.540087, coefficients[0], abs_error);
        EXPECT_NEAR(-250.146571, coefficients[1], abs_error);
    }
}

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

TEST_F(CelonisAggregateTest, test_multi_array_agg_single_agg_col) {
    const int32_t multi_array_agg_serialization_threshold = config::multi_array_agg_serialization_threshold;
    config::multi_array_agg_serialization_threshold = 10;
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT))};

    auto return_type = AnyValUtil::column_type_to_type_desc(logical_types_to_struct_type({TYPE_VARCHAR}));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    std::vector<bool> is_asc_order{false};
    std::vector<bool> nulls_first{true};
    local_ctx->set_is_asc_order(is_asc_order);
    local_ctx->set_nulls_first(nulls_first);
    local_ctx->set_runtime_state(runtime_state.get());

    const AggregateFunction* array_agg_func = get_aggregate_function("multi_array_agg", TYPE_BIGINT, TYPE_STRUCT,
                                                                     false);
    auto state = ManagedAggrState::create(local_ctx.get(), array_agg_func);

    // nullable columns input
    {
        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto char_column = ColumnHelper::create_column(char_type, true);
        char_column->append_datum(Datum());
        char_column->append_datum("bcd");
        char_column->append_datum("cdrdfe");
        char_column->append_datum(Datum());
        char_column->append_datum("esfg");

        auto int_type = TypeDescriptor::from_logical_type(LogicalType::TYPE_INT);
        auto int_column = ColumnHelper::create_column(int_type, true);
        int_column->append_datum(Datum());
        int_column->append_datum(9);
        int_column->append_datum(Datum());
        int_column->append_datum(7);
        int_column->append_datum(6);

        std::vector<const Column*> raw_columns;
        std::vector<ColumnPtr> columns;
        columns.push_back(char_column);
        columns.push_back(int_column);
        raw_columns.resize(2);
        raw_columns[0] = char_column.get();
        raw_columns[1] = int_column.get();

        // test update
        array_agg_func->update_batch_single_state(local_ctx.get(), int_column->size(), raw_columns.data(),
                                                  state->state());
        auto agg_state = (MultiArrayAggAggregateState*) (state->state());
        ASSERT_EQ(agg_state->data_columns.size(), 0);
        Columns data_columns;
        data_columns.reserve(2);
        for(auto i = 0; i < 2; ++i) {
            data_columns.emplace_back(local_ctx->create_column(*local_ctx->get_arg_type(i), true));
        }
        agg_state->deserialize_data(data_columns);
        EXPECT_EQ(data_columns[0]->debug_string(), char_column->debug_string());
        EXPECT_EQ(data_columns[1]->debug_string(), int_column->debug_string());

        TypeDescriptor type_array_char;
        type_array_char.type = LogicalType::TYPE_ARRAY;
        type_array_char.children.emplace_back(TypeDescriptor(LogicalType::TYPE_VARCHAR));

        TypeDescriptor type_array_int;
        type_array_int.type = LogicalType::TYPE_ARRAY;
        type_array_int.children.emplace_back(TypeDescriptor(LogicalType::TYPE_INT));

        TypeDescriptor type_struct_char_int;
        type_struct_char_int.type = LogicalType::TYPE_STRUCT;
        type_struct_char_int.children.emplace_back(type_array_char);
        type_struct_char_int.children.emplace_back(type_array_int);
        type_struct_char_int.field_names.emplace_back("vchar");
        type_struct_char_int.field_names.emplace_back("int");
        auto serialized_col = ColumnHelper::create_column(type_struct_char_int, true);
        array_agg_func->serialize_to_column(local_ctx.get(), state->state(), serialized_col.get());
        EXPECT_EQ(strcmp(serialized_col->debug_string().c_str(),
                         "[{vchar:[NULL,'bcd','cdrdfe',NULL,'esfg'],int:[NULL,9,NULL,7,6]}]"), 0);

        state = ManagedAggrState::create(local_ctx.get(), array_agg_func);
        array_agg_func->merge_batch_single_state(local_ctx.get(), state->state(), serialized_col.get(), 0,
                                                 serialized_col->size());

        serialized_col->resize(0);
        array_agg_func->convert_to_serialize_format(local_ctx.get(), columns, int_column->size(), &serialized_col);
        EXPECT_EQ(strcmp(serialized_col->debug_string().c_str(),
                         "[{vchar:[NULL],int:[NULL]}, {vchar:['bcd'],int:[9]}, {vchar:['cdrdfe'],int:[NULL]}, "
                         "{vchar:[NULL],int:[7]}, {vchar:['esfg'],int:[6]}]"),
                  0);

        auto res_col = ColumnHelper::create_column(logical_types_to_struct_type({TYPE_VARCHAR}), true);
        array_agg_func->finalize_to_column(local_ctx.get(), state->state(), res_col.get());
        EXPECT_EQ(strcmp(res_col->debug_string().c_str(), "[{col0:[NULL,'cdrdfe','bcd',NULL,'esfg']}]"), 0);
    }
    // nullable columns input with cancelled
    {
        auto state = ManagedAggrState::create(local_ctx.get(), array_agg_func);
        auto char_type = TypeDescriptor::create_varchar_type(30);
        auto char_column = ColumnHelper::create_column(char_type, true);
        char_column->append_datum(Datum());
        char_column->append_datum("bcd");
        char_column->append_datum("cdrdfe");
        char_column->append_datum(Datum());
        char_column->append_datum("esfg");

        auto int_type = TypeDescriptor::from_logical_type(LogicalType::TYPE_INT);
        auto int_column = ColumnHelper::create_column(int_type, true);
        int_column->append_datum(Datum());
        int_column->append_datum(9);
        int_column->append_datum(Datum());
        int_column->append_datum(7);
        int_column->append_datum(6);

        std::vector<const Column*> raw_columns;
        std::vector<ColumnPtr> columns;
        columns.push_back(char_column);
        columns.push_back(int_column);
        raw_columns.resize(2);
        raw_columns[0] = char_column.get();
        raw_columns[1] = int_column.get();

        // test update
        array_agg_func->update_batch_single_state(local_ctx.get(), int_column->size(), raw_columns.data(),
                                                  state->state());
        auto agg_state = (MultiArrayAggAggregateState*) (state->state());
        ASSERT_EQ(agg_state->data_columns.size(), 0);
        Columns data_columns;
        data_columns.reserve(2);
        for(auto i = 0; i < 2; ++i) {
            data_columns.emplace_back(local_ctx->create_column(*local_ctx->get_arg_type(i), true));
        }
        agg_state->deserialize_data(data_columns);
        EXPECT_EQ(data_columns[0]->debug_string(), char_column->debug_string());
        EXPECT_EQ(data_columns[1]->debug_string(), int_column->debug_string());

        TypeDescriptor type_array_char;
        type_array_char.type = LogicalType::TYPE_ARRAY;
        type_array_char.children.emplace_back(TypeDescriptor(LogicalType::TYPE_VARCHAR));

        TypeDescriptor type_array_int;
        type_array_int.type = LogicalType::TYPE_ARRAY;
        type_array_int.children.emplace_back(TypeDescriptor(LogicalType::TYPE_INT));

        TypeDescriptor type_struct_char_int;
        type_struct_char_int.type = LogicalType::TYPE_STRUCT;
        type_struct_char_int.children.emplace_back(type_array_char);
        type_struct_char_int.children.emplace_back(type_array_int);
        type_struct_char_int.field_names.emplace_back("vchar");
        type_struct_char_int.field_names.emplace_back("int");
        auto serialized_col = ColumnHelper::create_column(type_struct_char_int, true);
        array_agg_func->serialize_to_column(local_ctx.get(), state->state(), serialized_col.get());
        EXPECT_EQ(strcmp(serialized_col->debug_string().c_str(),
                         "[{vchar:[NULL,'bcd','cdrdfe',NULL,'esfg'],int:[NULL,9,NULL,7,6]}]"), 0);

        state = ManagedAggrState::create(local_ctx.get(), array_agg_func);
        array_agg_func->merge_batch_single_state(local_ctx.get(), state->state(), serialized_col.get(), 0,
                                                 serialized_col->size());
        serialized_col->resize(0);
        array_agg_func->convert_to_serialize_format(local_ctx.get(), columns, int_column->size(), &serialized_col);
        EXPECT_EQ(strcmp(serialized_col->debug_string().c_str(),
                         "[{vchar:[NULL],int:[NULL]}, {vchar:['bcd'],int:[9]}, {vchar:['cdrdfe'],int:[NULL]}, "
                         "{vchar:[NULL],int:[7]}, {vchar:['esfg'],int:[6]}]"), 0);

        auto res_col = ColumnHelper::create_column(logical_types_to_struct_type({TYPE_VARCHAR}), false);
        local_ctx->state()->set_is_cancelled(true);
        array_agg_func->finalize_to_column(local_ctx.get(), state->state(), res_col.get());
        ASSERT_TRUE(local_ctx->has_error());
    }
    config::multi_array_agg_serialization_threshold = multi_array_agg_serialization_threshold;
}

TEST_F(CelonisAggregateTest, test_multi_array_agg_multiple_agg_cols) {
    const int32_t multi_array_agg_serialization_threshold = config::multi_array_agg_serialization_threshold;
    config::multi_array_agg_serialization_threshold = 10;
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT))};

    auto return_type = AnyValUtil::column_type_to_type_desc(logical_types_to_struct_type({TYPE_VARCHAR, TYPE_VARCHAR}));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    std::vector<bool> is_asc_order{false};
    std::vector<bool> nulls_first{true};
    local_ctx->set_is_asc_order(is_asc_order);
    local_ctx->set_nulls_first(nulls_first);
    local_ctx->set_runtime_state(runtime_state.get());

    const AggregateFunction* array_agg_func = get_aggregate_function("multi_array_agg", TYPE_VARCHAR, TYPE_STRUCT,
                                                                     false);
    auto state = ManagedAggrState::create(local_ctx.get(), array_agg_func);

    auto char_type = TypeDescriptor::create_varchar_type(30);
    auto char_column_1 = ColumnHelper::create_column(char_type, true);
    char_column_1->append_datum(Datum());
    char_column_1->append_datum("bcd");
    char_column_1->append_datum("cdrdfe");
    char_column_1->append_datum(Datum());
    char_column_1->append_datum("esfg");

    auto char_column_2 = ColumnHelper::create_column(char_type, true);
    char_column_2->append_datum(Datum());
    char_column_2->append_datum("bcd2");
    char_column_2->append_datum("cdrdfe2");
    char_column_2->append_datum(Datum());
    char_column_2->append_datum("esfg2");

    auto int_type = TypeDescriptor::from_logical_type(LogicalType::TYPE_INT);
    auto int_column = ColumnHelper::create_column(int_type, true);
    int_column->append_datum(Datum());
    int_column->append_datum(9);
    int_column->append_datum(Datum());
    int_column->append_datum(7);
    int_column->append_datum(6);

    std::vector<const Column*> raw_columns;
    std::vector<ColumnPtr> columns;
    columns.push_back(char_column_1);
    columns.push_back(char_column_2);
    columns.push_back(int_column);
    raw_columns.resize(3);
    raw_columns[0] = char_column_1.get();
    raw_columns[1] = char_column_2.get();
    raw_columns[2] = int_column.get();

    // test update
    array_agg_func->update_batch_single_state(local_ctx.get(), int_column->size(), raw_columns.data(),
                                              state->state());
    auto agg_state = (MultiArrayAggAggregateState*) (state->state());
    ASSERT_EQ(agg_state->data_columns.size(), 0);
    Columns data_columns;
    data_columns.reserve(3);
    for(auto i = 0; i < 3; ++i) {
        data_columns.emplace_back(local_ctx->create_column(*local_ctx->get_arg_type(i), true));
    }
    agg_state->deserialize_data(data_columns);
    EXPECT_EQ(data_columns[0]->debug_string(), char_column_1->debug_string());
    EXPECT_EQ(data_columns[1]->debug_string(), char_column_2->debug_string());
    EXPECT_EQ(data_columns[2]->debug_string(), int_column->debug_string());

    TypeDescriptor type_array_char;
    type_array_char.type = LogicalType::TYPE_ARRAY;
    type_array_char.children.emplace_back(TypeDescriptor(LogicalType::TYPE_VARCHAR));

    TypeDescriptor type_array_int;
    type_array_int.type = LogicalType::TYPE_ARRAY;
    type_array_int.children.emplace_back(TypeDescriptor(LogicalType::TYPE_INT));

    TypeDescriptor type_struct_char_char_int;
    type_struct_char_char_int.type = LogicalType::TYPE_STRUCT;
    type_struct_char_char_int.children.emplace_back(type_array_char);
    type_struct_char_char_int.children.emplace_back(type_array_char);
    type_struct_char_char_int.children.emplace_back(type_array_int);
    type_struct_char_char_int.field_names.emplace_back("vchar1");
    type_struct_char_char_int.field_names.emplace_back("vchar2");
    type_struct_char_char_int.field_names.emplace_back("int");
    auto serialized_col = ColumnHelper::create_column(type_struct_char_char_int, true);
    array_agg_func->serialize_to_column(local_ctx.get(), state->state(), serialized_col.get());
    EXPECT_EQ(strcmp(serialized_col->debug_string().c_str(),
                     "[{vchar1:[NULL,'bcd','cdrdfe',NULL,'esfg'],"
                     "vchar2:[NULL,'bcd2','cdrdfe2',NULL,'esfg2'],int:[NULL,9,NULL,7,6]}]"), 0);

    state = ManagedAggrState::create(local_ctx.get(), array_agg_func);
    array_agg_func->merge_batch_single_state(local_ctx.get(), state->state(), serialized_col.get(), 0,
                                             serialized_col->size());

    serialized_col->resize(0);
    array_agg_func->convert_to_serialize_format(local_ctx.get(), columns, int_column->size(), &serialized_col);
    EXPECT_EQ(strcmp(serialized_col->debug_string().c_str(),
                     "[{vchar1:[NULL],vchar2:[NULL],int:[NULL]}, {vchar1:['bcd'],vchar2:['bcd2'],int:[9]}, "
                     "{vchar1:['cdrdfe'],vchar2:['cdrdfe2'],int:[NULL]}, {vchar1:[NULL],vchar2:[NULL],int:[7]}, "
                     "{vchar1:['esfg'],vchar2:['esfg2'],int:[6]}]"), 0);

    auto res_col = ColumnHelper::create_column(logical_types_to_struct_type({TYPE_VARCHAR, TYPE_VARCHAR}), true);
    array_agg_func->finalize_to_column(local_ctx.get(), state->state(), res_col.get());
    EXPECT_EQ(strcmp(res_col->debug_string().c_str(),
                     "[{col0:[NULL,'cdrdfe','bcd',NULL,'esfg'],col1:[NULL,'cdrdfe2','bcd2',NULL,'esfg2']}]"), 0);
    config::multi_array_agg_serialization_threshold = multi_array_agg_serialization_threshold;
}

TEST_F(CelonisAggregateTest, test_multi_array_agg_multiple_long_agg_cols) {
    const int32_t multi_array_agg_serialization_threshold = config::multi_array_agg_serialization_threshold;
    config::multi_array_agg_serialization_threshold = 10;
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT))};

    auto return_type = AnyValUtil::column_type_to_type_desc(logical_types_to_struct_type({TYPE_VARCHAR, TYPE_VARCHAR}));
    std::unique_ptr<RuntimeState> runtime_state = std::make_unique<RuntimeState>();
    std::unique_ptr<FunctionContext> local_ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));
    std::vector<bool> is_asc_order{false};
    std::vector<bool> nulls_first{true};
    local_ctx->set_is_asc_order(is_asc_order);
    local_ctx->set_nulls_first(nulls_first);
    local_ctx->set_runtime_state(runtime_state.get());

    const AggregateFunction* array_agg_func = get_aggregate_function("multi_array_agg", TYPE_VARCHAR, TYPE_STRUCT,
                                                                     false);
    auto state = ManagedAggrState::create(local_ctx.get(), array_agg_func);

    auto char_type = TypeDescriptor::create_varchar_type(30);
    auto char_column_1 = ColumnHelper::create_column(char_type, true);
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

    auto char_column_2 = ColumnHelper::create_column(char_type, true);
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
    auto int_column = ColumnHelper::create_column(int_type, true);
    int_column->append_datum(Datum());
    int_column->append_datum(1);
    int_column->append_datum(Datum());
    int_column->append_datum(2);
    int_column->append_datum(3);
    int_column->append_datum(Datum());
    int_column->append_datum(4);
    int_column->append_datum(Datum());
    int_column->append_datum(5);
    int_column->append_datum(6);
    int_column->append_datum(7);

    std::vector<const Column*> raw_columns;
    std::vector<ColumnPtr> columns;
    columns.push_back(char_column_1);
    columns.push_back(char_column_2);
    columns.push_back(int_column);
    raw_columns.resize(3);
    raw_columns[0] = char_column_1.get();
    raw_columns[1] = char_column_2.get();
    raw_columns[2] = int_column.get();

    // test update
    array_agg_func->update_batch_single_state(local_ctx.get(), int_column->size(), raw_columns.data(),
                                              state->state());
    auto agg_state = (MultiArrayAggAggregateState*) (state->state());
    ASSERT_EQ(agg_state->data_columns.size(), 3);
    // data_columns in state are nullable
    EXPECT_EQ((agg_state->data_columns)[0]->debug_string(), char_column_1->debug_string());
    EXPECT_EQ((agg_state->data_columns)[1]->debug_string(), char_column_2->debug_string());
    EXPECT_EQ((agg_state->data_columns)[2]->debug_string(), int_column->debug_string());

    TypeDescriptor type_array_char;
    type_array_char.type = LogicalType::TYPE_ARRAY;
    type_array_char.children.emplace_back(TypeDescriptor(LogicalType::TYPE_VARCHAR));

    TypeDescriptor type_array_int;
    type_array_int.type = LogicalType::TYPE_ARRAY;
    type_array_int.children.emplace_back(TypeDescriptor(LogicalType::TYPE_INT));

    TypeDescriptor type_struct_char_char_int;
    type_struct_char_char_int.type = LogicalType::TYPE_STRUCT;
    type_struct_char_char_int.children.emplace_back(type_array_char);
    type_struct_char_char_int.children.emplace_back(type_array_char);
    type_struct_char_char_int.children.emplace_back(type_array_int);
    type_struct_char_char_int.field_names.emplace_back("vchar1");
    type_struct_char_char_int.field_names.emplace_back("vchar2");
    type_struct_char_char_int.field_names.emplace_back("int");
    auto serialized_col = ColumnHelper::create_column(type_struct_char_char_int, true);
    array_agg_func->serialize_to_column(local_ctx.get(), state->state(), serialized_col.get());
    EXPECT_EQ(strcmp(serialized_col->debug_string().c_str(),
                     "[{vchar1:[NULL,'A','B',NULL,'C',NULL,'D','E',NULL,'F','G'],"
                     "vchar2:[NULL,'a','b',NULL,'c',NULL,'d','e',NULL,'f','g'],int:[NULL,1,NULL,2,3,NULL,4,NULL,5,6,7]}]"), 0);

    state = ManagedAggrState::create(local_ctx.get(), array_agg_func);
    array_agg_func->merge_batch_single_state(local_ctx.get(), state->state(), serialized_col.get(), 0,
                                             serialized_col->size());

    serialized_col->resize(0);
    array_agg_func->convert_to_serialize_format(local_ctx.get(), columns, int_column->size(), &serialized_col);
    EXPECT_EQ(strcmp(serialized_col->debug_string().c_str(),
                     "[{vchar1:[NULL],vchar2:[NULL],int:[NULL]}, {vchar1:['A'],vchar2:['a'],int:[1]}, {vchar1:['B'],vchar2:['b'],int:[NULL]}, {vchar1:[NULL],vchar2:[NULL],int:[2]}, {vchar1:['C'],vchar2:['c'],int:[3]}, {vchar1:[NULL],vchar2:[NULL],int:[NULL]}, {vchar1:['D'],vchar2:['d'],int:[4]}, {vchar1:['E'],vchar2:['e'],int:[NULL]}, {vchar1:[NULL],vchar2:[NULL],int:[5]}, {vchar1:['F'],vchar2:['f'],int:[6]}, {vchar1:['G'],vchar2:['g'],int:[7]}]"), 0);

    auto res_col = ColumnHelper::create_column(logical_types_to_struct_type({TYPE_VARCHAR, TYPE_VARCHAR}), true);
    array_agg_func->finalize_to_column(local_ctx.get(), state->state(), res_col.get());
    EXPECT_EQ(strcmp(res_col->debug_string().c_str(),
                     "[{col0:[NULL,'E','B',NULL,'G','F',NULL,'D','C',NULL,'A'],col1:[NULL,'e','b',NULL,'g','f',NULL,'d','c',NULL,'a']}]"), 0);
    config::multi_array_agg_serialization_threshold = multi_array_agg_serialization_threshold;
}

} // namespace starrocks