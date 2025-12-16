#include "exprs/celonis/greatest_least.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <memory>
#include <vector>

#include "column/column_helper.h"
#include "exprs/celonis/anyval_util.h"
#include "exprs/function_context.h"

namespace starrocks {

class CelonisGreatestLeastTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    void Prepare(const std::vector<LogicalType>& types) {
        assert(!types.empty());
        std::vector<FunctionContext::TypeDesc> arg_types{};

        for (const LogicalType type : types) {
            arg_types.push_back(CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(type)));
            input_columns.push_back(ColumnHelper::create_column(TypeDescriptor(type), true));
        }

        auto return_type =
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(types.front()));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));
    }

    void Prepare(const LogicalType type, const std::size_t number_of_columns) {
        Prepare(std::vector<LogicalType>(number_of_columns, type));
    }

    void AddRow(const DatumArray& values) {
        assert(values.size() == input_columns.size());
        std::size_t col_idx{0};
        for (auto& column : input_columns) {
            column->append_datum(values.at(col_idx++));
        }
    }

    [[nodiscard]] StatusOr<ColumnPtr> RunGreatest() {
        return CelonisGreatestLeast::celonis_greatest(ctx_.get(), input_columns);
    }

    [[nodiscard]] StatusOr<ColumnPtr> RunLeast() {
        return CelonisGreatestLeast::celonis_least(ctx_.get(), input_columns);
    }

    std::unique_ptr<FunctionContext> ctx_;
    Columns input_columns;
};

template <typename T>
struct Expected {
    std::size_t size;
    std::vector<bool> nulls;
    std::vector<T> values;
};

template <typename T>
[[nodiscard]] Expected<T> make_expected(std::initializer_list<Datum> values) {
    Expected<T> ret;
    ret.size = values.size();
    for (const auto& datum : values) {
        const bool is_null{datum.is_null()};
        ret.nulls.push_back(is_null);
        ret.values.push_back(is_null ? T{} : datum.get<T>());
    }
    assert(ret.size == ret.nulls.size() && ret.size == ret.values.size());
    return ret;
}

template <typename T>
void validate(const ColumnPtr& result, const Expected<T>& expected) {
    const auto [expected_size, expected_nulls, expected_values] = expected;

    ASSERT_EQ(expected_size, result->size());
    for (std::size_t row_idx{0}; row_idx < expected_size; ++row_idx) {
        const bool expected_is_null{expected_nulls.at(row_idx)};
        const bool actual_is_null{result->get(row_idx).is_null()};
        EXPECT_EQ(expected_is_null, actual_is_null);
        if (!actual_is_null) {
            const auto& expected_value{expected_values.at(row_idx)};
            const auto actual_value{result->get(row_idx).get<T>()};
            EXPECT_EQ(expected_value, actual_value);
        }
    }
}

TEST_F(CelonisGreatestLeastTest, celonis_least_single_argument) {
    // GIVEN
    using T = std::int64_t;
    Prepare(TYPE_BIGINT, /* number_of_columns */ 1);

    AddRow({1L});
    AddRow({kNullDatum});

    const auto expected = make_expected<T>({1L, kNullDatum});

    // WHEN
    auto result_status = RunLeast();
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    // THEN
    validate(result, expected);
}

TEST_F(CelonisGreatestLeastTest, celonis_greatest_single_argument) {
    // GIVEN
    using T = double;
    Prepare(TYPE_DOUBLE, /* number_of_columns */ 1);

    AddRow({1.5});
    AddRow({kNullDatum});

    const auto expected = make_expected<T>({1.5, kNullDatum});

    // WHEN
    auto result_status = RunGreatest();
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    // THEN
    validate(result, expected);
}

TEST_F(CelonisGreatestLeastTest, celonis_greatest_int_data_mixed_nulls) {
    // GIVEN
    using T = std::int64_t;
    Prepare(TYPE_BIGINT, /* number_of_columns */ 3);

    AddRow({1L, 2L, 3L});
    AddRow({kNullDatum, 1L, 2L});
    AddRow({2L, kNullDatum, 1L});
    AddRow({3L, 2L, kNullDatum});
    AddRow({kNullDatum, kNullDatum, kNullDatum});
    AddRow({1L, 1L, 1L});

    const auto expected = make_expected<T>({3L, 2L, 2L, 3L, kNullDatum, 1L});

    // WHEN
    auto result_status = RunGreatest();
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    // THEN
    validate(result, expected);
}

TEST_F(CelonisGreatestLeastTest, celonis_greatest_double_data_mixed_nulls) {
    // GIVEN
    using T = double;
    Prepare(TYPE_DOUBLE, /* number_of_columns */ 3);

    AddRow({1.2, 2.9, 3.5});
    AddRow({kNullDatum, 1.2, 2.4});
    AddRow({2.0, kNullDatum, 1.0});
    AddRow({3.0, 2.0, kNullDatum});
    AddRow({kNullDatum, kNullDatum, kNullDatum});
    AddRow({1.0, 1.0, 1.0});

    const auto expected = make_expected<T>({3.5, 2.4, 2.0, 3.0, kNullDatum, 1.0});

    // WHEN
    auto result_status = RunGreatest();
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    // THEN
    validate(result, expected);
}

