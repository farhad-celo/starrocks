#include "exprs/celonis/agg/top_k_utils.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace starrocks {
namespace celonis {
namespace top_k {
namespace {

static constexpr char const* ABC_STR = "abc";
static constexpr char const* HELLO_WORLD_STR = "Hello World";
static constexpr char const* LONG_STR = "This a very long string!!!!";
static constexpr char const* CELONIS_STR = "Celonis";
static constexpr char const* EMPTY_STR = "";
static constexpr char const* NUMERIC_STR = "12";

class TestRow : public TopKRowAccessor {
public:
    TestRow(RowIdxType row_idx, ColumnIdxType num_columns, std::vector<Datum> data)
            : TopKRowAccessor{num_columns, row_idx}, data_(std::move(data)) {}

    Datum get(ColumnIdxType idx) const override { return data_.at(idx); }

private:
    std::vector<Datum> data_;
};

class RowBuilder {
public:
    template <typename... Types>
    TestRow makeRow(Types&&... values) {
        std::vector<Datum> data;
        data.reserve(sizeof...(Types));
        (data.emplace_back(make_datum(std::forward<Types>(values))), ...);
        return TestRow{cur_row_idx_++, sizeof...(Types), std::move(data)};
    }

private:
    template <typename T>
    Datum make_datum(T value) {
        return Datum{std::move(value)};
    }

    RowIdxType cur_row_idx_{0};
    std::vector<std::string> strings_;
};

class AllTypeRowBuilder : public RowBuilder {
public:
    TestRow makeRow(int64_t integer, DateValue date, TimestampValue timestamp, Slice slice, DecimalV2Value decimal,
                    double floating_point) {
        return RowBuilder::makeRow<int64_t, DateValue, TimestampValue, Slice, DecimalV2Value, double>(
                std::move(integer), std::move(date), std::move(timestamp), std::move(slice), std::move(decimal),
                std::move(floating_point));
    }

    static std::vector<LogicalType> GET_TYPES() {
        return {TYPE_BIGINT, TYPE_DATE, TYPE_DATETIME, TYPE_VARCHAR, TYPE_DECIMALV2, TYPE_DOUBLE};
    }
};

static constexpr SortDescriptor ASC_NULLS_FIRST{.sort_order = SortOrder::ASC,
                                                .null_handling = NullHandling::NULLS_FIRST};
static constexpr SortDescriptor DESC_NULLS_FIRST{.sort_order = SortOrder::DESC,
                                                 .null_handling = NullHandling::NULLS_FIRST};
static constexpr SortDescriptor ASC_NULLS_LAST{.sort_order = SortOrder::ASC, .null_handling = NullHandling::NULLS_LAST};
static constexpr SortDescriptor DESC_NULLS_LAST{.sort_order = SortOrder::DESC,
                                                .null_handling = NullHandling::NULLS_LAST};

template <typename... Types>
struct RowSequence {
    RowSequence& addRow(Types&&... values) {
        rows.push_back(builder.makeRow<Types...>(std::forward<Types>(values)...));
        return *this;
    }

    RowBuilder builder{};
    std::vector<TestRow> rows{};
};

std::vector<RowIdxType> execute_for_sequence(TopKRowPriorityQueue& sorter, const auto& sequence) {
    for (const TestRow& row : sequence.rows) {
        sorter.try_push_row(row);
    }
    return sorter.get_ranking();
}

struct BasicIntTestConfig {
    std::array<SortDescriptor, 3> sort_descriptors;
    RowIdxType k;
    TopKRowPriorityQueue::RankingType expected_ranking;
};

class CelonisTopKRowPriorityQueueIntTestFixture : public ::testing::TestWithParam<BasicIntTestConfig> {
protected:
    void SetUp() override { config = GetParam(); }

