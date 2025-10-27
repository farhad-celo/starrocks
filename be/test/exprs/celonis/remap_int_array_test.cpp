#include "exprs/celonis/remap_int_array.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util.h"
#include "util/defer_op.h"

namespace starrocks {

class CelonisRemapIntArrayTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_BIGINT)),
                AnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_BIGINT)),
                AnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_BIGINT)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT))};
        auto return_type = AnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_BIGINT));
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        input_array_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_BIGINT), true);
        default_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
        old_array_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_BIGINT), false);
        new_array_column_ = ColumnHelper::create_column(celonis::array_type(TYPE_BIGINT), false);
    }

    void AddRow(const DatumArray& input_array, const DatumArray& old_array, const DatumArray& new_array,
                const Datum& default_value) {
        input_array_column_->append_datum(input_array);
        old_array_column_->append_datum(old_array);
        new_array_column_->append_datum(new_array);
        default_column_->append_datum(default_value);
    }

    StatusOr<ColumnPtr> Run(bool has_default) {
        DeferOp close_fragment_local(
                [this] { CelonisRemapIntArray::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisRemapIntArray::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] { CelonisRemapIntArray::close(ctx_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisRemapIntArray::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        if (has_default) {
            result = CelonisRemapIntArray::remap_int_array(
                    ctx_.get(), {input_array_column_, old_array_column_, new_array_column_, default_column_});
        } else {
            result = CelonisRemapIntArray::remap_int_array(ctx_.get(),
                                                           {input_array_column_, old_array_column_, new_array_column_});
        }
        return result;
    }

    StatusOr<ColumnPtr> RunConstantValueMap(const DatumArray& old_array, const DatumArray& new_array,
                                            bool has_default) {
        old_array_column_->append_datum(old_array);
        new_array_column_->append_datum(new_array);
        const auto nrows = input_array_column_->size();
        old_array_column_ = ConstColumn::create(old_array_column_, nrows);
        new_array_column_ = ConstColumn::create(new_array_column_, nrows);
        ctx_->set_constant_columns({nullptr, old_array_column_, new_array_column_, nullptr});
        return Run(has_default);
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr input_array_column_;
    ColumnPtr old_array_column_;
    ColumnPtr new_array_column_;
    ColumnPtr default_column_;
};

TEST_F(CelonisRemapIntArrayTest, remap_int_array_const_inconsistent_value_map) {
    Prepare();

    input_array_column_->append_datum(DatumArray{1L, 2L, 3L});
    default_column_->append_datum(kNullDatum);

    auto old_array = DatumArray{1L, 2L};
    auto new_array = DatumArray{10L, 20L, 30L};

    const auto result = RunConstantValueMap(old_array, new_array, true);
    EXPECT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ("[prepare] old value array must have the same length as new value array.", result.status().message());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_non_const_inconsistent_value_map) {
    Prepare();

    AddRow(DatumArray{1L, 2L}, DatumArray{1L, 2L}, DatumArray{10L}, 0L);

    const auto result = Run(true);
    EXPECT_TRUE(result.status().is_invalid_argument());
    EXPECT_EQ("old value array must have the same length as new value array.", result.status().message());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_empty_input_column) {
    Prepare();
    const auto result = Run(true).value();
    ASSERT_EQ(0, result->size());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_basic_mapping_with_default) {
    Prepare();

    input_array_column_->append_datum(DatumArray{1L, 2L, 3L, 4L});
    input_array_column_->append_datum(DatumArray{5L, 1L, 2L});
    input_array_column_->append_datum(DatumArray{});

    default_column_->append_datum(0L);
    default_column_->append_datum(kNullDatum);
    default_column_->append_datum(999L);

    auto old_array = DatumArray{1L, 2L};
    auto new_array = DatumArray{10L, 20L};

    const auto result = RunConstantValueMap(old_array, new_array, true).value();
    ASSERT_EQ(3, result->size());

    // First array: [1, 2, 3, 4] -> [10, 20, 0, 0] (3 and 4 use default 0)
    ASSERT_EQ(4, result->get(0).get_array().size());
    EXPECT_EQ(10L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(20L, result->get(0).get_array()[1].get_int64());
    EXPECT_EQ(0L, result->get(0).get_array()[2].get_int64());
    EXPECT_EQ(0L, result->get(0).get_array()[3].get_int64());

    // Second array: [5, 1, 2] -> [null, 10, 20] (5 uses default null)
    ASSERT_EQ(3, result->get(1).get_array().size());
    EXPECT_TRUE(result->get(1).get_array()[0].is_null());
    EXPECT_EQ(10L, result->get(1).get_array()[1].get_int64());
    EXPECT_EQ(20L, result->get(1).get_array()[2].get_int64());

    // Third array: [] -> []
    ASSERT_EQ(0, result->get(2).get_array().size());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_basic_mapping_without_default) {
    Prepare();

    input_array_column_->append_datum(DatumArray{1L, 2L, 3L, 4L});
    input_array_column_->append_datum(DatumArray{5L, 1L, 2L});

    auto old_array = DatumArray{1L, 2L};
    auto new_array = DatumArray{10L, 20L};

    const auto result = RunConstantValueMap(old_array, new_array, false).value();
    ASSERT_EQ(2, result->size());

    // First array: [1, 2, 3, 4] -> [10, 20, 3, 4] (3 and 4 keep original values)
    ASSERT_EQ(4, result->get(0).get_array().size());
    EXPECT_EQ(10L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(20L, result->get(0).get_array()[1].get_int64());
    EXPECT_EQ(3L, result->get(0).get_array()[2].get_int64());
    EXPECT_EQ(4L, result->get(0).get_array()[3].get_int64());

    // Second array: [5, 1, 2] -> [5, 10, 20] (5 keeps original value)
    ASSERT_EQ(3, result->get(1).get_array().size());
    EXPECT_EQ(5L, result->get(1).get_array()[0].get_int64());
    EXPECT_EQ(10L, result->get(1).get_array()[1].get_int64());
    EXPECT_EQ(20L, result->get(1).get_array()[2].get_int64());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_null_handling_with_default) {
    Prepare();

    input_array_column_->append_datum(DatumArray{1L, kNullDatum, 3L});
    input_array_column_->append_datum(DatumArray{kNullDatum, 2L});

    default_column_->append_datum(0L);
    default_column_->append_datum(kNullDatum);

    auto old_array = DatumArray{1L, kNullDatum};
    auto new_array = DatumArray{10L, 999L};

    const auto result = RunConstantValueMap(old_array, new_array, true).value();
    ASSERT_EQ(2, result->size());

    // First array: [1, null, 3] -> [10, 999, 0] (null maps to 999, 3 uses default 0)
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(10L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(999L, result->get(0).get_array()[1].get_int64());
    EXPECT_EQ(0L, result->get(0).get_array()[2].get_int64());

    // Second array: [null, 2] -> [999, null] (null maps to 999, 2 uses default null)
    ASSERT_EQ(2, result->get(1).get_array().size());
    EXPECT_EQ(999L, result->get(1).get_array()[0].get_int64());
    EXPECT_TRUE(result->get(1).get_array()[1].is_null());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_null_handling_without_default) {
    Prepare();

    input_array_column_->append_datum(DatumArray{1L, kNullDatum, 3L});

    auto old_array = DatumArray{1L, kNullDatum};
    auto new_array = DatumArray{10L, 999L};

    const auto result = RunConstantValueMap(old_array, new_array, false).value();
    ASSERT_EQ(1, result->size());

    // Array: [1, null, 3] -> [10, 999, 3] (null maps to 999, 3 keeps original)
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(10L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(999L, result->get(0).get_array()[1].get_int64());
    EXPECT_EQ(3L, result->get(0).get_array()[2].get_int64());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_empty_value_map_with_default) {
    Prepare();

    input_array_column_->append_datum(DatumArray{1L, 2L, 3L});
    default_column_->append_datum(999L);

    auto old_array = DatumArray{};
    auto new_array = DatumArray{};

    const auto result = RunConstantValueMap(old_array, new_array, true).value();
    ASSERT_EQ(1, result->size());

    // Array: [1, 2, 3] -> [999, 999, 999] (all use default)
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(999L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(999L, result->get(0).get_array()[1].get_int64());
    EXPECT_EQ(999L, result->get(0).get_array()[2].get_int64());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_empty_value_map_without_default) {
    Prepare();

    input_array_column_->append_datum(DatumArray{1L, 2L, 3L});

    auto old_array = DatumArray{};
    auto new_array = DatumArray{};

    const auto result = RunConstantValueMap(old_array, new_array, false).value();
    ASSERT_EQ(1, result->size());

    // Array: [1, 2, 3] -> [1, 2, 3] (all keep original values)
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(1L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(2L, result->get(0).get_array()[1].get_int64());
    EXPECT_EQ(3L, result->get(0).get_array()[2].get_int64());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_overwrite_mapping) {
    Prepare();

    input_array_column_->append_datum(DatumArray{1L, 2L});
    default_column_->append_datum(0L);

    // Same key appears multiple times - should use last mapping
    auto old_array = DatumArray{1L, 2L, 1L};
    auto new_array = DatumArray{100L, 200L, 999L};

    const auto result = RunConstantValueMap(old_array, new_array, true).value();
    ASSERT_EQ(1, result->size());

    // Array: [1, 2] -> [999, 200] (1 maps to 999 due to overwrite, 2 maps to 200)
    ASSERT_EQ(2, result->get(0).get_array().size());
    EXPECT_EQ(999L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(200L, result->get(0).get_array()[1].get_int64());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_non_constant_mapping) {
    Prepare();

    AddRow(DatumArray{1L, 2L, 3L}, DatumArray{1L, 2L}, DatumArray{10L, 20L}, 0L);
    AddRow(DatumArray{2L, 3L, 4L}, DatumArray{2L, 3L}, DatumArray{200L, 300L}, kNullDatum);

    const auto result = Run(true).value();
    ASSERT_EQ(2, result->size());

    // First array: [1, 2, 3] with mapping {1->10, 2->20} and default 0
    // Result: [10, 20, 0]
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(10L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(20L, result->get(0).get_array()[1].get_int64());
    EXPECT_EQ(0L, result->get(0).get_array()[2].get_int64());

    // Second array: [2, 3, 4] with mapping {2->200, 3->300} and default null
    // Result: [200, 300, null]
    ASSERT_EQ(3, result->get(1).get_array().size());
    EXPECT_EQ(200L, result->get(1).get_array()[0].get_int64());
    EXPECT_EQ(300L, result->get(1).get_array()[1].get_int64());
    EXPECT_TRUE(result->get(1).get_array()[2].is_null());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_non_constant_mapping_overwrite) {
    Prepare();

    AddRow(DatumArray{1L, 2L}, DatumArray{1L, 2L, 1L}, DatumArray{100L, 200L, 999L}, 0L);

    const auto result = Run(true).value();
    ASSERT_EQ(1, result->size());

    // Array: [1, 2] with mapping {1->100, 2->200, 1->999} (last 1 mapping wins)
    // Result: [999, 200]
    ASSERT_EQ(2, result->get(0).get_array().size());
    EXPECT_EQ(999L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(200L, result->get(0).get_array()[1].get_int64());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_null_input_array) {
    Prepare();

    input_array_column_->append_datum(kNullDatum);
    input_array_column_->append_datum(DatumArray{1L, 2L});
    default_column_->append_datum(0L);
    default_column_->append_datum(0L);

    auto old_array = DatumArray{1L, 2L};
    auto new_array = DatumArray{10L, 20L};

    const auto result = RunConstantValueMap(old_array, new_array, true).value();
    ASSERT_EQ(2, result->size());

    // First row: null input array -> null result
    EXPECT_TRUE(result->is_null(0));

    // Second row: [1, 2] -> [10, 20]
    ASSERT_EQ(2, result->get(1).get_array().size());
    EXPECT_EQ(10L, result->get(1).get_array()[0].get_int64());
    EXPECT_EQ(20L, result->get(1).get_array()[1].get_int64());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_null_in_new_value_array) {
    Prepare();

    input_array_column_->append_datum(DatumArray{1L, 2L, 3L});
    default_column_->append_datum(0L);

    auto old_array = DatumArray{1L, 2L};
    auto new_array = DatumArray{10L, kNullDatum};

    const auto result = RunConstantValueMap(old_array, new_array, true).value();
    ASSERT_EQ(1, result->size());

    // Array: [1, 2, 3] with mapping {1->10, 2->null} and default 0
    // Result: [10, null, 0]
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(10L, result->get(0).get_array()[0].get_int64());
    EXPECT_TRUE(result->get(0).get_array()[1].is_null());
    EXPECT_EQ(0L, result->get(0).get_array()[2].get_int64());
}

TEST_F(CelonisRemapIntArrayTest, remap_int_array_const_input_array) {
    Prepare();

    input_array_column_->append_datum(DatumArray{1L, 2L, 3L});
    input_array_column_ = ConstColumn::create(input_array_column_, 2);
    default_column_->append_datum(0L);
    default_column_->append_datum(999L);

    auto old_array = DatumArray{1L, 2L};
    auto new_array = DatumArray{10L, 20L};

    const auto result = RunConstantValueMap(old_array, new_array, true).value();
    ASSERT_EQ(2, result->size());

    // Both rows should have same input array [1, 2, 3] but different defaults
    // First row: [1, 2, 3] -> [10, 20, 0]
    ASSERT_EQ(3, result->get(0).get_array().size());
    EXPECT_EQ(10L, result->get(0).get_array()[0].get_int64());
    EXPECT_EQ(20L, result->get(0).get_array()[1].get_int64());
    EXPECT_EQ(0L, result->get(0).get_array()[2].get_int64());

    // Second row: [1, 2, 3] -> [10, 20, 999]
    ASSERT_EQ(3, result->get(1).get_array().size());
    EXPECT_EQ(10L, result->get(1).get_array()[0].get_int64());
    EXPECT_EQ(20L, result->get(1).get_array()[1].get_int64());
    EXPECT_EQ(999L, result->get(1).get_array()[2].get_int64());
}

} // namespace starrocks
