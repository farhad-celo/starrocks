#include "exprs/celonis/patindex.h"

#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/function_context.h"
#include "util/defer_op.h"

namespace starrocks {

class CelonisPatindexTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    void Prepare() {
        std::vector<FunctionContext::TypeDesc> arg_types = {TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                                                            TypeDescriptor::from_logical_type(TYPE_VARCHAR),
                                                            TypeDescriptor::from_logical_type(TYPE_BIGINT)};
        auto return_type = TypeDescriptor::from_logical_type(TYPE_BIGINT);
        ctx_.reset(FunctionContext::create_test_context(std::move(arg_types), return_type));

        string_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        pattern_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
        occurrence_column_ = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), true);
    }

    StatusOr<ColumnPtr> Run(bool without_occurrence = false) {
        DeferOp close_fragment_local([this] { CelonisPatindex::close(ctx_.get(), FunctionContext::FRAGMENT_LOCAL); });
        RETURN_IF_ERROR(CelonisPatindex::prepare(ctx_.get(), FunctionContext::FRAGMENT_LOCAL));
        DeferOp close_thread_local([this] { CelonisPatindex::close(ctx_.get(), FunctionContext::THREAD_LOCAL); });
        RETURN_IF_ERROR(CelonisPatindex::prepare(ctx_.get(), FunctionContext::THREAD_LOCAL));
        StatusOr<ColumnPtr> result;
        if (without_occurrence) {
            result = CelonisPatindex::patindex(ctx_.get(), {string_column_, pattern_column_});
        } else {
            result = CelonisPatindex::patindex(ctx_.get(), {string_column_, pattern_column_, occurrence_column_});
        }
        return result;
    }

    StatusOr<ColumnPtr> RunConstantPattern(const std::optional<std::string>& pattern, bool without_occurrence = false) {
        if (pattern.has_value()) {
            pattern_column_->append_datum(pattern.value().c_str());
        } else {
            pattern_column_->append_datum(kNullDatum);
        }
        pattern_column_ = ConstColumn::create(pattern_column_, string_column_->size());
        if (without_occurrence) {
            ctx_->set_constant_columns({nullptr, pattern_column_});
        } else {
            ctx_->set_constant_columns({nullptr, pattern_column_, nullptr});
        }
        return Run(without_occurrence);
    }

    std::unique_ptr<FunctionContext> ctx_;
    ColumnPtr string_column_;
    ColumnPtr pattern_column_;
    ColumnPtr occurrence_column_;
};

TEST_F(CelonisPatindexTest, empty_input) {
    Prepare();
    const auto result = Run().value();
    ASSERT_TRUE(result->empty());
}

