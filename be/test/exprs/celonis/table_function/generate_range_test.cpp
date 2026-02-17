#include "exprs/celonis/table_function/generate_range.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "common/config.h"
#include "testutil/assert.h"

namespace starrocks {

class CelonisGenerateRangeTest : public ::testing::Test {
protected:
    struct TestCase {
        Datum step;
        Datum range_start;
        Datum range_end;
        DatumArray expected;
    };

    void SetUp() override {
        rt_state_ = std::make_unique<RuntimeState>();
        rt_state_->set_chunk_size(4096);
    }

    void TearDown() override {}

    template <LogicalType LT, LogicalType STEP_LT>
    std::tuple<TableFunctionState*, std::unique_ptr<TableFunction>> Prepare(
            const std::vector<TestCase>& test_cases,
            int64_t limit = static_cast<int64_t>(DEFAULT_GENERATED_ROWS_LIMIT)) {
        auto step = ColumnHelper::create_column(TypeDescriptor::from_logical_type(STEP_LT), true);
        auto range_start = ColumnHelper::create_column(TypeDescriptor::from_logical_type(LT), true);
        auto range_end = ColumnHelper::create_column(TypeDescriptor::from_logical_type(LT), true);
        auto limit_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_BIGINT), true);
        for (int i = 0; i < test_cases.size(); ++i) {
            step->append_datum(test_cases[i].step);
            range_start->append_datum(test_cases[i].range_start);
            range_end->append_datum(test_cases[i].range_end);
            limit_col->append_datum(static_cast<int64_t>(limit));
        }

        TableFunctionState* table_state;
        auto function = std::make_unique<CelonisGenerateRange<LT, STEP_LT>>();
        Columns input;
        input.push_back(step);
        input.push_back(range_start);
        input.push_back(range_end);
        input.push_back(limit_col);
        EXPECT_OK(function->init({}, &table_state));
        table_state->set_params(input);
        EXPECT_OK(function->prepare(table_state));

        return {table_state, std::move(function)};
    }

    template <LogicalType LT>
    void Evaluate(const ColumnPtr& result_column, const std::vector<TestCase>& test_cases) {
        auto result = ColumnViewer<LT>(result_column);

        int expected_rows = 0;
        for (int i = 0; i < test_cases.size(); ++i) {
            expected_rows += test_cases[i].expected.size();
        }
        ASSERT_EQ(result.size(), expected_rows);

        int row = 0;
        for (int i = 0; i < test_cases.size(); ++i) {
            for (int j = 0; j < test_cases[i].expected.size(); ++j) {
                ASSERT_FALSE(result.is_null(row));
                EXPECT_EQ(result.value(row++), test_cases[i].expected[j].get<RunTimeCppType<LT>>())
                        << "input_row: " << i << ", element_index : " << j;
            }
        }
    }

    template <LogicalType LT>
    void Evaluate(const ColumnPtr& result_column, const DatumArray& expected) {
        auto result = ColumnViewer<LT>(result_column);

        ASSERT_EQ(result.size(), expected.size());

        for (int row = 0; row < result.size(); ++row) {
            ASSERT_FALSE(result.is_null(row));
            EXPECT_EQ(result.value(row), expected[row].get<RunTimeCppType<LT>>()) << "row: " << row;
        }
    }

    template <LogicalType LT, LogicalType STEP_LT>
    void Run(const std::vector<TestCase>& test_cases) {
        auto [table_state, function] = Prepare<LT, STEP_LT>(test_cases);

        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(table_state->processed_rows(), test_cases.size());

        Evaluate<LT>(results[0], test_cases);

        function->close(nullptr, table_state);
    }

private:
    std::unique_ptr<RuntimeState> rt_state_;
};

TEST_F(CelonisGenerateRangeTest, bigint) {
    std::vector<TestCase> test_cases{{1L, 1L, 4L, {1L, 2L, 3L, 4L}},
                                     {2L, 1L, 10L, {1L, 3L, 5L, 7L, 9L}},
                                     {2L, -10L, -1L, {-10L, -8L, -6L, -4L, -2L}}};
    Run<TYPE_BIGINT, TYPE_BIGINT>(test_cases);
}

