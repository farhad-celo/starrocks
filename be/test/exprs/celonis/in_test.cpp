#include "exprs/celonis/in.h"

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace starrocks {

class CelonisInTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    template <LogicalType TYPE>
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE))};
        auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BOOLEAN));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        input_ = ColumnHelper::create_column(TypeDescriptor(TYPE), true);
        match_array_ = ColumnHelper::create_column(celonis::array_type(TYPE), false);
    }

    StatusOr<ColumnPtr> Run() {
        return CelonisIn::celonis_in(ctx_.get(), {input_, match_array_});
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr input_;
    ColumnPtr match_array_;
};

TEST_F(CelonisInTest, celonis_in_string_data_no_null_in_match_list) {
    Prepare<TYPE_VARCHAR>();

    input_->append_datum("string1");
    input_->append_datum("string2");
    input_->append_datum(Datum());
    input_->append_datum("string3");

    match_array_->append_datum(DatumArray{"string1", "string3"});

    const auto result = Run().value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_string_data_null_in_match_list) {
    Prepare<TYPE_VARCHAR>();

    input_->append_datum("string1");
    input_->append_datum("string2");
    input_->append_datum(Datum());
    input_->append_datum("string3");

    match_array_->append_datum(DatumArray{"string1", "string3", Datum()});

    const auto result = Run().value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_string_empty_match_list) {
    Prepare<TYPE_VARCHAR>();

    input_->append_datum("string1");
    input_->append_datum("string2");
    input_->append_datum(Datum());
    input_->append_datum("string3");

    match_array_->append_datum(DatumArray{});

    const auto result = Run().value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(false, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(false, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_int) {
    Prepare<TYPE_INT>();

    input_->append_datum(1);
    input_->append_datum(2);
    input_->append_datum(Datum());
    input_->append_datum(3);

    match_array_->append_datum(DatumArray{Datum{}, 2});

    const auto result = Run().value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(false, result->get(0).get_uint8());
    EXPECT_EQ(true, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(false, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_bigint) {
    Prepare<TYPE_BIGINT>();

    input_->append_datum(1L);
    input_->append_datum(2L);
    input_->append_datum(Datum());
    input_->append_datum(INT64_MIN);
    input_->append_datum(INT64_MAX);

    match_array_->append_datum(DatumArray{1L, INT64_MIN});

    const auto result = Run().value();
    EXPECT_EQ(5, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
    EXPECT_EQ(false, result->get(4).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_double) {
    Prepare<TYPE_DOUBLE>();

    input_->append_datum(1.1);
    input_->append_datum(2.2);
    input_->append_datum(Datum());
    input_->append_datum(3.3);

    match_array_->append_datum(DatumArray{1.1, 3.3});

    const auto result = Run().value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_datetime) {
    Prepare<TYPE_DATETIME>();

    auto datetime1 = TimestampValue::create(2017, 10, 1, 2, 32, 32);
    auto datetime2 = TimestampValue::create(2017, 10, 2, 2, 32, 32);
    auto datetime3 = TimestampValue::create(2017, 10, 3, 2, 32, 32);
    auto datetime4 = TimestampValue::create(2017, 10, 4, 2, 32, 32);

    input_->append_datum(datetime1);
    input_->append_datum(datetime3);
    input_->append_datum(Datum());
    input_->append_datum(datetime4);

    match_array_->append_datum(DatumArray{datetime1, datetime2, datetime3, Datum{}});

    const auto result = Run().value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(true, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(false, result->get(3).get_uint8());
}

TEST_F(CelonisInTest, celonis_in_unsupported_type) {
    Prepare<TYPE_DECIMALV2>();

    match_array_->append_datum(DatumArray{});

    EXPECT_THROW(Run(), std::runtime_error);
}

} // namespace starrocks
