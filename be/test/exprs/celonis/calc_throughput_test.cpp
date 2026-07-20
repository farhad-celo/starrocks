#include "exprs/celonis/calc_throughput.h"

#include "column/column_helper.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "types/logical_type.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

namespace {
TypeDescriptor array_type(const LogicalType& child_type) {
    TypeDescriptor t;
    t.type = TYPE_ARRAY;
    t.children.resize(1);
    t.children[0].type = child_type;
    t.children[0].len = child_type == TYPE_VARCHAR ? 10 : child_type == TYPE_CHAR ? 10 : -1;
    return t;
} // namespace

void create_const_params(ColumnPtr* start_activity, ColumnPtr* end_activity, ColumnPtr* start_label, ColumnPtr* end_label) {
    *start_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    (*start_activity)->append_datum(Slice("a"));

    *end_activity = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    (*end_activity)->append_datum(Slice("b"));

    *start_label = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    (*start_label)->append_datum(Slice("first"));

    *end_label = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    (*end_label)->append_datum(Slice("first"));
}
} // namespace

class CelonisCalcThroughputTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    void testOne(DatumArray activities, DatumArray timestamps, std::string start, std::string end, std::string start_label,
                 std::string end_label, bool nullable, Datum expected) {
        auto activity_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, nullable);
        activity_array->append_datum(activities);
        auto timestamp_array = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, nullable);
        timestamp_array->append_datum(timestamps);

        auto start_activity_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
        start_activity_col->append_datum(Slice(start));

        auto end_activity_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
        end_activity_col->append_datum(Slice(end));

        auto start_label_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
        start_label_col->append_datum(Slice(start_label));

        auto end_label_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
        end_label_col->append_datum(Slice(end_label));

        const auto result = CelonisCalcThroughputFunctions::celonis_calc_throughput(
                nullptr, {activity_array, timestamp_array, start_activity_col, end_activity_col, start_label_col, end_label_col}).value();
        ASSERT_EQ(1, result->size());
        if (expected.is_null()) {
            EXPECT_TRUE(result->is_null(0));
        } else {
            EXPECT_EQ(expected.get_int64(), result->get(0).get_int64());
        }
    }

    TypeDescriptor TYPE_ARRAY_BIGINT = array_type(TYPE_BIGINT);
    TypeDescriptor TYPE_ARRAY_VARCHAR = array_type(TYPE_VARCHAR);
};

TEST_F(CelonisCalcThroughputTest, FirstToFirst) {
    testOne(DatumArray{"a1", "a1", "a1", "a2", "a2", "a3", "a3"}, DatumArray{1L, 2L, 3L, 10L, 20L, 100L, 200L}, "a1",
            "a2", "first", "first", false, 9L);
}

TEST_F(CelonisCalcThroughputTest, FirstToFirstNullInput) {
    auto activity_array = ColumnHelper::create_const_null_column(1);
    auto timestamp_array = ColumnHelper::create_const_null_column(1);


    ColumnPtr start_activity_col, end_activity_col, start_label_col, end_label_col;
    create_const_params(&start_activity_col, &end_activity_col, &start_label_col, &end_label_col);
    const auto result = CelonisCalcThroughputFunctions::celonis_calc_throughput(
            nullptr, {activity_array, timestamp_array, start_activity_col, end_activity_col, start_label_col, end_label_col}).value();

    ASSERT_EQ(1, result->size());
    ASSERT_TRUE(result->get(0).is_null());
}

TEST_F(CelonisCalcThroughputTest, InputArrayIsSometimesNull) {
    auto activity_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, true);
    // Activity input:
    // row 0: NULL
    // row 1: [NULL, "a"]
    // row 2: ["a", "a", "a", "b", "b", "c"]
    // row 3: ["a", "b", NULL]
    activity_array->append_datum(Datum());
    activity_array->append_datum(DatumArray{Datum(), "a"});
    activity_array->append_datum(DatumArray{"a", "a", "a", "b", "b", "c"});
    activity_array->append_datum(DatumArray{"a", "b", Datum()});

    ColumnPtr start_activity_col, end_activity_col, start_label_col, end_label_col;
    create_const_params(&start_activity_col, &end_activity_col, &start_label_col, &end_label_col);

    auto timestamp_array = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, true);
    // Timestamp input:
    // row 0: [1]
    // row 1: NULL
    // row 2: [1, 2, 3, 10, 20, 100]
    // row 3: NULL
    timestamp_array->append_datum(DatumArray{1L});
    timestamp_array->append_datum(Datum());
    timestamp_array->append_datum(DatumArray{1L, 2L, 3L, 10L, 20L, 100L});
    timestamp_array->append_datum(Datum());

    // Only row 2 has non-NULL data in both activity and timestamp columns.
    const auto result = CelonisCalcThroughputFunctions::celonis_calc_throughput(
            nullptr, {activity_array, timestamp_array, start_activity_col, end_activity_col, start_label_col, end_label_col}).value();

    ASSERT_EQ(4, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_FALSE(result->get(2).is_null());
    EXPECT_EQ(18L, result->get(2).get_int64());
    EXPECT_TRUE(result->get(3).is_null());
}