TEST_F(CelonisGenerateRangeTest, bigint_null_row) {
    std::vector<TestCase> test_cases{{1L, 1L, 4L, {1L, 2L, 3L, 4L}},
                                     {2L, kNullDatum, 10L, {}},
                                     {kNullDatum, 1L, 10L, {}},
                                     {2L, -10L, -1L, {-10L, -8L, -6L, -4L, -2L}}};
    Run<TYPE_BIGINT, TYPE_BIGINT>(test_cases);
}

TEST_F(CelonisGenerateRangeTest, bigint_chunks) {
    rt_state_->set_chunk_size(4);
    const auto LT = TYPE_BIGINT;
    std::vector<TestCase> test_cases{{3L, 3L, 14L, {}}, {1L, 1L, 9L, {}}, {2L, 0L, 2L, {}}};

    auto [table_state, function] = Prepare<LT, LT>(test_cases);

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(table_state->processed_rows(), 1); // input0 is processed.
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 4);
        Evaluate<LT>(results[0], {3L, 6L, 9L, 12L});
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(table_state->processed_rows(), 1); // input1 is being processed.
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 4);
        Evaluate<LT>(results[0], {1L, 2L, 3L, 4L});
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(table_state->processed_rows(), 1); // input1 is being processed.
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 4);
        Evaluate<LT>(results[0], {5L, 6L, 7L, 8L});
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(table_state->processed_rows(), 3); // input1 and input 2 are processed in this call.
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 1);
        EXPECT_EQ(offset->get(2).get_uint32(), 3);
        Evaluate<LT>(results[0], {9L, 0L, 2L});
    }

    function->close(nullptr, table_state);
}

TEST_F(CelonisGenerateRangeTest, bigint_invalid_step) {
    const auto LT = TYPE_BIGINT;
    std::vector<TestCase> test_cases{{0L, 3L, 4L, {}}};

    auto [table_state, function] = Prepare<LT, LT>(test_cases);
    auto [results, offset] = function->process(rt_state_.get(), table_state);

    EXPECT_EQ(table_state->processed_rows(), 0);
    EXPECT_EQ(results[0]->size(), 0);
    ASSERT_TRUE(table_state->status().is_invalid_argument());

    function->close(nullptr, table_state);
}

TEST_F(CelonisGenerateRangeTest, bigint_start_greater_than_end) {
    const auto LT = TYPE_BIGINT;
    std::vector<TestCase> test_cases{{1L, 3L, 0L, {}}};

    auto [table_state, function] = Prepare<LT, LT>(test_cases);
    auto [results, offset] = function->process(rt_state_.get(), table_state);

    EXPECT_EQ(table_state->processed_rows(), 1);
    EXPECT_EQ(results[0]->size(), 0);
    function->close(nullptr, table_state);
}

TEST_F(CelonisGenerateRangeTest, bigint_custom_limit) {
    const auto LT = TYPE_BIGINT;
    std::vector<TestCase> test_cases{{1L, 1L, 100L, {1L, 2L, 3L}}};

    auto [table_state, function] = Prepare<LT, LT>(test_cases, /*limit=*/3);
    auto [results, offset] = function->process(rt_state_.get(), table_state);

    EXPECT_EQ(results[0]->size(), 3);
    ASSERT_TRUE(table_state->status().is_invalid_argument());
    function->close(nullptr, table_state);
}

TEST_F(CelonisGenerateRangeTest, datetime_custom_limit) {
    const auto LT = TYPE_DATETIME;
    const auto STEP_LT = TYPE_VARCHAR;
    std::vector<TestCase> test_cases{
            {"1M", TimestampValue::create(2019, 1, 1, 0, 0, 0), TimestampValue::create(2025, 1, 1, 0, 0, 0), {}}};

    auto [table_state, function] = Prepare<LT, STEP_LT>(test_cases, /*limit=*/2);
    auto [results, offset] = function->process(rt_state_.get(), table_state);

    EXPECT_EQ(results[0]->size(), 2);
    ASSERT_TRUE(table_state->status().is_invalid_argument());
    function->close(nullptr, table_state);
}

TEST_F(CelonisGenerateRangeTest, bigint_negative_limit) {
    const auto LT = TYPE_BIGINT;
    std::vector<TestCase> test_cases{{1L, 1L, 10L, {}}};

    auto [table_state, function] = Prepare<LT, LT>(test_cases, /*limit=*/-1);
    auto [results, offset] = function->process(rt_state_.get(), table_state);

    EXPECT_EQ(results[0]->size(), 0);
    ASSERT_TRUE(table_state->status().is_invalid_argument());
    function->close(nullptr, table_state);
}