TEST_F(CelonisPatindexTest, const_pattern) {
    {
        Prepare();
        std::string pattern = "%like%";
        string_column_->append_datum("I like database systems");
        string_column_->append_datum(kNullDatum);
        string_column_->append_datum("Moves like Jagger");
        string_column_->append_datum("Process mining is awesome");
        string_column_->append_datum("");
        string_column_->append_datum("Do you also like PQL?");
        for (auto i = 0; i < string_column_->size(); ++i) {
            occurrence_column_->append_datum(1L);
        }
        const auto result = RunConstantPattern(pattern).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(3L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(7L, result->get(2).get_int64());
        EXPECT_EQ(0L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(13L, result->get(5).get_int64());
    }
    {
        Prepare();
        std::string pattern = "%da__base%syst_%";
        string_column_->append_datum("I like database systems");
        string_column_->append_datum(kNullDatum);
        for (auto i = 0; i < string_column_->size(); ++i) {
            occurrence_column_->append_datum(1L);
        }
        const auto result = RunConstantPattern(pattern).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(8L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisPatindexTest, const_pattern_without_occurrence) {
    Prepare();
    std::string pattern = "%like%";
    string_column_->append_datum("I like database systems");
    string_column_->append_datum(kNullDatum);
    string_column_->append_datum("Moves like Jagger");
    string_column_->append_datum("Process mining is awesome");
    string_column_->append_datum("");
    string_column_->append_datum("Do you also like PQL?");
    const auto result = RunConstantPattern(pattern, true).value();
    ASSERT_EQ(string_column_->size(), result->size());
    EXPECT_EQ(3L, result->get(0).get_int64());
    EXPECT_TRUE(result->get(1).is_null());
    EXPECT_EQ(7L, result->get(2).get_int64());
    EXPECT_EQ(0L, result->get(3).get_int64());
    EXPECT_EQ(0L, result->get(4).get_int64());
    EXPECT_EQ(13L, result->get(5).get_int64());
}

TEST_F(CelonisPatindexTest, const_pattern_with_nonone_occurrence) {
    {
        Prepare();
        std::string pattern = "%like%";
        string_column_->append_datum("I like database systems");
        string_column_->append_datum(kNullDatum);
        string_column_->append_datum("Moves like Jagger");
        string_column_->append_datum("Process mining is awesome");
        string_column_->append_datum("");
        string_column_->append_datum("Do you also like PQL, like, really, like something like");
        for (auto i = 0; i < string_column_->size(); ++i) {
            occurrence_column_->append_datum(2L);
        }
        const auto result = RunConstantPattern(pattern).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(0L, result->get(2).get_int64());
        EXPECT_EQ(0L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(23L, result->get(5).get_int64());
    }
    {
        Prepare();
        std::string pattern = "%like%";
        string_column_->append_datum("I like database systems");
        string_column_->append_datum(kNullDatum);
        string_column_->append_datum("Moves like Jagger");
        string_column_->append_datum("Process mining is awesome");
        string_column_->append_datum("");
        string_column_->append_datum("Do you also like PQL, like, really, like something like");
        for (auto i = 0; i < string_column_->size(); ++i) {
            occurrence_column_->append_datum(4L);
        }
        const auto result = RunConstantPattern(pattern).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(0L, result->get(2).get_int64());
        EXPECT_EQ(0L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(52L, result->get(5).get_int64());
    }
    {
        Prepare();
        std::string pattern = "%like%";
        string_column_->append_datum("I like database systems");
        string_column_->append_datum(kNullDatum);
        string_column_->append_datum("Moves like Jagger");
        string_column_->append_datum("Process mining is awesome");
        string_column_->append_datum("");
        string_column_->append_datum("Do you also like PQL, like, really, like something like");
        for (auto i = 0; i < string_column_->size(); ++i) {
            occurrence_column_->append_datum(5L);
        }
        const auto result = RunConstantPattern(pattern).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(0L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(0L, result->get(2).get_int64());
        EXPECT_EQ(0L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(0L, result->get(5).get_int64());
    }
    {
        Prepare();
        std::string pattern = "%BB%";
        string_column_->append_datum("BaaBBaaaBBBBaaBB");
        occurrence_column_->append_datum(3L);
        const auto result = RunConstantPattern(pattern).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(10L, result->get(0).get_int64());
    }
}

TEST_F(CelonisPatindexTest, non_const_pattern_without_occurrence) {
    {
        Prepare();
        string_column_->append_datum("I like database systems");
        string_column_->append_datum(kNullDatum);
        string_column_->append_datum("Moves like Jagger");
        string_column_->append_datum("Process mining is awesome");
        string_column_->append_datum("");
        string_column_->append_datum("Do you also like PQL?");
        for (auto i = 0; i < string_column_->size(); ++i) {
            pattern_column_->append_datum("%like%");
        }
        const auto result = Run(true).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(3L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(7L, result->get(2).get_int64());
        EXPECT_EQ(0L, result->get(3).get_int64());
        EXPECT_EQ(0L, result->get(4).get_int64());
        EXPECT_EQ(13L, result->get(5).get_int64());
    }
    {
        Prepare();
        string_column_->append_datum("I like database systems");
        string_column_->append_datum(kNullDatum);
        for (auto i = 0; i < string_column_->size(); ++i) {
            pattern_column_->append_datum("%da__base%syst_%");
        }
        const auto result = Run(true).value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(8L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
    }
}

TEST_F(CelonisPatindexTest, non_const_pattern) {
    {
        Prepare();
        string_column_->append_datum("I like database systems");
        string_column_->append_datum(kNullDatum);
        string_column_->append_datum("Moves like Jagger");
        string_column_->append_datum("Process mining is awesome");
        string_column_->append_datum("");
        string_column_->append_datum("Do you also like PQL?");
        string_column_->append_datum("app");
        string_column_->append_datum("I l%ke d_t_bases, I l%ke d_t_bases");

        pattern_column_->append_datum("%database%");
        pattern_column_->append_datum("null");
        pattern_column_->append_datum("%gg%");
        pattern_column_->append_datum("%_mining_%");
        pattern_column_->append_datum("");
        pattern_column_->append_datum("%SQL%");
        pattern_column_->append_datum("app");
        pattern_column_->append_datum("%l\\%%d\\_t\\_bas_s");
        for (auto i = 0; i < string_column_->size() - 1; ++i) {
            occurrence_column_->append_datum(1L);
        }
        occurrence_column_->append_datum(2L);
        const auto result = Run().value();
        ASSERT_EQ(string_column_->size(), result->size());
        EXPECT_EQ(8L, result->get(0).get_int64());
        EXPECT_TRUE(result->get(1).is_null());
        EXPECT_EQ(14L, result->get(2).get_int64());
        EXPECT_EQ(8L, result->get(3).get_int64());
        EXPECT_EQ(1L, result->get(4).get_int64());
        EXPECT_EQ(0L, result->get(5).get_int64());
        EXPECT_EQ(1L, result->get(6).get_int64());
        EXPECT_EQ(21L, result->get(7).get_int64());
    }
}

TEST_F(CelonisPatindexTest, null_input) {
    {
        Prepare();
        string_column_->append_datum(kNullDatum);
        const auto result = RunConstantPattern("pattern", true).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        string_column_->append_datum(kNullDatum);
        occurrence_column_->append_datum(1L);
        const auto result = RunConstantPattern("pattern", false).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        string_column_->append_datum("string");
        pattern_column_->append_datum(kNullDatum);
        const auto result = Run(true).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        string_column_->append_datum("string");
        pattern_column_->append_datum(kNullDatum);
        occurrence_column_->append_datum(2L);
        const auto result = Run(false).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        string_column_->append_datum("string");
        pattern_column_->append_datum("pattern");
        occurrence_column_->append_datum(kNullDatum);
        const auto result = Run(false).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
    {
        Prepare();
        const std::string pattern = "%like%";
        string_column_->append_datum("string");
        occurrence_column_->append_datum(kNullDatum);
        const auto result = RunConstantPattern(pattern, false).value();
        ASSERT_EQ(1, result->size());
        EXPECT_TRUE(result->get(0).is_null());
    }
}

} // namespace starrocks
