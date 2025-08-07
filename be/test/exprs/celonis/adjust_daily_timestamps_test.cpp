#include "exprs/celonis/adjust_daily_timestamps.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "column/array_column.h"
#include "column/struct_column.h"
#include "eventlog_utils.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"

namespace starrocks::celonis {

namespace {

struct ExpectedResultCase {
    DatumArray expected_timestamps{};
    DatumArray expected_reordered_positions{};
};

class ExpectedResultCaseBuilder {
public:
    ExpectedResultCaseBuilder& add_row(std::optional<TimestampValue> timestamp,
                                       std::optional<int64_t> reordered_position) {
        if (timestamp.has_value()) {
            expected_timestamps_.emplace_back(std::move(timestamp.value()));
        } else {
            expected_timestamps_.emplace_back(kNullDatum);
        }
        if (reordered_position.has_value()) {
            expected_reordered_positions_.emplace_back(std::move(reordered_position.value()));
        } else {
            expected_reordered_positions_.emplace_back(kNullDatum);
        }

        return *this;
    }

    ExpectedResultCase build() {
        return ExpectedResultCase{.expected_timestamps = std::move(expected_timestamps_),
                                  .expected_reordered_positions = std::move(expected_reordered_positions_)};
    }

private:
    DatumArray expected_timestamps_{};
    DatumArray expected_reordered_positions_{};
};

struct ExpectedResult {
    TestEvaluator<TYPE_DATETIME> expected_timestamps;
    TestEvaluator<TYPE_BIGINT> expected_reordered_positions;
};

class ExpectedResultBuilder {
public:
    ExpectedResultBuilder& add_case(ExpectedResultCase result_case) {
        expected_timestamps_.add_expected(std::move(result_case.expected_timestamps));
        expected_reordered_positions_.add_expected(std::move(result_case.expected_reordered_positions));
        return *this;
    }

    ExpectedResultBuilder& add_null_case() {
        expected_timestamps_.add_expected(kNullDatum);
        expected_reordered_positions_.add_expected(kNullDatum);
        return *this;
    };

    ExpectedResult build() {
        return ExpectedResult{.expected_timestamps = std::move(expected_timestamps_),
                              .expected_reordered_positions = std::move(expected_reordered_positions_)};
    }

private:
    TestEvaluator<TYPE_DATETIME> expected_timestamps_;
    TestEvaluator<TYPE_BIGINT> expected_reordered_positions_;
};

void validate_result(const ColumnPtr& result, const ExpectedResult& expected_result) {
    const StructColumn* struct_column = down_cast<const StructColumn*>(result.get());
    expected_result.expected_timestamps.evaluate(struct_column->fields()[0]);
    expected_result.expected_reordered_positions.evaluate(struct_column->fields()[1]);
}

class CelonisAdjustDailyTimestampsTest : public ::testing::Test {
public:
    enum class PassSortingColumn { YES, NO };

protected:
    CelonisAdjustDailyTimestampsTest()
            : arg_types_{AnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_DATETIME)),
                         AnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_BOOLEAN))},
              return_type_() {
        return_type_.type = TYPE_STRUCT;
        return_type_.children.push_back(AnyValUtil::column_type_to_type_desc(
                                         TypeDescriptor::from_logical_type(TYPE_DATETIME)));
        return_type_.children.push_back(AnyValUtil::column_type_to_type_desc(
                                         TypeDescriptor::from_logical_type(TYPE_BIGINT)));
        return_type_.field_names.push_back("adjusted_timestamps");
        return_type_.field_names.push_back("reordering");
    }

    void SetUp() override {}

    void TearDown() override {}

    void run_scenario(const EventlogScenario& scenario, const ExpectedResult& expected_result,
                      PassSortingColumn pass_sorting_column = PassSortingColumn::YES) {
        Columns columns;
        columns.push_back(scenario.timestamp_column);
        columns.push_back(scenario.is_day_based_column);

        if (pass_sorting_column == PassSortingColumn::YES) {
            arg_types_.emplace_back(AnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_BIGINT)));
            columns.emplace_back(scenario.sorting_column);
        }

        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types_), return_type_));
        const auto result = CelonisAdjustDailyTimestamps::celonis_adjust_daily_timestamps(ctx.get(), columns).value();
        result->check_or_die();
        validate_result(result, expected_result);
    }