TEST_F(CelonisGenerateRangeTest, datetime) {
    std::vector<TestCase> test_cases{
            {"1M",
             TimestampValue::create(2019, 1, 1, 0, 0, 0),
             TimestampValue::create(2019, 6, 1, 0, 0, 0),
             {TimestampValue::create(2019, 1, 1, 0, 0, 0), TimestampValue::create(2019, 2, 1, 0, 0, 0),
              TimestampValue::create(2019, 3, 1, 0, 0, 0), TimestampValue::create(2019, 4, 1, 0, 0, 0),
              TimestampValue::create(2019, 5, 1, 0, 0, 0), TimestampValue::create(2019, 6, 1, 0, 0, 0)}}};
    Run<TYPE_DATETIME, TYPE_VARCHAR>(test_cases);
}

TEST_F(CelonisGenerateRangeTest, datetime_max_day) {
    std::vector<TestCase> test_cases{{"1M",
                                      TimestampValue::create(2019, 12, 31, 1, 2, 3),
                                      TimestampValue::create(2020, 7, 1, 0, 0, 0),
                                      {
                                              TimestampValue::create(2019, 12, 31, 1, 2, 3),
                                              TimestampValue::create(2020, 1, 31, 1, 2, 3),
                                              TimestampValue::create(2020, 2, 29, 1, 2, 3),
                                              TimestampValue::create(2020, 3, 31, 1, 2, 3),
                                              TimestampValue::create(2020, 4, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 5, 31, 1, 2, 3),
                                              TimestampValue::create(2020, 6, 30, 1, 2, 3),
                                      }},
                                     {"1Q",
                                      TimestampValue::create(2019, 2, 28, 1, 2, 3),
                                      TimestampValue::create(2020, 7, 1, 0, 0, 0),
                                      {
                                              TimestampValue::create(2019, 2, 28, 1, 2, 3),
                                              TimestampValue::create(2019, 5, 31, 1, 2, 3),
                                              TimestampValue::create(2019, 8, 31, 1, 2, 3),
                                              TimestampValue::create(2019, 11, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 2, 29, 1, 2, 3),
                                              TimestampValue::create(2020, 5, 31, 1, 2, 3),
                                      }},
                                     {"3M", // Same test case as 1Q
                                      TimestampValue::create(2019, 2, 28, 1, 2, 3),
                                      TimestampValue::create(2020, 7, 1, 0, 0, 0),
                                      {
                                              TimestampValue::create(2019, 2, 28, 1, 2, 3),
                                              TimestampValue::create(2019, 5, 31, 1, 2, 3),
                                              TimestampValue::create(2019, 8, 31, 1, 2, 3),
                                              TimestampValue::create(2019, 11, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 2, 29, 1, 2, 3),
                                              TimestampValue::create(2020, 5, 31, 1, 2, 3),
                                      }},
                                     {"2Y",
                                      TimestampValue::create(2018, 2, 28, 1, 2, 3),
                                      TimestampValue::create(2027, 7, 1, 0, 0, 0),
                                      {
                                              TimestampValue::create(2018, 2, 28, 1, 2, 3),
                                              TimestampValue::create(2020, 2, 29, 1, 2, 3),
                                              TimestampValue::create(2022, 2, 28, 1, 2, 3),
                                              TimestampValue::create(2024, 2, 29, 1, 2, 3),
                                              TimestampValue::create(2026, 2, 28, 1, 2, 3),
                                      }}};
    Run<TYPE_DATETIME, TYPE_VARCHAR>(test_cases);
}