    BasicIntTestConfig config;
};

TEST_P(CelonisTopKRowPriorityQueueIntTestFixture, CelonisTopKRowPriorityQueueIntTest) {
    std::vector<SorterMetadata> sorters;
    for (SortDescriptor sort_desc : config.sort_descriptors) {
        sorters.push_back(SorterMetadata{.logical_type = TYPE_BIGINT, .sort_descriptor = std::move(sort_desc)});
    }

    using BigIntCppType = RunTimeCppType<TYPE_BIGINT>;
    RowSequence<BigIntCppType, BigIntCppType, BigIntCppType> row_sequence{};
    row_sequence.addRow(3, 4, 5)
            .addRow(3, 5, 5)
            .addRow(3, 3, 6)
            .addRow(2, 4, 2)
            .addRow(1, 3, 5)
            .addRow(2, 5, 1)
            .addRow(4, 2, 6)
            .addRow(2, 4, 3);

    TopKRowPriorityQueue sorter{config.k, sorters};
    TopKRowPriorityQueue::RankingType actual_ranking{execute_for_sequence(sorter, row_sequence)};
    EXPECT_EQ(actual_ranking, config.expected_ranking);

    // Should be the same for reversed sequence
    std::ranges::reverse(row_sequence.rows);
    TopKRowPriorityQueue reverse_sorter{config.k, sorters};
    TopKRowPriorityQueue::RankingType reverse_actual_ranking{execute_for_sequence(reverse_sorter, row_sequence)};
    EXPECT_EQ(reverse_actual_ranking, config.expected_ranking);
}

INSTANTIATE_TEST_SUITE_P(
        CelonisTopKRowPriorityQueueIntTest, CelonisTopKRowPriorityQueueIntTestFixture,
        ::testing::Values(BasicIntTestConfig{.sort_descriptors = {ASC_NULLS_FIRST, ASC_NULLS_FIRST, ASC_NULLS_FIRST},
                                             .k = 1,
                                             .expected_ranking = {4}},
                          BasicIntTestConfig{.sort_descriptors = {ASC_NULLS_FIRST, ASC_NULLS_FIRST, ASC_NULLS_FIRST},
                                             .k = 3,
                                             .expected_ranking = {4, 3, 7}},
                          BasicIntTestConfig{.sort_descriptors = {ASC_NULLS_FIRST, ASC_NULLS_FIRST, ASC_NULLS_FIRST},
                                             .k = 5,
                                             .expected_ranking = {4, 3, 7, 5, 2}},
                          BasicIntTestConfig{.sort_descriptors = {DESC_NULLS_FIRST, ASC_NULLS_FIRST, ASC_NULLS_FIRST},
                                             .k = 3,
                                             .expected_ranking = {6, 2, 0}},
                          BasicIntTestConfig{.sort_descriptors = {ASC_NULLS_FIRST, DESC_NULLS_FIRST, ASC_NULLS_FIRST},
                                             .k = 3,
                                             .expected_ranking = {4, 5, 3}},
                          BasicIntTestConfig{.sort_descriptors = {ASC_NULLS_FIRST, ASC_NULLS_FIRST, DESC_NULLS_FIRST},
                                             .k = 3,
                                             .expected_ranking = {4, 7, 3}}));

struct ComplexTestConfig {
    std::array<SortDescriptor, 6> sort_descriptors;
    RowIdxType k;
    TopKRowPriorityQueue::RankingType expected_ranking;
};

class CelonisTopKRowPriorityQueueComplexTestFixture : public ::testing::TestWithParam<ComplexTestConfig> {
protected:
    void SetUp() override { config = GetParam(); }