private:
    std::vector<FunctionContext::TypeDesc> arg_types_;
    FunctionContext::TypeDesc return_type_;
};

TEST_F(CelonisAdjustDailyTimestampsTest, const_null_column) {
    {
        auto timestamp_column = ColumnHelper::create_const_null_column(2);
        auto is_day_based_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BOOLEAN), false);
        is_day_based_column->append_datum(false);
        is_day_based_column->append_datum(false);
        Columns columns;
        columns.push_back(timestamp_column);
        columns.push_back(is_day_based_column);
        const auto result = CelonisAdjustDailyTimestamps::celonis_adjust_daily_timestamps(nullptr, columns).value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
    {
        auto timestamp_column = ColumnHelper::create_const_null_column(2);
        auto is_day_based_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BOOLEAN), false);
        auto sorting_column = ColumnHelper::create_column(celonis::array_type(TYPE_BIGINT), false);
        is_day_based_column->append_datum(false);
        is_day_based_column->append_datum(false);
        sorting_column->append_datum(DatumArray{1L});
        sorting_column->append_datum(DatumArray{2L});
        Columns columns;
        columns.push_back(timestamp_column);
        columns.push_back(is_day_based_column);
        columns.push_back(sorting_column);
        const auto result = CelonisAdjustDailyTimestamps::celonis_adjust_daily_timestamps(nullptr, columns).value();
        ASSERT_EQ(2, result->size());
        EXPECT_TRUE(result->only_null());
        EXPECT_TRUE(result->is_constant());
    }
}

