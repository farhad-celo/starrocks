#include "exprs/celonis/encode_string.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <utility>

#include "column/column_helper.h"
#include "column/datum.h"
#include "exprs/celonis/anyval_util.h"
#include "exprs/function_context.h"
#include "runtime/types.h"
#include "util.h"
#include "util/defer_op.h"

namespace starrocks {

namespace {

struct TestCaseConst {
    Datum string;
    Datum expected;
};

struct TestCaseNonConst {
    Datum string;
    Datum dict;
    Datum expected;
};

class CelonisEncodeStringTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR)),
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
        auto return_type = CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        string_column_ = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
        dict_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
    }

    void TearDown() override {}

private:
    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local(
                [this] { CelonisEncodeString::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisEncodeString::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] { CelonisEncodeString::close(ctx_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisEncodeString::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        auto result = CelonisEncodeString::encode_string(ctx_.get(), {string_column_, dict_column_});
        return result;
    }

    StatusOr<ColumnPtr> RunConstantMap(const Datum& dict) {
        dict_column_->append_datum(dict);
        const auto num_rows = string_column_->size();
        dict_column_ = ConstColumn::create(dict_column_, num_rows);
        ctx_->set_constant_columns({nullptr, dict_column_});
        return Run();
    }

    void Validate(const ColumnPtr& result, size_t row, const Datum& expected) {
        ASSERT_LT(row, result->size());
        if (expected.is_null()) {
            EXPECT_TRUE(result->is_null(row)) << "row: " << row;
        } else {
            EXPECT_EQ(result->get(row).get_int32(), expected.get_int32()) << "row: " << row;
        }
    }

    void RunAndValidateConst(const std::vector<TestCaseConst>& test_cases, const Datum& dict) {
        for (const auto& row : test_cases) {
            string_column_->append_datum(row.string);
        }
        const auto result = RunConstantMap(dict).value();
        ASSERT_EQ(test_cases.size(), result->size());
        for (size_t i = 0; i < test_cases.size(); ++i) {
            Validate(result, i, test_cases.at(i).expected);
        }
    }

    void RunAndValidateNonConst(const std::vector<TestCaseNonConst>& test_cases) {
        for (const auto& row : test_cases) {
            string_column_->append_datum(row.string);
            dict_column_->append_datum(row.dict);
        }
        const auto result = Run().value();
        ASSERT_EQ(test_cases.size(), result->size());
        for (size_t i = 0; i < test_cases.size(); ++i) {
            Validate(result, i, test_cases.at(i).expected);
        }
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr string_column_;
    ColumnPtr dict_column_;
};

} // namespace

TEST_F(CelonisEncodeStringTest, null_dict) {
    const Datum dict = kNullDatum;
    const std::vector<TestCaseConst> test_cases = {
            {/*string*/ "A", /*expected*/ kNullDatum},
            {kNullDatum, kNullDatum},
    };

    RunAndValidateConst(test_cases, dict);
}

TEST_F(CelonisEncodeStringTest, empty_dict) {
    const Datum dict = DatumArray{};
    const std::vector<TestCaseConst> test_cases = {
            {/*string*/ "A", /*expected*/ -1},
            {kNullDatum, kNullDatum},
    };

    RunAndValidateConst(test_cases, dict);
}

TEST_F(CelonisEncodeStringTest, const_dict_normal_case) {
    const Datum dict = DatumArray{"A", kNullDatum, "C", "A", "B", "D", kNullDatum};
    const std::vector<TestCaseConst> test_cases = {
            {/*string*/ "A", /*expected*/ 0}, {"B", 2}, {"C", 1}, {"D", 3}, {kNullDatum, kNullDatum}, {"E", -1},
    };

    RunAndValidateConst(test_cases, dict);
}

TEST_F(CelonisEncodeStringTest, non_const_dict_normal_case) {
    const std::vector<TestCaseNonConst> test_cases = {
            {/*string*/ kNullDatum, /*dict*/ kNullDatum, /*expected*/ kNullDatum},
            {"A", kNullDatum, kNullDatum},
            {kNullDatum, DatumArray{"A"}, kNullDatum},
            {"A", DatumArray{}, -1},
            {"A", DatumArray{"B"}, -1},
            {"A", DatumArray{"A"}, 0},
            {"A", DatumArray{kNullDatum, "A"}, 0},
            {"A", DatumArray{"B", "A"}, 1},
            {"A", DatumArray{"A", "B", "A"}, 0},
    };

    RunAndValidateNonConst(test_cases);
}

} // namespace starrocks
