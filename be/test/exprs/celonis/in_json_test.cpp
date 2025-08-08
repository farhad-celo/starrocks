#include "exprs/celonis/in_json.h"

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/base64.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"
#include "util/defer_op.h"

#include <glog/logging.h>
#include <gtest/gtest.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace starrocks {

struct CelonisInJsonTestParam {
    bool compress_json_str = false;
};

class CelonisInJsonTest :public testing::TestWithParam<CelonisInJsonTestParam> {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    template<LogicalType LT>
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                TypeDescriptor::from_logical_type(LT),
                TypeDescriptor::from_logical_type(TYPE_VARCHAR)};
        auto return_type = TypeDescriptor::from_logical_type(TYPE_BOOLEAN);
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        value_column_ = ColumnHelper::create_column(TypeDescriptor(LT), true);
        match_json_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    }

    template<LogicalType LT>
    void AddRow(const Datum& value, const DatumArray& match_array) {
        value_column_->append_datum(value);
        json match_array_json = ToJsonArray<LT>(match_array);
        std::string match_array_json_str = match_array_json.dump();
        match_json_column_->append_datum(Slice(match_array_json_str));
    }

    template<LogicalType LT>
    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local([this] {
            CelonisInJson<LT>::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL);
        });
        RETURN_IF_ERROR(CelonisInJson<LT>::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] {
            CelonisInJson<LT>::close(ctx_.get(), FunctionContext::THREAD_LOCAL);
        });
        RETURN_IF_ERROR(CelonisInJson<LT>::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        auto result = CelonisInJson<LT>::in_json(ctx_.get(), {value_column_, match_json_column_});
        return result;
    }

    template<LogicalType LT>
    json ToJsonArray(const DatumArray& match_array) {
        json match_array_json = json::array();
        if constexpr (lt_is_string<LT>) {
            for (const auto& item: match_array) {
                if (item.is_null()) {
                    match_array_json.push_back(nullptr);
                } else {
                    match_array_json.push_back(item.get_slice().to_string());
                }
            }
        } else {
            for (const auto& item: match_array) {
                if (item.is_null()) {
                    match_array_json.push_back(nullptr);
                } else {
                    match_array_json.push_back(item.get<RunTimeCppType<LT>>());
                }
            }
        }
        return match_array_json;
    }

    template<LogicalType LT>
    StatusOr<ColumnPtr> RunConstantMatch(const DatumArray& match_array, bool compress_json_str) {
        json match_array_json = ToJsonArray<LT>(match_array);
        std::string match_array_json_str = match_array_json.dump();
        // std::cerr << "match_array_json_str: " << match_array_json_str << std::endl;
        return RunConstantMatch<LT>(match_array_json_str, compress_json_str);
    }

    template<LogicalType LT>
    StatusOr<ColumnPtr> RunConstantMatch(const std::string& json_str, bool compress_json_str) {
        match_json_column_->append_datum(
                Slice(compress_json_str ? compressAndBase64Encode(json_str) : json_str));
        const auto nrows = value_column_->size();
        match_json_column_ = ConstColumn::create(match_json_column_, nrows);
        ctx_->set_constant_columns({nullptr, match_json_column_});
        return Run<LT>();
    }

    std::string compressAndBase64Encode(const std::string& str) {
        std::string compressed_str = compress_string(str, false);
        int cipher_len = (size_t) (4.0 * ceil((double) compressed_str.length() / 3.0)) + 1;
        std::string p(cipher_len, '\0');
        int len = base64_encode3((unsigned char*) compressed_str.data(), compressed_str.length(),
                                 (unsigned char*) p.data());
        std::string encoded_string(p.data(), len);
        return encoded_string;
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr value_column_;
    ColumnPtr match_json_column_;
};

