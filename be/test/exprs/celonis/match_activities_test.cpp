#include "exprs/celonis/match_activities.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

namespace starrocks {

class CelonisMatchActivitiesTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                TypeDescriptor::from_logical_type(TYPE_ARRAY), TypeDescriptor::from_logical_type(TYPE_ARRAY),
                TypeDescriptor::from_logical_type(TYPE_ARRAY), TypeDescriptor::from_logical_type(TYPE_ARRAY),
                TypeDescriptor::from_logical_type(TYPE_ARRAY), TypeDescriptor::from_logical_type(TYPE_ARRAY),
                TypeDescriptor::from_logical_type(TYPE_ARRAY)};
        auto return_type = TypeDescriptor::from_logical_type(TYPE_BIGINT);
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        activity_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
        // Array Literal is not wrapped with ConstColumn.
        starting_nodes_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        nodes_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        ending_nodes_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        excluding_nodes_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        excluding_all_nodes_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
        any_nodes_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), false);
    }

    void AddRow(const DatumArray& activity_array, const DatumArray& starting_nodes_array, const DatumArray& nodes_array,
                const DatumArray& ending_nodes_array, const DatumArray& excluding_nodes_array,
                const DatumArray& excluding_all_nodes_array, const DatumArray& any_nodes_array) {
        activity_column_->append_datum(activity_array);
        starting_nodes_column_->append_datum(starting_nodes_array);
        nodes_column_->append_datum(nodes_array);
        ending_nodes_column_->append_datum(ending_nodes_array);
        excluding_nodes_column_->append_datum(excluding_nodes_array);
        excluding_all_nodes_column_->append_datum(excluding_all_nodes_array);
        any_nodes_column_->append_datum(any_nodes_array);
    }

    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local(
                [this] { CelonisMatchActivitiesFunctions::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisMatchActivitiesFunctions::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local(
                [this] { CelonisMatchActivitiesFunctions::close(ctx_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisMatchActivitiesFunctions::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        result = CelonisMatchActivitiesFunctions::celonis_match_activities(
                ctx_.get(), {activity_column_, starting_nodes_column_, nodes_column_, ending_nodes_column_,
                             excluding_nodes_column_, excluding_all_nodes_column_, any_nodes_column_});
        return result;
    }

    StatusOr<ColumnPtr> RunConstantConfig(const DatumArray& starting_nodes_array, const DatumArray& nodes_array,
                                          const DatumArray& ending_nodes_array, const DatumArray& excluding_nodes_array,
                                          const DatumArray& excluding_all_nodes_array,
                                          const DatumArray& any_nodes_array) {
        starting_nodes_column_->append_datum(starting_nodes_array);
        nodes_column_->append_datum(nodes_array);
        ending_nodes_column_->append_datum(ending_nodes_array);
        excluding_nodes_column_->append_datum(excluding_nodes_array);
        excluding_all_nodes_column_->append_datum(excluding_all_nodes_array);
        any_nodes_column_->append_datum(any_nodes_array);
        const auto nrows = activity_column_->size();
        starting_nodes_column_ = ConstColumn::create(starting_nodes_column_, nrows);
        nodes_column_ = ConstColumn::create(nodes_column_, nrows);
        ending_nodes_column_ = ConstColumn::create(ending_nodes_column_, nrows);
        excluding_nodes_column_ = ConstColumn::create(excluding_nodes_column_, nrows);
        excluding_all_nodes_column_ = ConstColumn::create(excluding_all_nodes_column_, nrows);
        any_nodes_column_ = ConstColumn::create(any_nodes_column_, nrows);
        ctx_->set_constant_columns({nullptr, starting_nodes_column_, nodes_column_, ending_nodes_column_,
                                    excluding_nodes_column_, excluding_all_nodes_column_, any_nodes_column_});
        return Run();
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr activity_column_;
    ColumnPtr starting_nodes_column_;
    ColumnPtr nodes_column_;
    ColumnPtr ending_nodes_column_;
    ColumnPtr excluding_nodes_column_;
    ColumnPtr excluding_all_nodes_column_;
    ColumnPtr any_nodes_column_;
};

TEST_F(CelonisMatchActivitiesTest, const_null_activity_column) {
    {
        Prepare();
        activity_column_->append_datum(kNullDatum);
        activity_column_ = ConstColumn::create(activity_column_, 1);
        starting_nodes_column_->append_datum(DatumArray{});
        nodes_column_->append_datum(DatumArray{"string1", "string3"});
        ending_nodes_column_->append_datum(DatumArray{});
        excluding_nodes_column_->append_datum(DatumArray{});
        excluding_all_nodes_column_->append_datum(DatumArray{});
        any_nodes_column_->append_datum(DatumArray{});
        const auto result = Run().value();
        ASSERT_EQ(activity_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        activity_column_->append_datum(kNullDatum);
        activity_column_ = ConstColumn::create(activity_column_, 2);
        auto nodes_array = DatumArray{"string1", "string3"};
        auto empty_array = DatumArray{};
        const auto result =
                RunConstantConfig(empty_array, nodes_array, empty_array, empty_array, empty_array, empty_array).value();
        ASSERT_EQ(activity_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisMatchActivitiesTest, with_nodes) {
    Prepare();
    activity_column_->append_datum(DatumArray{"string1", "string2"});
    activity_column_->append_datum(DatumArray{Datum(), "string1", Datum(), "string2", "string3"});
    activity_column_->append_datum(DatumArray{"string3", "string4", "string1"});

    auto nodes_array = DatumArray{"string1", "string3"};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(empty_array, nodes_array, empty_array, empty_array, empty_array, empty_array).value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(1, result->get(1).get_int64());
    EXPECT_EQ(1, result->get(2).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_excluding_nodes) {
    Prepare();
    activity_column_->append_datum(DatumArray{"string1", "string2"});
    activity_column_->append_datum(DatumArray{Datum(), "string1", Datum(), "string2", "string3"});
    activity_column_->append_datum(DatumArray{"string3", "string4", "string1"});
    activity_column_->append_datum(DatumArray{"string1", "string3", "string2"});
    activity_column_->append_nulls(1);

    auto nodes_array = DatumArray{"string1", "string3"};
    auto excluding_nodes_array = DatumArray{"string2"};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(empty_array, nodes_array, empty_array, excluding_nodes_array, empty_array, empty_array)
                    .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(0, result->get(1).get_int64());
    EXPECT_EQ(1, result->get(2).get_int64());
    EXPECT_EQ(0, result->get(3).get_int64());
    EXPECT_TRUE(result->get(4).is_null());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_with_only_excluding_nodes) {
    Prepare();
    activity_column_->append_datum(DatumArray{"string1", "string2"});
    activity_column_->append_datum(DatumArray{"string3", "string4", "string1"});
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{"string3"});

    auto excluding_nodes_array = DatumArray{"string2"};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(empty_array, empty_array, empty_array, excluding_nodes_array, empty_array, empty_array)
                    .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(1, result->get(1).get_int64());
    EXPECT_EQ(0, result->get(2).get_int64());
    EXPECT_EQ(1, result->get(3).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_only_end_nodes) {
    Prepare();
    activity_column_->append_datum(DatumArray{});
    activity_column_->append_datum(DatumArray{"start1", "end2"});
    activity_column_->append_datum(DatumArray{"start1", "start2", "end1", kNullDatum});
    activity_column_->append_datum(DatumArray{"start2", "excluding_activity", "end1"});
    activity_column_->append_datum(DatumArray{"start1", "end2", "end3"});
    activity_column_->append_datum(DatumArray{"start1", "start2", "end4"});
    activity_column_->append_datum(DatumArray{"start1", "start2", "end4", kNullDatum});
    activity_column_->append_datum(DatumArray{});
    auto end_nodes_array = DatumArray{"end1", "end2"};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(empty_array, empty_array, end_nodes_array, empty_array, empty_array, empty_array).value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(1L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(0L, result->get(5).get_int64());
    EXPECT_EQ(0L, result->get(6).get_int64());
    EXPECT_EQ(0L, result->get(7).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_nodes_and_end_nodes) {
    Prepare();
    activity_column_->append_datum(DatumArray{"start1", "end2"});
    activity_column_->append_datum(DatumArray{"start1", "start2", "end1"});
    activity_column_->append_datum(DatumArray{"start2", "excluding_activity", "end1"});
    activity_column_->append_datum(DatumArray{"start1", "end2", "end3"});
    activity_column_->append_datum(DatumArray{"start1", "start2", "end2"});
    auto nodes_array = DatumArray{"start1", "end2"};
    auto end_nodes_array = DatumArray{"end1", "end2"};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(empty_array, nodes_array, end_nodes_array, empty_array, empty_array, empty_array).value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(1L, result->get(4).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_start_and_end_nodes) {
    Prepare();
    activity_column_->append_datum(DatumArray{"start1", "end2"});
    activity_column_->append_datum(DatumArray{"start2", "end1"});
    activity_column_->append_datum(DatumArray{"start2", "excluding_activity", "end1"});
    activity_column_->append_datum(DatumArray{"start2", "end1", "string1"});
    activity_column_->append_datum(DatumArray{"string1", "start2", "end1"});
    activity_column_->append_datum(DatumArray{kNullDatum, "start1", "end2", kNullDatum});
    activity_column_->append_datum(DatumArray{"start2", "end1", kNullDatum});

    auto start_nodes_array = DatumArray{"start1", "start2"};
    auto end_nodes_array = DatumArray{"end1", "end2"};
    auto excluding_nodes_array = DatumArray{"excluding_activity"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(start_nodes_array, empty_array, end_nodes_array, excluding_nodes_array,
                                          empty_array, empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, no_non_null_activities_with_start_nodes) {
    Prepare();
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, "start1"});
    auto start_nodes_array = DatumArray{"start1", "start2"};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(start_nodes_array, empty_array, empty_array, empty_array, empty_array, empty_array)
                    .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, short_variant_with_nodes) {
    Prepare();
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, kNullDatum, "node1", "node2", "node3", "node4"});
    auto nodes_array = DatumArray{"node1", "node2", "node3"};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(empty_array, nodes_array, empty_array, empty_array, empty_array, empty_array).value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, null_in_const_filters) {
    Prepare();
    activity_column_->append_datum(DatumArray{"start1", "end2"});
    activity_column_->append_datum(DatumArray{"start2", "end1"});
    activity_column_->append_datum(DatumArray{"start2", "excluding_activity", "end1"});
    activity_column_->append_datum(DatumArray{"start2", "end1", "string1"});
    activity_column_->append_datum(DatumArray{"string1", "start2", "end1"});
    activity_column_->append_datum(DatumArray{kNullDatum, "start1", "end2", kNullDatum});
    activity_column_->append_datum(DatumArray{"start2", "end1", kNullDatum});

    auto start_nodes_array = DatumArray{"start1", kNullDatum, "start2", kNullDatum};
    auto end_nodes_array = DatumArray{kNullDatum, "end1", "end2"};
    auto excluding_nodes_array = DatumArray{"excluding_activity", kNullDatum};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(start_nodes_array, empty_array, end_nodes_array, excluding_nodes_array,
                                          empty_array, empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_excluding_all_nodes) {
    Prepare();
    activity_column_->append_datum(DatumArray{"B", "A"});
    activity_column_->append_datum(DatumArray{"B", "D"});
    activity_column_->append_datum(DatumArray{"B", "C", "E"});
    activity_column_->append_datum(DatumArray{"A", "C"});
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{"C", kNullDatum, "A"});
    activity_column_->append_datum(DatumArray{kNullDatum, "A"});
    activity_column_->append_datum(DatumArray{});
    activity_column_->append_datum(DatumArray{"D"});

    auto excluding_all_nodes_array = DatumArray{"A", "C"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(empty_array, empty_array, empty_array, empty_array, excluding_all_nodes_array,
                                          empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(0L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_EQ(0L, result->get(7).get_int64());
    EXPECT_EQ(1L, result->get(8).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_nodes_any) {
    Prepare();
    activity_column_->append_datum(DatumArray{"B", "A"});
    activity_column_->append_datum(DatumArray{"B", "D"});
    activity_column_->append_datum(DatumArray{"B", "C", "E"});
    activity_column_->append_datum(DatumArray{"A", "C"});
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{"C", kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, "A"});
    activity_column_->append_datum(DatumArray{});

    auto any_nodes_array = DatumArray{"A", "C"};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(empty_array, empty_array, empty_array, empty_array, empty_array, any_nodes_array).value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(1L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(1L, result->get(5).get_int64());
    EXPECT_EQ(1L, result->get(6).get_int64());
    EXPECT_EQ(0L, result->get(7).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_empty_input) {
    Prepare();
    auto starting_nodes_array = DatumArray{"start1", "start2"};
    auto ending_nodes_array = DatumArray{"end1", "end2"};
    auto excluding_nodes_array = DatumArray{"excluding_activity"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(starting_nodes_array, empty_array, ending_nodes_array, excluding_nodes_array,
                                          empty_array, empty_array)
                                .value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisMatchActivitiesTest, non_const_filters) {
    Prepare();
    AddRow(DatumArray{"start1", "end2"}, DatumArray{"start1"}, DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{},
           DatumArray{});
    AddRow(DatumArray{"start2", "end1"}, DatumArray{"start2"}, DatumArray{}, DatumArray{"end1"}, DatumArray{},
           DatumArray{}, DatumArray{});
    AddRow(DatumArray{kNullDatum, "start1", "end2", kNullDatum}, DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{});
    AddRow(DatumArray{"start2", "end1", kNullDatum}, DatumArray{"start2"}, DatumArray{}, DatumArray{"end2"},
           DatumArray{}, DatumArray{}, DatumArray{});

    const auto result = Run().value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, null_in_non_const_filters) {
    Prepare();
    AddRow(DatumArray{"start1", "end2"}, DatumArray{kNullDatum, "start1", kNullDatum}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{}, DatumArray{});
    AddRow(DatumArray{"start2", "end1"}, DatumArray{kNullDatum, "start2"}, DatumArray{}, DatumArray{"end1"},
           DatumArray{}, DatumArray{}, DatumArray{});
    AddRow(DatumArray{kNullDatum, "start1", "end2", kNullDatum}, DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{});
    AddRow(DatumArray{"start2", "end1", kNullDatum}, DatumArray{"start2", kNullDatum}, DatumArray{},
           DatumArray{"end2", kNullDatum}, DatumArray{}, DatumArray{}, DatumArray{});

    const auto result = Run().value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, ban_non_const_config_works) {
    const bool fail_query_when_expensive_non_const_impl_is_called =
            config::fail_query_when_expensive_non_const_impl_is_called;
    config::fail_query_when_expensive_non_const_impl_is_called = true;
    Prepare();
    AddRow(DatumArray{"start1", "end2"}, DatumArray{kNullDatum, "start1", kNullDatum}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{}, DatumArray{});
    AddRow(DatumArray{"start2", "end1"}, DatumArray{kNullDatum, "start2"}, DatumArray{}, DatumArray{"end1"},
           DatumArray{}, DatumArray{}, DatumArray{});

    const auto result = Run();
    EXPECT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ("The non-const version of CELONIS_MATCH_ACTIVITIES should not be called.", result.status().message());
    config::fail_query_when_expensive_non_const_impl_is_called = fail_query_when_expensive_non_const_impl_is_called;
}

} // namespace starrocks