[[nodiscard]] inline auto operator""_ts(const unsigned long long seconds) -> TimestampValue {
    return TimestampValue::MIN_TIMESTAMP_VALUE.add<SECOND>(seconds);
}

TEST_F(CelonisGreatestLeastTest, celonis_greatest_datetime_data_mixed_nulls) {
    // GIVEN
    using T = TimestampValue;
    Prepare(TYPE_DATETIME, /* number_of_columns */ 3);

    AddRow({1_ts, 2_ts, 3_ts});
    AddRow({kNullDatum, 1_ts, 2_ts});
    AddRow({2_ts, kNullDatum, 1_ts});
    AddRow({3_ts, 2_ts, kNullDatum});
    AddRow({kNullDatum, kNullDatum, kNullDatum});
    AddRow({1_ts, 1_ts, 1_ts});

    const auto expected = make_expected<T>({3_ts, 2_ts, 2_ts, 3_ts, kNullDatum, 1_ts});

    // WHEN
    auto result_status = RunGreatest();
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    // THEN
    validate(result, expected);
}

TEST_F(CelonisGreatestLeastTest, celonis_greatest_varchar_data_mixed_nulls) {
    // GIVEN
    using T = Slice;
    Prepare(TYPE_VARCHAR, /* number_of_columns */ 3);

    AddRow({"1", "2", "3"});
    AddRow({kNullDatum, "1", "2"});
    AddRow({"2", kNullDatum, "1"});
    AddRow({"3", "2", kNullDatum});
    AddRow({kNullDatum, kNullDatum, kNullDatum});
    AddRow({"1", "1", "1"});

    const auto expected = make_expected<T>({"3", "2", "2", "3", kNullDatum, "1"});

    // WHEN
    auto result_status = RunGreatest();
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    // THEN
    validate(result, expected);
}

TEST_F(CelonisGreatestLeastTest, celonis_least_int_data_mixed_nulls) {
    // GIVEN
    using T = std::int64_t;
    Prepare(TYPE_BIGINT, /* number_of_columns */ 3);

    AddRow({1L, 2L, 3L});
    AddRow({kNullDatum, 1L, 2L});
    AddRow({2L, kNullDatum, 1L});
    AddRow({3L, 2L, kNullDatum});
    AddRow({kNullDatum, kNullDatum, kNullDatum});
    AddRow({1L, 1L, 1L});

    const auto expected = make_expected<T>({1L, 1L, 1L, 2L, kNullDatum, 1L});

    // WHEN
    auto result_status = RunLeast();
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    // THEN
    validate(result, expected);
}

TEST_F(CelonisGreatestLeastTest, celonis_least_double_data_mixed_nulls) {
    // GIVEN
    using T = double;
    Prepare(TYPE_DOUBLE, /* number_of_columns */ 3);

    AddRow({1.5, 2.0, 3.0});
    AddRow({kNullDatum, 1.2, 2.0});
    AddRow({2.0, kNullDatum, 1.0});
    AddRow({3.0, 2.0, kNullDatum});
    AddRow({kNullDatum, kNullDatum, kNullDatum});
    AddRow({1.0, 1.0, 1.0});

    const auto expected = make_expected<T>({1.5, 1.2, 1.0, 2.0, kNullDatum, 1.0});

    // WHEN
    auto result_status = RunLeast();
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    // THEN
    validate(result, expected);
}

TEST_F(CelonisGreatestLeastTest, celonis_least_datetime_data_mixed_nulls) {
    // GIVEN
    using T = TimestampValue;
    Prepare(TYPE_DATETIME, /* number_of_columns */ 3);

    AddRow({1_ts, 2_ts, 3_ts});
    AddRow({kNullDatum, 1_ts, 2_ts});
    AddRow({2_ts, kNullDatum, 1_ts});
    AddRow({3_ts, 2_ts, kNullDatum});
    AddRow({kNullDatum, kNullDatum, kNullDatum});
    AddRow({1_ts, 1_ts, 1_ts});

    const auto expected = make_expected<T>({1_ts, 1_ts, 1_ts, 2_ts, kNullDatum, 1_ts});

    // WHEN
    auto result_status = RunLeast();
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    // THEN
    validate(result, expected);
}

TEST_F(CelonisGreatestLeastTest, celonis_least_varchar_data_mixed_nulls) {
    // GIVEN
    using T = Slice;
    Prepare(TYPE_VARCHAR, /* number_of_columns */ 3);

    AddRow({"1", "2", "3"});
    AddRow({kNullDatum, "1", "2"});
    AddRow({"2", kNullDatum, "1"});
    AddRow({"3", "2", kNullDatum});
    AddRow({kNullDatum, kNullDatum, kNullDatum});
    AddRow({"1", "1", "1"});

    const auto expected = make_expected<T>({"1", "1", "1", "2", kNullDatum, "1"});

    // WHEN
    auto result_status = RunLeast();
    ASSERT_TRUE(result_status.ok());
    const auto result = result_status.value();

    // THEN
    validate(result, expected);
}

} // namespace starrocks