TEST_F(CelonisCalcThroughputTest, FirstToFirstNullableArray) {
    testOne(DatumArray{"a1", "a1", "a1", "a2", "a2", "a3", "a3"}, DatumArray{1L, 2L, 3L, 10L, 20L, 100L, 200L}, "a1",
            "a2", "first", "first", true, 9L);
}

TEST_F(CelonisCalcThroughputTest, FirstToLast) {
    testOne(DatumArray{"a1", "a1", "a1", "a2", "a2", "a3", "a3"}, DatumArray{1L, 2L, 3L, 10L, 20L, 100L, 200L}, "a1",
            "a2", "first", "last", false, 19L);
}

TEST_F(CelonisCalcThroughputTest, LastToFirst) {
    testOne(DatumArray{"a1", "a1", "a1", "a2", "a2", "a3", "a3"}, DatumArray{1L, 2L, 3L, 10L, 20L, 100L, 200L}, "a1",
            "a2", "last", "first", false, 7L);
}

TEST_F(CelonisCalcThroughputTest, CaseStart) {
    testOne(DatumArray{"a1", "a1", "a1", "a2", "a2", "a3", "a3"}, DatumArray{1L, 2L, 3L, 10L, 20L, 100L, 200L}, "",
            "a2", "case_start", "first", false, 9L);
}

TEST_F(CelonisCalcThroughputTest, CaseEnd) {
    testOne(DatumArray{"a1", "a1", "a1", "a2", "a2", "a3", "a3"}, DatumArray{1L, 2L, 3L, 10L, 20L, 100L, 200L}, "a1",
            "", "last", "case_end", false, 197L);
}

TEST_F(CelonisCalcThroughputTest, LastToLast) {
    testOne(DatumArray{"a1", "a1", "a1", "a2", "a2", "a3", "a3"}, DatumArray{1L, 2L, 3L, 10L, 20L, 100L, 200L}, "a1",
            "a2", "last", "last", false, 17L);
}

TEST_F(CelonisCalcThroughputTest, MissingStart) {
    testOne(DatumArray{"a1", "a1", "a1", "a2", "a2", "a3", "a3"}, DatumArray{1L, 2L, 3L, 10L, 20L, 100L, 200L}, "a0",
            "a2", "first", "first", false, Datum());
}

TEST_F(CelonisCalcThroughputTest, MissingEnd) {
    testOne(DatumArray{"a1", "a1", "a1", "a2", "a2", "a3", "a3"}, DatumArray{1L, 2L, 3L, 10L, 20L, 100L, 200L}, "a0",
            "a9", "first", "first", false, Datum());
}

TEST_F(CelonisCalcThroughputTest, StartTimestampIsNull) {
    testOne(DatumArray{"a1", "a2", "a3"}, DatumArray{Datum(), 10L, 100L}, "a1", "a3", "first", "first", false, Datum());
}

TEST_F(CelonisCalcThroughputTest, EndTimestampIsNull) {
    testOne(DatumArray{"a1", "a2", "a3"}, DatumArray{1L, 10L, Datum()}, "a1", "a3", "first", "first", false, Datum());
}

TEST_F(CelonisCalcThroughputTest, ActivityContainsNull) {
    testOne(DatumArray{"a1", Datum(), "a3"}, DatumArray{1L, 10L, 100L}, "a1", "a3", "first", "first", false, 99L);
}

TEST_F(CelonisCalcThroughputTest, MultipleRows) {
    auto activity_array = ColumnHelper::create_column(TYPE_ARRAY_VARCHAR, false);
    activity_array->append_datum(DatumArray{"a1"});
    activity_array->append_datum(DatumArray{"a1", "a2"});
    activity_array->append_datum(DatumArray{"a1", "a2", "a3"});
    activity_array->append_datum(DatumArray{"a1", Datum(), "a2", "a3"});
    auto timestamp_array = ColumnHelper::create_column(TYPE_ARRAY_BIGINT, false);
    timestamp_array->append_datum(DatumArray{1L});
    timestamp_array->append_datum(DatumArray{1L, 10L});
    timestamp_array->append_datum(DatumArray{1L, Datum(), 100L});
    timestamp_array->append_datum(DatumArray{Datum(),  10L, 20L, 100L});

    auto start_activity_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    start_activity_col->append_datum(Slice("a1"));

    auto end_activity_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    end_activity_col->append_datum(Slice("a2"));

    auto start_label_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    start_label_col->append_datum(Slice("first"));

    auto end_label_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    end_label_col->append_datum(Slice("last"));

    const auto result = CelonisCalcThroughputFunctions::celonis_calc_throughput(
            nullptr, {activity_array, timestamp_array, start_activity_col, end_activity_col, start_label_col, end_label_col}).value();
    ASSERT_EQ(4, result->size());
    EXPECT_TRUE(result->get(0).is_null());
    EXPECT_EQ(9L, result->get(1).get_int64());
    EXPECT_TRUE(result->get(2).is_null());
    EXPECT_TRUE(result->get(3).is_null());
}

} // namespace starrocks
