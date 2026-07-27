#include "exprs/celonis/match_activities.h"

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/celonis/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

namespace starrocks {

class CelonisMatchActivitiesTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

    // element_type is threaded into BOTH the FunctionContext arg descriptors and
    // the columns, so the function runs against the matching element type instead
    // of defaulting to the VARCHAR path.
    void Prepare(LogicalType element_type) {
        auto array_type_desc = CelonisAnyValUtil::column_type_to_type_desc(celonis::array_type(element_type));
        std::vector<FunctionContext::TypeDesc> arg_types(7, array_type_desc);
        auto return_type = CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        activity_column_ = ColumnHelper::create_column(celonis::array_type(element_type), true);
        // Array Literal is not wrapped with ConstColumn.
        starting_nodes_column_ = ColumnHelper::create_column(celonis::array_type(element_type), false);
        nodes_column_ = ColumnHelper::create_column(celonis::array_type(element_type), false);
        ending_nodes_column_ = ColumnHelper::create_column(celonis::array_type(element_type), false);
        excluding_nodes_column_ = ColumnHelper::create_column(celonis::array_type(element_type), false);
        excluding_all_nodes_column_ = ColumnHelper::create_column(celonis::array_type(element_type), false);
        any_nodes_column_ = ColumnHelper::create_column(celonis::array_type(element_type), false);
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

    template <LogicalType element_type>
    StatusOr<ColumnPtr> RunTyped() {
        DeferOp close_fragment_local([this] {
            CelonisMatchActivitiesFunctions<element_type>::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisMatchActivitiesFunctions<element_type>::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisMatchActivitiesFunctions<element_type>::close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(
                CelonisMatchActivitiesFunctions<element_type>::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        return CelonisMatchActivitiesFunctions<element_type>::celonis_match_activities(
                ctx_.get(), {activity_column_, starting_nodes_column_, nodes_column_, ending_nodes_column_,
                             excluding_nodes_column_, excluding_all_nodes_column_, any_nodes_column_});
    }

    StatusOr<ColumnPtr> Run(LogicalType element_type) {
        switch (element_type) {
        case TYPE_VARCHAR:
            return RunTyped<TYPE_VARCHAR>();
        case TYPE_INT:
            return RunTyped<TYPE_INT>();
        default:
            return Status::InternalError("unsupported element type in test harness");
        }
    }

    StatusOr<ColumnPtr> RunConstantConfig(LogicalType element_type, const DatumArray& starting_nodes_array,
                                          const DatumArray& nodes_array, const DatumArray& ending_nodes_array,
                                          const DatumArray& excluding_nodes_array,
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
        return Run(element_type);
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
        Prepare(TYPE_VARCHAR);
        activity_column_->append_datum(kNullDatum);
        activity_column_ = ConstColumn::create(activity_column_, 1);
        starting_nodes_column_->append_datum(DatumArray{});
        nodes_column_->append_datum(DatumArray{"string1", "string3"});
        ending_nodes_column_->append_datum(DatumArray{});
        excluding_nodes_column_->append_datum(DatumArray{});
        excluding_all_nodes_column_->append_datum(DatumArray{});
        any_nodes_column_->append_datum(DatumArray{});
        const auto result = Run(TYPE_VARCHAR).value();
        ASSERT_EQ(activity_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare(TYPE_VARCHAR);
        activity_column_->append_datum(kNullDatum);
        activity_column_ = ConstColumn::create(activity_column_, 2);
        auto nodes_array = DatumArray{"string1", "string3"};
        auto empty_array = DatumArray{};
        const auto result = RunConstantConfig(TYPE_VARCHAR, empty_array, nodes_array, empty_array, empty_array,
                                              empty_array, empty_array)
                                    .value();
        ASSERT_EQ(activity_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisMatchActivitiesTest, with_nodes) {
    Prepare(TYPE_VARCHAR);
    activity_column_->append_datum(DatumArray{"string1", "string2"});
    activity_column_->append_datum(DatumArray{Datum(), "string1", Datum(), "string2", "string3"});
    activity_column_->append_datum(DatumArray{"string3", "string4", "string1"});

    auto nodes_array = DatumArray{"string1", "string3"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_VARCHAR, empty_array, nodes_array, empty_array, empty_array, empty_array,
                                          empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(1, result->get(1).get_int64());
    EXPECT_EQ(1, result->get(2).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_duplicate_nodes) {
    Prepare(TYPE_VARCHAR);
    // nodes = {"A", "B"}
    // activity = {"A", "A"} -> Should return 0.
    activity_column_->append_datum(DatumArray{"A", "A"});

    auto nodes_array = DatumArray{"A", "B"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_VARCHAR, empty_array, nodes_array, empty_array, empty_array, empty_array,
                                          empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_excluding_nodes) {
    Prepare(TYPE_VARCHAR);
    activity_column_->append_datum(DatumArray{"string1", "string2"});
    activity_column_->append_datum(DatumArray{Datum(), "string1", Datum(), "string2", "string3"});
    activity_column_->append_datum(DatumArray{"string3", "string4", "string1"});
    activity_column_->append_datum(DatumArray{"string1", "string3", "string2"});
    activity_column_->append_nulls(1);

    auto nodes_array = DatumArray{"string1", "string3"};
    auto excluding_nodes_array = DatumArray{"string2"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_VARCHAR, empty_array, nodes_array, empty_array, excluding_nodes_array,
                                          empty_array, empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(0, result->get(1).get_int64());
    EXPECT_EQ(1, result->get(2).get_int64());
    EXPECT_EQ(0, result->get(3).get_int64());
    EXPECT_TRUE(result->get(4).is_null());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_with_only_excluding_nodes) {
    Prepare(TYPE_VARCHAR);
    activity_column_->append_datum(DatumArray{"string1", "string2"});
    activity_column_->append_datum(DatumArray{"string3", "string4", "string1"});
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{"string3"});

    auto excluding_nodes_array = DatumArray{"string2"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_VARCHAR, empty_array, empty_array, empty_array, excluding_nodes_array,
                                          empty_array, empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(1, result->get(1).get_int64());
    EXPECT_EQ(0, result->get(2).get_int64());
    EXPECT_EQ(1, result->get(3).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_only_end_nodes) {
    Prepare(TYPE_VARCHAR);
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
    const auto result = RunConstantConfig(TYPE_VARCHAR, empty_array, empty_array, end_nodes_array, empty_array,
                                          empty_array, empty_array)
                                .value();
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
    Prepare(TYPE_VARCHAR);
    activity_column_->append_datum(DatumArray{"start1", "end2"});
    activity_column_->append_datum(DatumArray{"start1", "start2", "end1"});
    activity_column_->append_datum(DatumArray{"start2", "excluding_activity", "end1"});
    activity_column_->append_datum(DatumArray{"start1", "end2", "end3"});
    activity_column_->append_datum(DatumArray{"start1", "start2", "end2"});
    auto nodes_array = DatumArray{"start1", "end2"};
    auto end_nodes_array = DatumArray{"end1", "end2"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_VARCHAR, empty_array, nodes_array, end_nodes_array, empty_array,
                                          empty_array, empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(1L, result->get(4).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_start_and_end_nodes) {
    Prepare(TYPE_VARCHAR);
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
    const auto result = RunConstantConfig(TYPE_VARCHAR, start_nodes_array, empty_array, end_nodes_array,
                                          excluding_nodes_array, empty_array, empty_array)
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
    Prepare(TYPE_VARCHAR);
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, "start1"});
    auto start_nodes_array = DatumArray{"start1", "start2"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_VARCHAR, start_nodes_array, empty_array, empty_array, empty_array,
                                          empty_array, empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, short_variant_with_nodes) {
    Prepare(TYPE_VARCHAR);
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, kNullDatum, "node1", "node2", "node3", "node4"});
    auto nodes_array = DatumArray{"node1", "node2", "node3"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_VARCHAR, empty_array, nodes_array, empty_array, empty_array, empty_array,
                                          empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, null_in_const_filters) {
    Prepare(TYPE_VARCHAR);
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
    const auto result = RunConstantConfig(TYPE_VARCHAR, start_nodes_array, empty_array, end_nodes_array,
                                          excluding_nodes_array, empty_array, empty_array)
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
    Prepare(TYPE_VARCHAR);
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
    const auto result = RunConstantConfig(TYPE_VARCHAR, empty_array, empty_array, empty_array, empty_array,
                                          excluding_all_nodes_array, empty_array)
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
    Prepare(TYPE_VARCHAR);
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
    const auto result = RunConstantConfig(TYPE_VARCHAR, empty_array, empty_array, empty_array, empty_array, empty_array,
                                          any_nodes_array)
                                .value();
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
    Prepare(TYPE_VARCHAR);
    auto starting_nodes_array = DatumArray{"start1", "start2"};
    auto ending_nodes_array = DatumArray{"end1", "end2"};
    auto excluding_nodes_array = DatumArray{"excluding_activity"};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_VARCHAR, starting_nodes_array, empty_array, ending_nodes_array,
                                          excluding_nodes_array, empty_array, empty_array)
                                .value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisMatchActivitiesTest, non_const_filters) {
    Prepare(TYPE_VARCHAR);
    AddRow(DatumArray{"start1", "end2"}, DatumArray{"start1"}, DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{},
           DatumArray{});
    AddRow(DatumArray{"start2", "end1"}, DatumArray{"start2"}, DatumArray{}, DatumArray{"end1"}, DatumArray{},
           DatumArray{}, DatumArray{});
    AddRow(DatumArray{kNullDatum, "start1", "end2", kNullDatum}, DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{});
    AddRow(DatumArray{"start2", "end1", kNullDatum}, DatumArray{"start2"}, DatumArray{}, DatumArray{"end2"},
           DatumArray{}, DatumArray{}, DatumArray{});

    const auto result = Run(TYPE_VARCHAR).value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, null_in_non_const_filters) {
    Prepare(TYPE_VARCHAR);
    AddRow(DatumArray{"start1", "end2"}, DatumArray{kNullDatum, "start1", kNullDatum}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{}, DatumArray{});
    AddRow(DatumArray{"start2", "end1"}, DatumArray{kNullDatum, "start2"}, DatumArray{}, DatumArray{"end1"},
           DatumArray{}, DatumArray{}, DatumArray{});
    AddRow(DatumArray{kNullDatum, "start1", "end2", kNullDatum}, DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{});
    AddRow(DatumArray{"start2", "end1", kNullDatum}, DatumArray{"start2", kNullDatum}, DatumArray{},
           DatumArray{"end2", kNullDatum}, DatumArray{}, DatumArray{}, DatumArray{});

    const auto result = Run(TYPE_VARCHAR).value();
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
    Prepare(TYPE_VARCHAR);
    AddRow(DatumArray{"start1", "end2"}, DatumArray{kNullDatum, "start1", kNullDatum}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{}, DatumArray{});
    AddRow(DatumArray{"start2", "end1"}, DatumArray{kNullDatum, "start2"}, DatumArray{}, DatumArray{"end1"},
           DatumArray{}, DatumArray{}, DatumArray{});

    const auto result = Run(TYPE_VARCHAR);
    EXPECT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ("The non-const version of CELONIS_MATCH_ACTIVITIES should not be called.", result.status().message());
    config::fail_query_when_expensive_non_const_impl_is_called = fail_query_when_expensive_non_const_impl_is_called;
}

// ---------------------------------------------------------------------------
// Integer-typed variants of the constant tests.
//
// The string labels above carry no meaning beyond equality, so each distinct
// label is mapped to a distinct integer and every expected result is unchanged.
// Non-obvious mappings are noted per test.
// ---------------------------------------------------------------------------

TEST_F(CelonisMatchActivitiesTest, const_null_activity_column_int) {
    // string1=1, string3=3
    {
        Prepare(TYPE_INT);
        activity_column_->append_datum(kNullDatum);
        activity_column_ = ConstColumn::create(activity_column_, 1);
        starting_nodes_column_->append_datum(DatumArray{});
        nodes_column_->append_datum(DatumArray{1, 3});
        ending_nodes_column_->append_datum(DatumArray{});
        excluding_nodes_column_->append_datum(DatumArray{});
        excluding_all_nodes_column_->append_datum(DatumArray{});
        any_nodes_column_->append_datum(DatumArray{});
        const auto result = Run(TYPE_INT).value();
        ASSERT_EQ(activity_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare(TYPE_INT);
        activity_column_->append_datum(kNullDatum);
        activity_column_ = ConstColumn::create(activity_column_, 2);
        auto nodes_array = DatumArray{1, 3};
        auto empty_array = DatumArray{};
        const auto result = RunConstantConfig(TYPE_INT, empty_array, nodes_array, empty_array, empty_array, empty_array,
                                              empty_array)
                                    .value();
        ASSERT_EQ(activity_column_->size(), result->size());
        EXPECT_TRUE(result->get(0).is_null());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisMatchActivitiesTest, with_nodes_int) {
    // string1=1, string2=2, string3=3, string4=4
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{1, 2});
    activity_column_->append_datum(DatumArray{Datum(), 1, Datum(), 2, 3});
    activity_column_->append_datum(DatumArray{3, 4, 1});

    auto nodes_array = DatumArray{1, 3};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(TYPE_INT, empty_array, nodes_array, empty_array, empty_array, empty_array, empty_array)
                    .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(1, result->get(1).get_int64());
    EXPECT_EQ(1, result->get(2).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_duplicate_nodes_int) {
    Prepare(TYPE_INT);
    // nodes = {1, 2}
    // activity = {1, 1} -> Should return 0.
    activity_column_->append_datum(DatumArray{1, 1});

    auto nodes_array = DatumArray{1, 2};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(TYPE_INT, empty_array, nodes_array, empty_array, empty_array, empty_array, empty_array)
                    .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_excluding_nodes_int) {
    // string1=1, string2=2, string3=3, string4=4
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{1, 2});
    activity_column_->append_datum(DatumArray{Datum(), 1, Datum(), 2, 3});
    activity_column_->append_datum(DatumArray{3, 4, 1});
    activity_column_->append_datum(DatumArray{1, 3, 2});
    activity_column_->append_nulls(1);

    auto nodes_array = DatumArray{1, 3};
    auto excluding_nodes_array = DatumArray{2};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_INT, empty_array, nodes_array, empty_array, excluding_nodes_array,
                                          empty_array, empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(0, result->get(1).get_int64());
    EXPECT_EQ(1, result->get(2).get_int64());
    EXPECT_EQ(0, result->get(3).get_int64());
    EXPECT_TRUE(result->get(4).is_null());
}

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_with_only_excluding_nodes_int) {
    // string1=1, string2=2, string3=3, string4=4
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{1, 2});
    activity_column_->append_datum(DatumArray{3, 4, 1});
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{3});

    auto excluding_nodes_array = DatumArray{2};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_INT, empty_array, empty_array, empty_array, excluding_nodes_array,
                                          empty_array, empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0, result->get(0).get_int64());
    EXPECT_EQ(1, result->get(1).get_int64());
    EXPECT_EQ(0, result->get(2).get_int64());
    EXPECT_EQ(1, result->get(3).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_only_end_nodes_int) {
    // start1=11, start2=12, end1=21, end2=22, end3=23, end4=24, excluding_activity=99
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{});
    activity_column_->append_datum(DatumArray{11, 22});
    activity_column_->append_datum(DatumArray{11, 12, 21, kNullDatum});
    activity_column_->append_datum(DatumArray{12, 99, 21});
    activity_column_->append_datum(DatumArray{11, 22, 23});
    activity_column_->append_datum(DatumArray{11, 12, 24});
    activity_column_->append_datum(DatumArray{11, 12, 24, kNullDatum});
    activity_column_->append_datum(DatumArray{});
    auto end_nodes_array = DatumArray{21, 22};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_INT, empty_array, empty_array, end_nodes_array, empty_array, empty_array,
                                          empty_array)
                                .value();
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

TEST_F(CelonisMatchActivitiesTest, with_nodes_and_end_nodes_int) {
    // start1=11, start2=12, end1=21, end2=22, end3=23, excluding_activity=99
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{11, 22});
    activity_column_->append_datum(DatumArray{11, 12, 21});
    activity_column_->append_datum(DatumArray{12, 99, 21});
    activity_column_->append_datum(DatumArray{11, 22, 23});
    activity_column_->append_datum(DatumArray{11, 12, 22});
    auto nodes_array = DatumArray{11, 22};
    auto end_nodes_array = DatumArray{21, 22};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_INT, empty_array, nodes_array, end_nodes_array, empty_array, empty_array,
                                          empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(0L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(1L, result->get(4).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, with_start_and_end_nodes_int) {
    // start1=11, start2=12, end1=21, end2=22, excluding_activity=99, string1=1
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{11, 22});
    activity_column_->append_datum(DatumArray{12, 21});
    activity_column_->append_datum(DatumArray{12, 99, 21});
    activity_column_->append_datum(DatumArray{12, 21, 1});
    activity_column_->append_datum(DatumArray{1, 12, 21});
    activity_column_->append_datum(DatumArray{kNullDatum, 11, 22, kNullDatum});
    activity_column_->append_datum(DatumArray{12, 21, kNullDatum});

    auto start_nodes_array = DatumArray{11, 12};
    auto end_nodes_array = DatumArray{21, 22};
    auto excluding_nodes_array = DatumArray{99};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_INT, start_nodes_array, empty_array, end_nodes_array,
                                          excluding_nodes_array, empty_array, empty_array)
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

TEST_F(CelonisMatchActivitiesTest, no_non_null_activities_with_start_nodes_int) {
    // start1=11, start2=12
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, 11});
    auto start_nodes_array = DatumArray{11, 12};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_INT, start_nodes_array, empty_array, empty_array, empty_array,
                                          empty_array, empty_array)
                                .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, short_variant_with_nodes_int) {
    // node1=31, node2=32, node3=33, node4=34
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, kNullDatum, 31, 32, 33, 34});
    auto nodes_array = DatumArray{31, 32, 33};
    auto empty_array = DatumArray{};
    const auto result =
            RunConstantConfig(TYPE_INT, empty_array, nodes_array, empty_array, empty_array, empty_array, empty_array)
                    .value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(0L, result->get(0).get_int64());
    EXPECT_EQ(0L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, null_in_const_filters_int) {
    // start1=11, start2=12, end1=21, end2=22, excluding_activity=99, string1=1
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{11, 22});
    activity_column_->append_datum(DatumArray{12, 21});
    activity_column_->append_datum(DatumArray{12, 99, 21});
    activity_column_->append_datum(DatumArray{12, 21, 1});
    activity_column_->append_datum(DatumArray{1, 12, 21});
    activity_column_->append_datum(DatumArray{kNullDatum, 11, 22, kNullDatum});
    activity_column_->append_datum(DatumArray{12, 21, kNullDatum});

    auto start_nodes_array = DatumArray{11, kNullDatum, 12, kNullDatum};
    auto end_nodes_array = DatumArray{kNullDatum, 21, 22};
    auto excluding_nodes_array = DatumArray{99, kNullDatum};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_INT, start_nodes_array, empty_array, end_nodes_array,
                                          excluding_nodes_array, empty_array, empty_array)
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

TEST_F(CelonisMatchActivitiesTest, with_excluding_all_nodes_int) {
    // A=1, B=2, C=3, D=4, E=5
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{2, 1});
    activity_column_->append_datum(DatumArray{2, 4});
    activity_column_->append_datum(DatumArray{2, 3, 5});
    activity_column_->append_datum(DatumArray{1, 3});
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{3, kNullDatum, 1});
    activity_column_->append_datum(DatumArray{kNullDatum, 1});
    activity_column_->append_datum(DatumArray{});
    activity_column_->append_datum(DatumArray{4});

    auto excluding_all_nodes_array = DatumArray{1, 3};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_INT, empty_array, empty_array, empty_array, empty_array,
                                          excluding_all_nodes_array, empty_array)
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

TEST_F(CelonisMatchActivitiesTest, with_nodes_any_int) {
    // A=1, B=2, C=3, D=4, E=5
    Prepare(TYPE_INT);
    activity_column_->append_datum(DatumArray{2, 1});
    activity_column_->append_datum(DatumArray{2, 4});
    activity_column_->append_datum(DatumArray{2, 3, 5});
    activity_column_->append_datum(DatumArray{1, 3});
    activity_column_->append_datum(DatumArray{kNullDatum});
    activity_column_->append_datum(DatumArray{3, kNullDatum});
    activity_column_->append_datum(DatumArray{kNullDatum, 1});
    activity_column_->append_datum(DatumArray{});

    auto any_nodes_array = DatumArray{1, 3};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_INT, empty_array, empty_array, empty_array, empty_array, empty_array,
                                          any_nodes_array)
                                .value();
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

TEST_F(CelonisMatchActivitiesTest, celonis_match_activities_empty_input_int) {
    // start1=11, start2=12, end1=21, end2=22, excluding_activity=99
    Prepare(TYPE_INT);
    auto starting_nodes_array = DatumArray{11, 12};
    auto ending_nodes_array = DatumArray{21, 22};
    auto excluding_nodes_array = DatumArray{99};
    auto empty_array = DatumArray{};
    const auto result = RunConstantConfig(TYPE_INT, starting_nodes_array, empty_array, ending_nodes_array,
                                          excluding_nodes_array, empty_array, empty_array)
                                .value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisMatchActivitiesTest, non_const_filters_int) {
    Prepare(TYPE_INT);
    AddRow(DatumArray{11, 22}, DatumArray{11}, DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{});
    AddRow(DatumArray{12, 21}, DatumArray{12}, DatumArray{}, DatumArray{21}, DatumArray{}, DatumArray{}, DatumArray{});
    AddRow(DatumArray{kNullDatum, 11, 22, kNullDatum}, DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{});
    AddRow(DatumArray{12, 21, kNullDatum}, DatumArray{12}, DatumArray{}, DatumArray{22}, DatumArray{}, DatumArray{},
           DatumArray{});

    const auto result = Run(TYPE_INT).value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, null_in_non_const_filters_int) {
    Prepare(TYPE_INT);
    AddRow(DatumArray{11, 22}, DatumArray{kNullDatum, 11, kNullDatum}, DatumArray{}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{});
    AddRow(DatumArray{12, 21}, DatumArray{kNullDatum, 12}, DatumArray{}, DatumArray{21}, DatumArray{}, DatumArray{},
           DatumArray{});
    AddRow(DatumArray{kNullDatum, 11, 22, kNullDatum}, DatumArray{}, DatumArray{}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{});
    AddRow(DatumArray{12, 21, kNullDatum}, DatumArray{12, kNullDatum}, DatumArray{}, DatumArray{22, kNullDatum},
           DatumArray{}, DatumArray{}, DatumArray{});

    const auto result = Run(TYPE_INT).value();
    ASSERT_EQ(activity_column_->size(), result->size());
    EXPECT_EQ(1L, result->get(0).get_int64());
    EXPECT_EQ(1L, result->get(1).get_int64());
    EXPECT_EQ(1L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
}

TEST_F(CelonisMatchActivitiesTest, ban_non_const_config_works_int) {
    const bool fail_query_when_expensive_non_const_impl_is_called =
            config::fail_query_when_expensive_non_const_impl_is_called;
    config::fail_query_when_expensive_non_const_impl_is_called = true;
    Prepare(TYPE_INT);
    AddRow(DatumArray{11, 22}, DatumArray{kNullDatum, 11, kNullDatum}, DatumArray{}, DatumArray{}, DatumArray{},
           DatumArray{}, DatumArray{});
    AddRow(DatumArray{12, 21}, DatumArray{kNullDatum, 12}, DatumArray{}, DatumArray{21}, DatumArray{}, DatumArray{},
           DatumArray{});

    const auto result = Run(TYPE_INT);
    EXPECT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ("The non-const version of CELONIS_MATCH_ACTIVITIES should not be called.", result.status().message());
    config::fail_query_when_expensive_non_const_impl_is_called = fail_query_when_expensive_non_const_impl_is_called;
}

} // namespace starrocks