TEST_F(CelonisAdjustDailyTimestampsTest, adjust_daily_timestamps_day_based_activity_reordered_after_non_day_based) {
    auto activities = ActivitiesBuilder{}.add_non_day_based_activity("A", 1).add_day_based_activity("B", 2).build();
    auto case1 = CaseBuilder{activities}
                       .add_row("B", create_day_based_timestamp(1))
                       .add_row("B", create_day_based_timestamp(2))
                       .add_row("A", create_timestamp(2, 1))
                       .build();

    const auto scenario = EventlogScenarioBuilder{}.add_case(std::move(case1)).build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{}
                                       .add_row(create_timestamp(1, 0), 0)
                                       .add_row(create_timestamp(2, 1), 2)
                                       .add_row(create_timestamp(2, 1), 1)
                                       .build();
    const auto expected_result = ExpectedResultBuilder{}.add_case(std::move(expected_result_case1)).build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest,
       adjust_daily_timestamps_day_based_activity_not_reordered_before_non_day_based) {
    auto activities = ActivitiesBuilder{}.add_non_day_based_activity("A", 2).add_day_based_activity("B", 1).build();
    auto case1 = CaseBuilder{activities}
                       .add_row("B", create_day_based_timestamp(1))
                       .add_row("B", create_day_based_timestamp(2))
                       .add_row("A", create_timestamp(2, 1))
                       .build();

    const auto scenario = EventlogScenarioBuilder{}.add_case(std::move(case1)).build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{}
                                       .add_row(create_timestamp(1, 0), 0)
                                       .add_row(create_timestamp(2, 1), 1)
                                       .add_row(create_timestamp(2, 1), 2)
                                       .build();
    const auto expected_result = ExpectedResultBuilder{}.add_case(std::move(expected_result_case1)).build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest, adjust_daily_timestamps_two_day_based_activities_reordered) {
    auto activities = ActivitiesBuilder{}.add_day_based_activity("A", 2).add_day_based_activity("B", 1).build();
    auto case1 = CaseBuilder{activities}
                       .add_row("A", create_day_based_timestamp(1))
                       .add_row("B", create_day_based_timestamp(1))
                       .add_row("B", create_day_based_timestamp(2))
                       .add_row("A", create_day_based_timestamp(2))
                       .build();

    const auto scenario = EventlogScenarioBuilder{}.add_case(std::move(case1)).build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{}
                                       .add_row(create_day_based_timestamp(1), 1)
                                       .add_row(create_day_based_timestamp(1), 0)
                                       .add_row(create_day_based_timestamp(2), 2)
                                       .add_row(create_day_based_timestamp(2), 3)
                                       .build();
    const auto expected_result = ExpectedResultBuilder{}.add_case(std::move(expected_result_case1)).build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest, adjust_daily_timestamps_simple_example_with_multiple_cases) {
    auto activities = ActivitiesBuilder{}.add_day_based_activity("A", 2).add_non_day_based_activity("B", 1).build();
    auto case1 = CaseBuilder{activities}.add_row("A", create_day_based_timestamp(1)).build();
    auto case2 = CaseBuilder{activities}.add_row("B", create_timestamp(1, 1)).build();
    auto case3 = CaseBuilder{activities}
                       .add_row("A", create_day_based_timestamp(1))
                       .add_row("B", create_timestamp(1, 1))
                       .build();

    const auto scenario = EventlogScenarioBuilder{}
                                .add_case(std::move(case1))
                                .add_case(std::move(case2))
                                .add_case(std::move(case3))
                                .build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{}.add_row(create_day_based_timestamp(1), 0).build();
    auto expected_result_case2 = ExpectedResultCaseBuilder{}.add_row(create_timestamp(1, 1), 0).build();
    auto expected_result_case3 =
            ExpectedResultCaseBuilder{}.add_row(create_timestamp(1, 1), 1).add_row(create_timestamp(1, 1), 0).build();
    const auto expected_result = ExpectedResultBuilder{}
                                       .add_case(std::move(expected_result_case1))
                                       .add_case(std::move(expected_result_case2))
                                       .add_case(std::move(expected_result_case3))
                                       .build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest, adjust_daily_timestamps_multiple_day_based_and_non_day_based) {
    auto activities = ActivitiesBuilder{}
                            .add_non_day_based_activity("A", 1)
                            .add_day_based_activity("B", 2)
                            .add_non_day_based_activity("C", 3)
                            .add_day_based_activity("D", 4)
                            .build();
    auto case1 = CaseBuilder{activities} // day 1
                       .add_row("A", create_timestamp(1, 1))
                       .add_row("C", create_timestamp(1, 2))
                       .add_row("D", create_day_based_timestamp(1))
                       // day 2
                       .add_row("B", create_day_based_timestamp(2))
                       .add_row("B", create_day_based_timestamp(2))
                       .add_row("D", create_day_based_timestamp(2))
                       .add_row("A", create_timestamp(2, 3))
                       .add_row("C", create_timestamp(2, 4))
                       .add_row("C", create_timestamp(2, 5))
                       .add_row("A", create_timestamp(2, 6))
                       // day 3
                       .add_row("D", create_day_based_timestamp(3))
                       .add_row("B", create_day_based_timestamp(3))
                       .add_row("C", create_timestamp(3, 1))
                       .build();

    const auto scenario = EventlogScenarioBuilder{}.add_case(std::move(case1)).build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{} // day 1
                                       .add_row(create_timestamp(1, 1), 0)
                                       .add_row(create_timestamp(1, 2), 1)
                                       .add_row(create_timestamp(1, 2), 2)
                                       // day 2
                                       .add_row(create_timestamp(2, 3), 4)
                                       .add_row(create_timestamp(2, 3), 5)
                                       .add_row(create_timestamp(2, 6), 9)
                                       .add_row(create_timestamp(2, 3), 3)
                                       .add_row(create_timestamp(2, 4), 6)
                                       .add_row(create_timestamp(2, 5), 7)
                                       .add_row(create_timestamp(2, 6), 8)
                                       // day 3
                                       .add_row(create_timestamp(3, 1), 12)
                                       .add_row(create_timestamp(3, 1), 10)
                                       .add_row(create_timestamp(3, 1), 11)
                                       .build();
    const auto expected_result = ExpectedResultBuilder{}.add_case(std::move(expected_result_case1)).build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest, adjust_daily_timestamps_multiple_identical_with_two_day_based) {
    auto activities = ActivitiesBuilder{}
                            .add_non_day_based_activity("A", 1)
                            .add_day_based_activity("B", 2)
                            .add_non_day_based_activity("C", 3)
                            .add_day_based_activity("D", 4)
                            .build();
    auto case1 = CaseBuilder{activities}
                       .add_row("B", create_day_based_timestamp(1))
                       .add_row("D", create_day_based_timestamp(1))
                       .add_row("A", create_timestamp(1, 1))
                       .add_row("C", create_timestamp(1, 2))
                       .add_row("C", create_timestamp(1, 3))
                       .add_row("A", create_timestamp(1, 4))
                       .build();
    auto case2 = case1;
    auto case3 = case1;

    const auto scenario = EventlogScenarioBuilder{}
                                .add_case(std::move(case1))
                                .add_case(std::move(case2))
                                .add_case(std::move(case3))
                                .build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{}
                                       .add_row(create_timestamp(1, 1), 1)
                                       .add_row(create_timestamp(1, 4), 5)
                                       .add_row(create_timestamp(1, 1), 0)
                                       .add_row(create_timestamp(1, 2), 2)
                                       .add_row(create_timestamp(1, 3), 3)
                                       .add_row(create_timestamp(1, 4), 4)
                                       .build();
    auto expected_result_case2 = expected_result_case1;
    auto expected_result_case3 = expected_result_case1;

    const auto expected_result = ExpectedResultBuilder{}
                                       .add_case(std::move(expected_result_case1))
                                       .add_case(std::move(expected_result_case2))
                                       .add_case(std::move(expected_result_case3))
                                       .build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest,
       adjust_daily_timestamps_day_based_not_written_to_null_if_other_timestamps_exist) {
    auto activities = ActivitiesBuilder{}
                            .add_non_day_based_activity("A", 1)
                            .add_day_based_activity("B", 2)
                            .add_non_day_based_activity("C", 3)
                            .build();
    auto case1 = CaseBuilder{activities}
                       .add_null_timestamp("A")
                       .add_null_timestamp("A")
                       .add_null_timestamp("C")
                       .add_row("B", create_day_based_timestamp(1))
                       .add_row("A", create_timestamp(1, 1))
                       .add_row("C", create_timestamp(1, 2))
                       .build();

    const auto scenario = EventlogScenarioBuilder{}.add_case(std::move(case1)).build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{}
                                       .add_row({}, 0)
                                       .add_row({}, 1)
                                       .add_row({}, 2)
                                       .add_row(create_timestamp(1, 1), 4)
                                       .add_row(create_timestamp(1, 1), 3)
                                       .add_row(create_timestamp(1, 2), 5)
                                       .build();

    const auto expected_result = ExpectedResultBuilder{}.add_case(std::move(expected_result_case1)).build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest, adjust_daily_timestamps_null_day_based_activities_not_rewritten) {
    auto activities = ActivitiesBuilder{}.add_non_day_based_activity("A", 1).add_day_based_activity("B", 2).build();
    auto case1 = CaseBuilder{activities}.add_null_timestamp("B").add_row("A", create_timestamp(0, 0)).build();

    const auto scenario = EventlogScenarioBuilder{}.add_case(std::move(case1)).build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{}.add_row({}, 0).add_row(create_timestamp(0, 0), 1).build();

    const auto expected_result = ExpectedResultBuilder{}.add_case(std::move(expected_result_case1)).build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest, adjust_daily_timestamps_day_based_rows_with_missing_metadata_not_rewritten) {
    auto activities = ActivitiesBuilder{}.add_non_day_based_activity("A", 1).add_day_based_activity("B", 2).build();
    auto case1 = CaseBuilder{activities}
                       .add_row("B", create_day_based_timestamp(1))
                       .add_row_with_missing_metadata("B", create_day_based_timestamp(1))
                       .add_row("A", create_timestamp(1, 1))
                       .build();

    const auto scenario = EventlogScenarioBuilder{}.add_case(std::move(case1)).build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{}
                                       .add_row(create_timestamp(1, 1), 2)
                                       .add_row(create_day_based_timestamp(1), 0)
                                       .add_row(create_timestamp(1, 1), 1)
                                       .build();

    const auto expected_result = ExpectedResultBuilder{}.add_case(std::move(expected_result_case1)).build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest,
       adjust_daily_timestamps_day_based_rows_rewritten_to_non_day_based_rows_with_missing_metadata) {
    auto activities = ActivitiesBuilder{}.add_non_day_based_activity("A", 2).add_day_based_activity("B", 2).build();
    auto case1 = CaseBuilder{activities}
                       .add_row("B", create_day_based_timestamp(1))
                       .add_row_with_missing_metadata("A", create_timestamp(1, 2))
                       .add_row("A", create_timestamp(1, 2))
                       .build();

    const auto scenario = EventlogScenarioBuilder{}.add_case(std::move(case1)).build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{}
                                       .add_row(create_timestamp(1, 2), 1)
                                       .add_row(create_timestamp(1, 2), 0)
                                       .add_row(create_timestamp(1, 2), 2)
                                       .build();

    const auto expected_result = ExpectedResultBuilder{}.add_case(std::move(expected_result_case1)).build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest, adjust_daily_timestamps_handle_null_case) {
    auto activities = ActivitiesBuilder{}.add_non_day_based_activity("A", 1).add_day_based_activity("B", 2).build();
    auto case1 = CaseBuilder{activities}
                       .add_row("B", create_day_based_timestamp(1))
                       .add_row("A", create_timestamp(1, 1))
                       .build();

    const auto scenario = EventlogScenarioBuilder{}.add_null_case().add_case(std::move(case1)).add_null_case().build();

    auto expected_result_case1 =
            ExpectedResultCaseBuilder{}.add_row(create_timestamp(1, 1), 1).add_row(create_timestamp(1, 1), 0).build();

    const auto expected_result =
            ExpectedResultBuilder{}.add_null_case().add_case(std::move(expected_result_case1)).add_null_case().build();

    run_scenario(scenario, expected_result);
}

TEST_F(CelonisAdjustDailyTimestampsTest, adjust_daily_timestamps_no_reordering_without_sorting_column) {
    auto activities = ActivitiesBuilder{}.add_non_day_based_activity("A", 1).add_day_based_activity("B", 2).build();
    auto case1 = CaseBuilder{activities}
                       .add_row("B", create_day_based_timestamp(1))
                       .add_row("A", create_timestamp(1, 1))
                       .build();

    const auto scenario = EventlogScenarioBuilder{}.add_case(std::move(case1)).build();

    auto expected_result_case1 = ExpectedResultCaseBuilder{}
                                       .add_row(create_day_based_timestamp(1), 0)
                                       .add_row(create_timestamp(1, 1), 1)
                                       .build();
    const auto expected_result = ExpectedResultBuilder{}.add_case(std::move(expected_result_case1)).build();

    run_scenario(scenario, expected_result, PassSortingColumn::NO);
}

} // namespace

} // namespace starrocks::celonis