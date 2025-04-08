#include "exprs/celonis/sort_utils.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace starrocks {
namespace celonis {
namespace {

// Helper to converts an arbitrary type to a signed type if its unsigned arithmetic and keep it as is otherwise.
template <typename T>
struct signed_to_unsigned {
    using Type = T;
};

template <typename T>
    requires std::is_unsigned_v<T>
struct signed_to_unsigned<T> {
    using Type = std::make_signed_t<T>;
};

template <typename T>
using signed_to_unsigned_t = signed_to_unsigned<T>::Type;

// Define high and low value to compare for each type
template <typename T>
struct TestedValues {};

template <typename T>
    requires std::is_arithmetic_v<T>
struct TestedValues<T> {
    static constexpr T LOW_VALUE{std::numeric_limits<T>::min()};
    // Starrocks automatically converts unsigned to signed types, so we cannot use a value that would become negative
    static constexpr T HIGH_VALUE{std::numeric_limits<signed_to_unsigned_t<T>>::max()};
};

template <>
struct TestedValues<Slice> {
    static constexpr std::string_view LOW_VALUE_STR{""};
    static constexpr std::string_view HIGH_VALUE_STR{"abc"};
    static inline const Slice LOW_VALUE{LOW_VALUE_STR.data(), LOW_VALUE_STR.size()};
    static inline const Slice HIGH_VALUE{HIGH_VALUE_STR.data(), HIGH_VALUE_STR.size()};
};

template <>
struct TestedValues<DecimalV2Value> {
    static inline const DecimalV2Value LOW_VALUE{DecimalV2Value::get_min_decimal()};
    static inline const DecimalV2Value HIGH_VALUE{DecimalV2Value::get_max_decimal()};
};

template <>
struct TestedValues<TimestampValue> {
    static inline const TimestampValue LOW_VALUE{TimestampValue::MIN_TIMESTAMP_VALUE};
    static inline const TimestampValue HIGH_VALUE{TimestampValue::MAX_TIMESTAMP_VALUE};
};

template <>
struct TestedValues<DateValue> {
    static inline const DateValue LOW_VALUE{DateValue::MIN_DATE_VALUE};
    static inline const DateValue HIGH_VALUE{DateValue::MAX_DATE_VALUE};
};

template <typename T>
class ParametrizedCmpTest : public ::testing::Test {
protected:
    Datum low_value{TestedValues<T>::LOW_VALUE};
    Datum high_value{TestedValues<T>::HIGH_VALUE};
    Datum null_datum{};
};

TYPED_TEST_SUITE_P(ParametrizedCmpTest);

TYPED_TEST_P(ParametrizedCmpTest, DatumComparatorAsc) {
    SortDescriptor desc{.sort_order = SortOrder::ASC, .null_handling = NullHandling::NULLS_FIRST};

    EXPECT_EQ(DatumComparator{}(this->low_value, this->high_value, desc), CmpResult::LESS);
    EXPECT_EQ(DatumComparator{}(this->high_value, this->low_value, desc), CmpResult::GREATER);
    EXPECT_EQ(DatumComparator{}(this->low_value, this->low_value, desc), CmpResult::EQUAL);
    EXPECT_EQ(DatumComparator{}(this->high_value, this->high_value, desc), CmpResult::EQUAL);
}

TYPED_TEST_P(ParametrizedCmpTest, DatumComparatorDesc) {
    SortDescriptor desc{.sort_order = SortOrder::DESC, .null_handling = NullHandling::NULLS_FIRST};

    EXPECT_EQ(DatumComparator{}(this->low_value, this->high_value, desc), CmpResult::GREATER);
    EXPECT_EQ(DatumComparator{}(this->high_value, this->low_value, desc), CmpResult::LESS);
    EXPECT_EQ(DatumComparator{}(this->low_value, this->low_value, desc), CmpResult::EQUAL);
    EXPECT_EQ(DatumComparator{}(this->high_value, this->high_value, desc), CmpResult::EQUAL);
}

TYPED_TEST_P(ParametrizedCmpTest, DatumComparatorNullsFirstAsc) {
    SortDescriptor desc{.sort_order = SortOrder::ASC, .null_handling = NullHandling::NULLS_FIRST};

    EXPECT_EQ(DatumComparator{}(this->null_datum, this->null_datum, desc), CmpResult::EQUAL);
    EXPECT_EQ(DatumComparator{}(this->null_datum, this->high_value, desc), CmpResult::LESS);
    EXPECT_EQ(DatumComparator{}(this->null_datum, this->low_value, desc), CmpResult::LESS);
    EXPECT_EQ(DatumComparator{}(this->high_value, this->null_datum, desc), CmpResult::GREATER);
    EXPECT_EQ(DatumComparator{}(this->low_value, this->null_datum, desc), CmpResult::GREATER);
}

TYPED_TEST_P(ParametrizedCmpTest, DatumComparatorNullsFirstDesc) {
    SortDescriptor desc{.sort_order = SortOrder::DESC, .null_handling = NullHandling::NULLS_FIRST};

    EXPECT_EQ(DatumComparator{}(this->null_datum, this->null_datum, desc), CmpResult::EQUAL);
    EXPECT_EQ(DatumComparator{}(this->null_datum, this->high_value, desc), CmpResult::GREATER);
    EXPECT_EQ(DatumComparator{}(this->null_datum, this->low_value, desc), CmpResult::GREATER);
    EXPECT_EQ(DatumComparator{}(this->high_value, this->null_datum, desc), CmpResult::LESS);
    EXPECT_EQ(DatumComparator{}(this->low_value, this->null_datum, desc), CmpResult::LESS);
}

TYPED_TEST_P(ParametrizedCmpTest, DatumComparatorNullsLastAsc) {
    SortDescriptor desc{.sort_order = SortOrder::ASC, .null_handling = NullHandling::NULLS_LAST};

    EXPECT_EQ(DatumComparator{}(this->null_datum, this->null_datum, desc), CmpResult::EQUAL);
    EXPECT_EQ(DatumComparator{}(this->null_datum, this->high_value, desc), CmpResult::GREATER);
    EXPECT_EQ(DatumComparator{}(this->null_datum, this->low_value, desc), CmpResult::GREATER);
    EXPECT_EQ(DatumComparator{}(this->high_value, this->null_datum, desc), CmpResult::LESS);
    EXPECT_EQ(DatumComparator{}(this->low_value, this->null_datum, desc), CmpResult::LESS);
}

TYPED_TEST_P(ParametrizedCmpTest, DatumComparatorNullsLastDesc) {
    SortDescriptor desc{.sort_order = SortOrder::DESC, .null_handling = NullHandling::NULLS_LAST};

    EXPECT_EQ(DatumComparator{}(this->null_datum, this->null_datum, desc), CmpResult::EQUAL);
    EXPECT_EQ(DatumComparator{}(this->null_datum, this->high_value, desc), CmpResult::LESS);
    EXPECT_EQ(DatumComparator{}(this->null_datum, this->low_value, desc), CmpResult::LESS);
    EXPECT_EQ(DatumComparator{}(this->high_value, this->null_datum, desc), CmpResult::GREATER);
    EXPECT_EQ(DatumComparator{}(this->low_value, this->null_datum, desc), CmpResult::GREATER);
}

REGISTER_TYPED_TEST_SUITE_P(ParametrizedCmpTest, DatumComparatorAsc, DatumComparatorDesc, DatumComparatorNullsFirstAsc,
                            DatumComparatorNullsFirstDesc, DatumComparatorNullsLastAsc, DatumComparatorNullsLastDesc);

using TestedTypes =
        ::testing::Types<uint64_t, int64_t, float, double, Slice, DecimalV2Value, TimestampValue, DateValue>;

INSTANTIATE_TYPED_TEST_SUITE_P(ParametrizedCmpTestExecution, ParametrizedCmpTest, TestedTypes);

} // namespace
} // namespace celonis
} // namespace starrocks