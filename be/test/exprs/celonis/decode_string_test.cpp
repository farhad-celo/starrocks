#include "exprs/celonis/decode_string.h"

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
    Datum dict_id;
    Datum expected;
};

struct TestCaseNonConst {
    Datum dict_id;
    Datum dict;
    Datum expected;
};

class CelonisDecodeStringTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_INT)),
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
        auto return_type = CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        dict_id_column_ = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_INT), true);
        dict_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), true);
    }

    void TearDown() override {}

private:
    StatusOr<ColumnPtr> Run() {
        DeferOp close_fragment_local(
                [this] { CelonisDecodeString::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisDecodeString::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] { CelonisDecodeString::close(ctx_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisDecodeString::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        auto result = CelonisDecodeString::decode_string(ctx_.get(), {dict_id_column_, dict_column_});
        return result;
    }

    StatusOr<ColumnPtr> RunConstantMap(const Datum& dict) {
        dict_column_->append_datum(dict);
        const auto num_rows = dict_id_column_->size();
        dict_column_ = ConstColumn::create(dict_column_, num_rows);
        ctx_->set_constant_columns({nullptr, dict_column_});
        return Run();
    }

    void Validate(const ColumnPtr& result, size_t row, const Datum& expected) {
        ASSERT_LT(row, result->size());
        if (expected.is_null()) {
            EXPECT_TRUE(result->is_null(row)) << "row: " << row;
        } else {
            EXPECT_EQ(result->get(row).get_slice(), expected.get_slice()) << "row: " << row;
        }
    }

    void RunAndValidateConst(const std::vector<TestCaseConst>& test_cases, const Datum& dict) {
        for (const auto& row : test_cases) {
            dict_id_column_->append_datum(row.dict_id);
        }
        const auto result = RunConstantMap(dict).value();
        ASSERT_EQ(test_cases.size(), result->size());
        for (size_t i = 0; i < test_cases.size(); ++i) {
            Validate(result, i, test_cases.at(i).expected);
        }
    }

    void RunAndValidateNonConst(const std::vector<TestCaseNonConst>& test_cases) {
        for (const auto& row : test_cases) {
            dict_id_column_->append_datum(row.dict_id);
            dict_column_->append_datum(row.dict);
        }
        const auto result = Run().value();
        ASSERT_EQ(test_cases.size(), result->size());
        for (size_t i = 0; i < test_cases.size(); ++i) {
            Validate(result, i, test_cases.at(i).expected);
        }
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr dict_id_column_;
    ColumnPtr dict_column_;
};

} // namespace

TEST_F(CelonisDecodeStringTest, null_dict) {
    const Datum dict = kNullDatum;
    const std::vector<TestCaseConst> test_cases = {
            {/*dict_id*/ 0, /*expected*/ kNullDatum},
            {kNullDatum, kNullDatum},
    };

    RunAndValidateConst(test_cases, dict);
}

TEST_F(CelonisDecodeStringTest, empty_dict) {
    const Datum dict = DatumArray{};
    const std::vector<TestCaseConst> test_cases = {
            {/*dict_id*/ 0, /*expected*/ kNullDatum},
            {kNullDatum, kNullDatum},
    };

    RunAndValidateConst(test_cases, dict);
}

TEST_F(CelonisDecodeStringTest, const_dict_normal_case) {
    const Datum dict = DatumArray{"A", kNullDatum, "C", "A", "B", "D", kNullDatum};
    const std::vector<TestCaseConst> test_cases = {
            {/*dict_id*/ 0, /*expected*/ "A"}, {1, "C"}, {2, "B"}, {3, "D"}, {4, kNullDatum}, {-1, kNullDatum},
    };

    RunAndValidateConst(test_cases, dict);
}

TEST_F(CelonisDecodeStringTest, non_const_dict_normal_case) {
    const std::vector<TestCaseNonConst> test_cases = {
            {/*dict_id*/ kNullDatum, /*dict*/ kNullDatum, /*expected*/ kNullDatum},
            {0, kNullDatum, kNullDatum},
            {kNullDatum, DatumArray{"A"}, kNullDatum},
            {0, DatumArray{}, kNullDatum},
            {0, DatumArray{"A"}, "A"},
            {1, DatumArray{"A"}, kNullDatum},
            {-1, DatumArray{"A"}, kNullDatum},
            {0, DatumArray{kNullDatum, "A"}, "A"},
            {0, DatumArray{"B", "A"}, "B"},
            {1, DatumArray{"B", "A"}, "A"},
            {0, DatumArray{"A", kNullDatum, "B", "A"}, "A"},
            {1, DatumArray{"A", kNullDatum, "B", "A"}, "B"},
    };

    RunAndValidateNonConst(test_cases);
}

} // namespace starrocks