    ComplexTestConfig config;
};

TEST_P(CelonisTopKRowPriorityQueueComplexTestFixture, CelonisTopKRowPriorityQueueComplexTest) {
    //throw std::runtime_error{""};
    std::vector<SorterMetadata> sorters;
    auto type_signature{AllTypeRowBuilder::GET_TYPES()};
    for (size_t i{0}; i < type_signature.size(); i++) {
        sorters.push_back(
                SorterMetadata{.logical_type = type_signature[i], .sort_descriptor = config.sort_descriptors[i]});
    }

    AllTypeRowBuilder row_builder{};
    RowSequence<int64_t, DateValue, TimestampValue, Slice, DecimalV2Value, double> row_sequence{};
    row_sequence
            .addRow(3, DateValue::create(1964, 06, 02), TimestampValue::create(2016, 04, 12, 23, 12, 15, 232),
                    Slice{ABC_STR}, DecimalV2Value{"10.4"}, 23.6)
            .addRow(4, DateValue::create(1974, 06, 02), TimestampValue::create(2016, 04, 12, 23, 12, 15, 232),
                    Slice{ABC_STR}, DecimalV2Value{"10.4"}, 23.6)
            .addRow(3, DateValue::create(1964, 06, 02), TimestampValue::create(2016, 04, 12, 23, 12, 15, 232),
                    Slice{ABC_STR}, DecimalV2Value{"10.4"}, 23.6)
            .addRow(4, DateValue::create(1974, 06, 02), TimestampValue::create(2016, 04, 12, 23, 12, 15, 232),
                    Slice{ABC_STR}, DecimalV2Value{"10.4"}, 15.6)
            .addRow(5, DateValue::create(2013, 06, 02), TimestampValue::create(2016, 04, 12, 23, 12, 15, 232),
                    Slice{HELLO_WORLD_STR}, DecimalV2Value{"10.4"}, 23.6)
            .addRow(5, DateValue::create(2013, 06, 03), TimestampValue::create(2016, 04, 12, 23, 12, 15, 232),
                    Slice{LONG_STR}, DecimalV2Value{"10.4"}, 23.6)
            .addRow(2, DateValue::create(2011, 06, 02), TimestampValue::create(2016, 04, 12, 23, 12, 15, 232),
                    Slice{EMPTY_STR}, DecimalV2Value{"10.4"}, 23.6)
            .addRow(2, DateValue::create(2012, 06, 02), TimestampValue::create(2016, 04, 12, 23, 12, 15, 232),
                    Slice{NUMERIC_STR}, DecimalV2Value{"10.4"}, 23.6)
            .addRow(1, DateValue::create(1999, 01, 01), TimestampValue::create(2016, 04, 12, 23, 12, 15, 232),
                    Slice{CELONIS_STR}, DecimalV2Value{"10.4"}, 23.6);

    TopKRowPriorityQueue sorter{config.k, sorters};
    TopKRowPriorityQueue::RankingType actual_ranking{execute_for_sequence(sorter, row_sequence)};
    EXPECT_EQ(actual_ranking, config.expected_ranking);

    // Should be the same for reversed sequence
    std::ranges::reverse(row_sequence.rows);
    TopKRowPriorityQueue reverse_sorter{config.k, sorters};
    TopKRowPriorityQueue::RankingType reverse_actual_ranking{execute_for_sequence(reverse_sorter, row_sequence)};
    EXPECT_EQ(reverse_actual_ranking, config.expected_ranking);
}

INSTANTIATE_TEST_SUITE_P(
        CelonisTopKRowPriorityQueueComplexTest, CelonisTopKRowPriorityQueueComplexTestFixture,
        ::testing::Values(ComplexTestConfig{.sort_descriptors = {ASC_NULLS_FIRST, ASC_NULLS_FIRST, ASC_NULLS_FIRST,
                                                                 ASC_NULLS_FIRST, ASC_NULLS_FIRST, ASC_NULLS_FIRST},
                                            .k = 5,
                                            .expected_ranking = {8, 6, 7, 0, 2}},
                          ComplexTestConfig{.sort_descriptors = {DESC_NULLS_LAST, DESC_NULLS_LAST, DESC_NULLS_LAST,
                                                                 DESC_NULLS_LAST, DESC_NULLS_LAST, DESC_NULLS_LAST},
                                            .k = 5,
                                            .expected_ranking = {5, 4, 1, 3, 0}},
                          ComplexTestConfig{.sort_descriptors = {ASC_NULLS_FIRST, DESC_NULLS_LAST, ASC_NULLS_FIRST,
                                                                 ASC_NULLS_FIRST, ASC_NULLS_FIRST, ASC_NULLS_FIRST},
                                            .k = 5,
                                            .expected_ranking = {8, 7, 6, 0, 2}},
                          ComplexTestConfig{.sort_descriptors = {ASC_NULLS_FIRST, ASC_NULLS_FIRST, DESC_NULLS_LAST,
                                                                 ASC_NULLS_FIRST, ASC_NULLS_FIRST, ASC_NULLS_FIRST},
                                            .k = 5,
                                            .expected_ranking = {8, 6, 7, 0, 2}},
                          ComplexTestConfig{.sort_descriptors = {ASC_NULLS_FIRST, ASC_NULLS_FIRST, ASC_NULLS_FIRST,
                                                                 DESC_NULLS_LAST, ASC_NULLS_FIRST, ASC_NULLS_FIRST},
                                            .k = 5,
                                            .expected_ranking = {8, 6, 7, 0, 2}},
                          ComplexTestConfig{.sort_descriptors = {ASC_NULLS_FIRST, ASC_NULLS_FIRST, ASC_NULLS_FIRST,
                                                                 ASC_NULLS_FIRST, DESC_NULLS_LAST, DESC_NULLS_LAST},
                                            .k = 6,
                                            .expected_ranking = {8, 6, 7, 0, 2, 1}}));
} // namespace
} // namespace top_k
} // namespace celonis
} // namespace starrocks