TEST_F(CelonisGenerateRangeTest, datetime_non_max_day) {
    std::vector<TestCase> test_cases{{"1M",
                                      TimestampValue::create(2019, 12, 30, 1, 2, 3),
                                      TimestampValue::create(2021, 4, 1, 0, 0, 0),
                                      {
                                              TimestampValue::create(2019, 12, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 1, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 2, 29, 1, 2, 3),
                                              TimestampValue::create(2020, 3, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 4, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 5, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 6, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 7, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 8, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 9, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 10, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 11, 30, 1, 2, 3),
                                              TimestampValue::create(2020, 12, 30, 1, 2, 3),
                                              TimestampValue::create(2021, 1, 30, 1, 2, 3),
                                              TimestampValue::create(2021, 2, 28, 1, 2, 3),
                                              TimestampValue::create(2021, 3, 30, 1, 2, 3),
                                      }},
                                     {"1Q",
                                      TimestampValue::create(2018, 11, 29, 1, 2, 3),
                                      TimestampValue::create(2020, 7, 1, 0, 0, 0),
                                      {
                                              TimestampValue::create(2018, 11, 29, 1, 2, 3),
                                              TimestampValue::create(2019, 2, 28, 1, 2, 3),
                                              TimestampValue::create(2019, 5, 29, 1, 2, 3),
                                              TimestampValue::create(2019, 8, 29, 1, 2, 3),
                                              TimestampValue::create(2019, 11, 29, 1, 2, 3),
                                              TimestampValue::create(2020, 2, 29, 1, 2, 3),
                                              TimestampValue::create(2020, 5, 29, 1, 2, 3),
                                      }},
                                     {"2Y",
                                      TimestampValue::create(2020, 2, 28, 1, 2, 3),
                                      TimestampValue::create(2027, 7, 1, 0, 0, 0),
                                      {
                                              TimestampValue::create(2020, 2, 28, 1, 2, 3),
                                              TimestampValue::create(2022, 2, 28, 1, 2, 3),
                                              TimestampValue::create(2024, 2, 28, 1, 2, 3),
                                              TimestampValue::create(2026, 2, 28, 1, 2, 3),
                                      }}};
    Run<TYPE_DATETIME, TYPE_VARCHAR>(test_cases);
}

TEST_F(CelonisGenerateRangeTest, datetime_chunks) {
    rt_state_->set_chunk_size(4);
    const auto LT = TYPE_DATETIME;
    const auto STEP_LT = TYPE_VARCHAR;
    std::vector<TestCase> test_cases{
            {"1M", TimestampValue::create(2019, 12, 31, 1, 2, 3), TimestampValue::create(2020, 7, 1, 0, 0, 0), {}},
            {"1Q", TimestampValue::create(2018, 11, 29, 1, 2, 3), TimestampValue::create(2019, 12, 1, 0, 0, 0), {}},
            {"2Y", TimestampValue::create(2024, 1, 1, 0, 0, 0), TimestampValue::create(2028, 1, 1, 0, 0, 0), {}}};

    auto [table_state, function] = Prepare<LT, STEP_LT>(test_cases);

    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(table_state->processed_rows(), 0); // input0 is being processed.
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 4);

        DatumArray expected = {
                TimestampValue::create(2019, 12, 31, 1, 2, 3),
                TimestampValue::create(2020, 1, 31, 1, 2, 3),
                TimestampValue::create(2020, 2, 29, 1, 2, 3),
                TimestampValue::create(2020, 3, 31, 1, 2, 3),
        };
        Evaluate<LT>(results[0], expected);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(table_state->processed_rows(), 1); // input1 is being processed.
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 3); // end of input0 output
        EXPECT_EQ(offset->get(2).get_uint32(), 4);

        DatumArray expected = {
                TimestampValue::create(2020, 4, 30, 1, 2, 3),
                TimestampValue::create(2020, 5, 31, 1, 2, 3),
                TimestampValue::create(2020, 6, 30, 1, 2, 3),
                TimestampValue::create(2018, 11, 29, 1, 2, 3),
        };
        Evaluate<LT>(results[0], expected);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(table_state->processed_rows(), 2); // input1 is processed in this call.
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 4);
        DatumArray expected = {
                TimestampValue::create(2019, 2, 28, 1, 2, 3),
                TimestampValue::create(2019, 5, 29, 1, 2, 3),
                TimestampValue::create(2019, 8, 29, 1, 2, 3),
                TimestampValue::create(2019, 11, 29, 1, 2, 3),
        };
        Evaluate<LT>(results[0], expected);
    }
    {
        auto [results, offset] = function->process(rt_state_.get(), table_state);
        EXPECT_EQ(table_state->processed_rows(), 3); // input2 is processed in this call.
        EXPECT_EQ(offset->get(0).get_uint32(), 0);
        EXPECT_EQ(offset->get(1).get_uint32(), 3);
        DatumArray expected = {TimestampValue::create(2024, 1, 1, 0, 0, 0), TimestampValue::create(2026, 1, 1, 0, 0, 0),
                               TimestampValue::create(2028, 1, 1, 0, 0, 0)};
        Evaluate<LT>(results[0], expected);
    }

    function->close(nullptr, table_state);
}

} // namespace starrocks
