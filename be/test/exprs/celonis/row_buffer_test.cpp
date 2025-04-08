#include "exprs/celonis/row_buffer.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace starrocks {
namespace celonis {
namespace {

TEST(CelonisCompareDatumTest, row_buffer_empty_buffer) {
    ASSERT_THROW((RowBuffer{0, {TYPE_BIGINT}}), std::invalid_argument);
}

struct NullValue {
    NullValue() {}
};

class TestRow : public RowBufferRowAccessor {
public:
    TestRow(ColumnIdxType num_columns, std::vector<Datum> data)
            : RowBufferRowAccessor{num_columns}, data_(std::move(data)) {}

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
        return TestRow{sizeof...(Types), std::move(data)};
    }

private:
    template <typename T>
    Datum make_datum(T value) {
        return Datum{std::move(value)};
    }

    std::vector<std::string> strings_;
};

template <>
Datum RowBuilder::make_datum(std::string value) {
    strings_.emplace_back(std::move(value));
    const std::string& stored_string{strings_.back()};
    return Datum{Slice{stored_string.data(), stored_string.size()}};
}

template <>
Datum RowBuilder::make_datum(NullValue /*nv*/) {
    return Datum{};
}

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

void compare_slot(const RowBuffer& buffer, RowIdxType slot_id, const TestRow& row) {
    for (ColumnIdxType i{0}; i < row.num_columns(); i++) {
        Datum expected_datum{row.get(i)};
        if (expected_datum.is_null()) {
            Datum actual_datum{buffer.get(slot_id, i)};
            ASSERT_TRUE(actual_datum.is_null()) << "Expected null value but got non-null value at column " << i << ".";
            continue;
        }
        expected_datum.visit([&](const auto& datum_variant) {
            std::visit(overloaded{[&](const RowBufferSupportedType auto& expected_value) {
                                      using ValueType = std::decay_t<decltype(expected_value)>;
                                      Datum actual_datum{buffer.get(slot_id, i)};
                                      ASSERT_FALSE(actual_datum.is_null())
                                              << "Expected non-null value but got null value at column " << i << ".";
                                      const ValueType& actual_value{actual_datum.get<ValueType>()};
                                      ASSERT_EQ(expected_value, actual_value)
                                              << "Mismatch between expected and actual value at column " << i << ".";
                                  },
                                  [&](const auto& expected_value) {
                                      using ValueType = std::decay_t<decltype(expected_value)>;
                                      FAIL() << "Unsupported type " << typeid(ValueType).name() << ".";
                                  }},
                       datum_variant);
        });
    }
}

TEST(CelonisRowBufferTest, row_buffer_empty_buffer) {
    ASSERT_THROW((RowBuffer{0, {TYPE_BIGINT}}), std::invalid_argument);
}

TEST(CelonisRowBufferTest, row_buffer_too_large) {
    ASSERT_THROW((RowBuffer{1'000'000, {TYPE_BIGINT}}), std::invalid_argument);
}

TEST(CelonisRowBufferTest, row_buffer_empty_type_signature) {
    ASSERT_THROW((RowBuffer{5, {}}), std::invalid_argument);
}

TEST(CelonisRowBufferTest, row_buffer_single_slot_integer) {
    RowBuffer buffer{1, {LogicalType::TYPE_BIGINT, LogicalType::TYPE_BIGINT, LogicalType::TYPE_BIGINT}};

    RowBuilder row_builder{};
    using BigIntCppType = RunTimeCppType<TYPE_BIGINT>;
    TestRow row1{row_builder.makeRow<BigIntCppType, BigIntCppType, BigIntCppType>(1, 2, 3)};
    TestRow row2{row_builder.makeRow<BigIntCppType, BigIntCppType, BigIntCppType>(9, 8, 7)};

    buffer.set(0, row1);
    compare_slot(buffer, 0, row1);

    buffer.set(0, row2);
    compare_slot(buffer, 0, row2);
}

TEST(CelonisRowBufferTest, row_buffer_multi_slot_integer) {
    RowBuffer buffer{3, {LogicalType::TYPE_BIGINT, LogicalType::TYPE_BIGINT, LogicalType::TYPE_BIGINT}};

    RowBuilder row_builder{};
    using BigIntCppType = RunTimeCppType<TYPE_BIGINT>;
    TestRow row1{row_builder.makeRow<BigIntCppType, BigIntCppType, BigIntCppType>(1, 2, 3)};
    TestRow row2{row_builder.makeRow<BigIntCppType, BigIntCppType, BigIntCppType>(4, 5, 6)};
    TestRow row3{row_builder.makeRow<BigIntCppType, BigIntCppType, BigIntCppType>(7, 8, 9)};

    buffer.set(0, row1);
    compare_slot(buffer, 0, row1);

    buffer.set(1, row2);
    compare_slot(buffer, 0, row1);
    compare_slot(buffer, 1, row2);

    buffer.set(0, row3);
    compare_slot(buffer, 0, row3);
    compare_slot(buffer, 1, row2);

    buffer.set(2, row2);
    compare_slot(buffer, 0, row3);
    compare_slot(buffer, 1, row2);
    compare_slot(buffer, 2, row2);
}

TEST(CelonisRowBufferTest, row_buffer_multi_slot_integer_with_null) {
    RowBuffer buffer{3, {LogicalType::TYPE_BIGINT, LogicalType::TYPE_BIGINT, LogicalType::TYPE_BIGINT}};

    RowBuilder row_builder{};
    using BigIntCppType = RunTimeCppType<TYPE_BIGINT>;
    TestRow row1{row_builder.makeRow<NullValue, BigIntCppType, BigIntCppType>({}, 2, 3)};
    TestRow row2{row_builder.makeRow<BigIntCppType, NullValue, BigIntCppType>(4, {}, 6)};
    TestRow row3{row_builder.makeRow<BigIntCppType, BigIntCppType, NullValue>(7, 8, {})};

    buffer.set(0, row1);
    compare_slot(buffer, 0, row1);

    buffer.set(1, row2);
    compare_slot(buffer, 0, row1);
    compare_slot(buffer, 1, row2);

    buffer.set(0, row3);
    compare_slot(buffer, 0, row3);
    compare_slot(buffer, 1, row2);

    buffer.set(2, row2);
    compare_slot(buffer, 0, row3);
    compare_slot(buffer, 1, row2);
    compare_slot(buffer, 2, row2);
}

TEST(CelonisRowBufferTest, row_buffer_string_overwrite) {
    RowBuffer buffer{2, {LogicalType::TYPE_VARCHAR, LogicalType::TYPE_VARCHAR, LogicalType::TYPE_VARCHAR}};

    RowBuilder row_builder{};
    TestRow row1{row_builder.makeRow<Slice, Slice, Slice>(Slice{"abc"}, Slice{"Hi"}, Slice{"test"})};
    TestRow row2{row_builder.makeRow<Slice, Slice, Slice>(Slice{"abcdefg"}, Slice{"A"}, Slice{"Hello World"})};
    TestRow row3{row_builder.makeRow<Slice, Slice, Slice>(Slice{"."}, Slice{"This is a test"}, Slice{"abcdefghijklm"})};

    buffer.set(0, row1);
    compare_slot(buffer, 0, row1);

    buffer.set(0, row2);
    compare_slot(buffer, 0, row2);

    buffer.set(0, row3);
    compare_slot(buffer, 0, row3);

    buffer.set(0, row1);
    compare_slot(buffer, 0, row1);

    buffer.set(1, row3);
    compare_slot(buffer, 0, row1);
    compare_slot(buffer, 1, row3);

    buffer.set(1, row2);
    compare_slot(buffer, 0, row1);
    compare_slot(buffer, 1, row2);
}

TEST(CelonisRowBufferTest, row_buffer_string_overwrite_with_null) {
    RowBuffer buffer{2, {LogicalType::TYPE_VARCHAR, LogicalType::TYPE_VARCHAR, LogicalType::TYPE_VARCHAR}};

    RowBuilder row_builder{};
    TestRow row1{row_builder.makeRow<NullValue, Slice, Slice>({}, Slice{"Hi"}, Slice{"test"})};
    TestRow row2{row_builder.makeRow<Slice, NullValue, Slice>(Slice{"abcdefg"}, {}, Slice{"Hello World"})};
    TestRow row3{row_builder.makeRow<Slice, Slice, NullValue>(Slice{"."}, Slice{"This is a test"}, {})};

    buffer.set(0, row1);
    compare_slot(buffer, 0, row1);

    buffer.set(0, row2);
    compare_slot(buffer, 0, row2);

    buffer.set(0, row3);
    compare_slot(buffer, 0, row3);

    buffer.set(0, row1);
    compare_slot(buffer, 0, row1);

    buffer.set(1, row3);
    compare_slot(buffer, 0, row1);
    compare_slot(buffer, 1, row3);

    buffer.set(1, row2);
    compare_slot(buffer, 0, row1);
    compare_slot(buffer, 1, row2);
}

TEST(CelonisRowBufferTest, row_buffer_all_types) {
    RowBuffer buffer{3, AllTypeRowBuilder::GET_TYPES()};

    AllTypeRowBuilder row_builder{};
    TestRow row1{row_builder.makeRow(4, DateValue::create(1990, 10, 15),
                                     TimestampValue::create(2002, 03, 15, 12, 21, 42, 3), Slice{"abc"},
                                     DecimalV2Value::get_max_decimal(), 4.2)};
    TestRow row2{row_builder.makeRow(-35, DateValue::create(1994, 12, 20),
                                     TimestampValue::create(1975, 11, 2, 5, 53, 12, 646), Slice{"Hello World"},
                                     DecimalV2Value::get_max_decimal() / DecimalV2Value{2}, 4.2)};
    TestRow row3{row_builder.makeRow(243532, DateValue::create(2012, 12, 12),
                                     TimestampValue::create(1999, 1, 1, 17, 05, 1, 133), Slice{"Bye"},
                                     DecimalV2Value::get_max_decimal() / DecimalV2Value{4}, -15.16)};

    buffer.set(0, row1);
    compare_slot(buffer, 0, row1);

    buffer.set(0, row2);
    compare_slot(buffer, 0, row2);

    buffer.set(0, row3);
    compare_slot(buffer, 0, row3);

    buffer.set(0, row1);
    compare_slot(buffer, 0, row1);

    buffer.set(1, row3);
    compare_slot(buffer, 0, row1);
    compare_slot(buffer, 1, row3);

    buffer.set(1, row2);
    compare_slot(buffer, 0, row1);
    compare_slot(buffer, 1, row2);

    buffer.set(2, row3);
    compare_slot(buffer, 0, row1);
    compare_slot(buffer, 1, row2);
    compare_slot(buffer, 2, row3);
}

} // namespace
} // namespace celonis
} // namespace starrocks