TEST_P(CelonisInJsonTest, string_data_no_null_in_match_list) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum(Datum());
    value_column_->append_datum("string3");

    auto match_array = DatumArray{"string1", "string3"};

    const auto result = RunConstantMatch<LT>(match_array, GetParam().compress_json_str).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_P(CelonisInJsonTest, string_data_null_in_match_list) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum(Datum());
    value_column_->append_datum("string3");

    auto match_array = DatumArray{"string1", "string3", Datum()};

    const auto result = RunConstantMatch<LT>(match_array, GetParam().compress_json_str).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_P(CelonisInJsonTest, string_empty_match_list) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();

    value_column_->append_datum("string1");
    value_column_->append_datum("string2");
    value_column_->append_datum(Datum());
    value_column_->append_datum("string3");

    auto match_array = DatumArray{};

    const auto result = RunConstantMatch<LT>(match_array, GetParam().compress_json_str).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(false, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(false, result->get(3).get_uint8());
}

TEST_P(CelonisInJsonTest, int_input) {
    const LogicalType LT = TYPE_INT;
    Prepare<LT>();

    value_column_->append_datum(1);
    value_column_->append_datum(2);
    value_column_->append_datum(Datum());
    value_column_->append_datum(3);

    auto match_array = DatumArray{Datum{}, 2};

    const auto result = RunConstantMatch<LT>(match_array, GetParam().compress_json_str).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(false, result->get(0).get_uint8());
    EXPECT_EQ(true, result->get(1).get_uint8());
    EXPECT_EQ(true, result->get(2).get_uint8());
    EXPECT_EQ(false, result->get(3).get_uint8());
}

TEST_P(CelonisInJsonTest, bigint_input) {
    const LogicalType LT = TYPE_BIGINT;
    Prepare<LT>();

    value_column_->append_datum(1L);
    value_column_->append_datum(2L);
    value_column_->append_datum(Datum());
    value_column_->append_datum(INT64_MIN);
    value_column_->append_datum(INT64_MAX);

    auto match_array = DatumArray{1L, INT64_MIN};

    const auto result = RunConstantMatch<LT>(match_array, GetParam().compress_json_str).value();
    EXPECT_EQ(5, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
    EXPECT_EQ(false, result->get(4).get_uint8());
}

TEST_P(CelonisInJsonTest, double_input) {
    const LogicalType LT = TYPE_DOUBLE;
    Prepare<LT>();

    value_column_->append_datum(1.1);
    value_column_->append_datum(2.2);
    value_column_->append_datum(Datum());
    value_column_->append_datum(3.3);

    auto match_array = DatumArray{1.1, 3.3};

    const auto result = RunConstantMatch<LT>(match_array, GetParam().compress_json_str).value();
    EXPECT_EQ(4, result->size());
    EXPECT_EQ(true, result->get(0).get_uint8());
    EXPECT_EQ(false, result->get(1).get_uint8());
    EXPECT_EQ(false, result->get(2).get_uint8());
    EXPECT_EQ(true, result->get(3).get_uint8());
}

TEST_P(CelonisInJsonTest, invalid_json_str) {
    const LogicalType LT = TYPE_VARCHAR;
    Prepare<LT>();
    value_column_->append_datum("HELLO");
    const auto result = RunConstantMatch<LT>("UNKNOWN", GetParam().compress_json_str);
    EXPECT_TRUE(result.status().is_invalid_argument());
    if (GetParam().compress_json_str) {
        EXPECT_EQ("[CELONIS_IN_JSON] Invalid JSON format: UNKNOWN", result.status().message());
    } else {
        EXPECT_EQ("[CELONIS_IN_JSON] Failed to decompress JSON data", result.status().message());
    }
}

TEST_F(CelonisInJsonTest, non_const_match_fail) {
    const LogicalType LT = TYPE_INT;
    Prepare<LT>();

    AddRow<LT>(1, {10, 20, 30});
    AddRow<LT>(100, {100, 200, 300, Datum{}});

    const auto result = Run<LT>();
    EXPECT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ("The non-const version of [CELONIS_IN_JSON] should not be called.", result.status().message());
}

INSTANTIATE_TEST_SUITE_P(CelonisInJsonTest, CelonisInJsonTest,
                         ::testing::Values(CelonisInJsonTestParam{true}, CelonisInJsonTestParam{false}));


} // namespace starrocks
