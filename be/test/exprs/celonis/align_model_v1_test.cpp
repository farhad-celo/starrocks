#include "exprs/celonis/align_model_v1.h"

#include <gtest/gtest.h>

#include <random>
#include <thread>

#include "bpmn_models.h"
#include "column/array_column.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "runtime/mem_pool.h"
#include "testutil/assert.h"
#include "testutil/function_utils.h"
#include "util.h"
#include "util/defer_op.h"
#include "util/slice.h"
namespace starrocks {

class CelonisAlignModelTest : public testing::Test {
protected:
    CelonisAlignModelTest()
            : arg_types_{{TypeDescriptor::from_logical_type(TYPE_ARRAY),
                          TypeDescriptor::from_logical_type(TYPE_VARCHAR)}},
              return_type_(TypeDescriptor::from_logical_type(TYPE_STRUCT)) {
        // Initialize the struct type descriptor properly
        auto struct_desc = TypeDescriptor::from_logical_type(TYPE_STRUCT);
        struct_desc.children = {celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_BIGINT),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_BIGINT),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_VARCHAR)};
        struct_desc.field_names = {"alignment_model_vertex_id",
                                   "alignment_vertex_label",
                                   "alignment_move_type",
                                   "alignment_activity_index",
                                   "association_edge_class",
                                   "association_alignment_index",
                                   "edge_class_id",
                                   "edge_class_type"};
        return_type_ = struct_desc;
    }

    void SetUp() override {}

    void TearDown() override {}

private:
    typedef std::vector<std::string> Variant;
    typedef std::vector<Variant> VariantRows;
    typedef std::tuple<std::vector<int64_t>, std::vector<std::string>, std::vector<std::string>, std::vector<int64_t>,
                       std::vector<int64_t>, std::vector<int64_t>, std::vector<int64_t>, std::vector<std::string>>
            Result;
    typedef std::map<Variant, Result> ResultMap;

    static const ResultMap PARALLEL_MODEL_RESULTS;
    static const ResultMap LOOP_MODEL_RESULTS;

    FunctionContext::TypeDesc TYPEDESC_ARRAY_VARCHAR = celonis::array_type(TYPE_VARCHAR);
    FunctionContext::TypeDesc TYPEDESC_ARRAY_BIGINT = celonis::array_type(TYPE_BIGINT);

    class Evaluator {
    public:
        Evaluator(const StructColumn& result, const std::vector<Result>& expected,
                  const FunctionContext::TypeDesc& return_type)
                : result_(result), expected_(expected), return_type_(return_type) {}

        void evaluate() {
            ASSERT_EQ(result_.size(), expected_.size());
            for (int row = 0; row < result_.size(); ++row) {
                compare_array<0, int64_t>(row);
                compare_array<1, std::string>(row);
                compare_array<2, std::string>(row);
                compare_array<3, int64_t>(row);
                compare_array<4, int64_t>(row);
                compare_array<5, int64_t>(row);
                compare_array<6, int64_t>(row);
                compare_array<7, std::string>(row);
            }
        }

    private:
        template <int field, typename TYPE>
        void compare_array(int row) {
            if (result_.fields()[field]->get(row).is_null()) {
                EXPECT_TRUE(std::get<field>(expected_[row]).empty());
                return;
            }
            auto result_array = result_.fields()[field]->get(row).get_array();
            const auto& expected_array = std::get<field>(expected_[row]);
            ASSERT_EQ(result_array.size(), expected_array.size());
            for (int i = 0; i < result_array.size(); i++) {
                if constexpr (std::is_same_v<TYPE, std::string>) {
                    EXPECT_EQ(result_array[i].get_slice(), expected_array[i])
                            << "row: " << row << ", field: " << return_type_.field_names[field] << ", element: " << i;
                } else if constexpr (std::is_same_v<TYPE, int64_t>) {
                    EXPECT_EQ(result_array[i].get_int64(), expected_array[i])
                            << "row: " << row << ", field: " << return_type_.field_names[field] << ", element: " << i;
                } else {
                    static_assert("Invalid type");
                }
            }
        }

        const StructColumn& result_;
        const std::vector<Result>& expected_;
        const FunctionContext::TypeDesc& return_type_;
    };

    ColumnPtr build_variant_column(const std::vector<std::vector<std::string>>& rows) {
        auto array_column = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), /*nullable=*/true);
        for (int i = 0; i < rows.size(); i++) {
            if (rows[i].size() == 0) {
                array_column->append_nulls(1);
                continue;
            }
            DatumArray array;
            for (int j = 0; j < rows[i].size(); j++) {
                if (rows[i][j] == "null") {
                    array.push_back(kNullDatum);
                } else {
                    array.emplace_back(Slice(rows[i][j]));
                }
            }
            array_column->append_datum(array);
        }
        return array_column;
    }

    void Run(const VariantRows& variant_rows, const std::string& bpmn_model_description_json,
             const std::vector<Result>& expected) {
        auto variants = build_variant_column(variant_rows);
        auto model_column =
                ColumnHelper::create_const_column<TYPE_VARCHAR>(bpmn_model_description_json, variant_rows.size());

        Columns columns;
        columns.push_back(variants);
        columns.push_back(model_column);

        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types_), return_type_));
        ctx->set_constant_columns(columns);

        DeferOp close_fragment_local(
                [&ctx] { CelonisAlignModel::align_model_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL); });
        ASSERT_OK(
                CelonisAlignModel::align_model_prepare(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        DeferOp close_thread_local(
                [&ctx] { CelonisAlignModel::align_model_close(ctx.get(), FunctionContext::THREAD_LOCAL); });
        ASSERT_OK(CelonisAlignModel::align_model_prepare(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));

        const auto result = CelonisAlignModel::align_model(ctx.get(), columns).value();
        ASSERT_TRUE(result->is_struct());
        const StructColumn* st = down_cast<const StructColumn*>(result.get());
        Evaluator evaluator(*st, expected, return_type_);
        evaluator.evaluate();
    }

    std::vector<FunctionContext::TypeDesc> arg_types_;
    FunctionContext::TypeDesc return_type_;
};

const CelonisAlignModelTest::ResultMap CelonisAlignModelTest::PARALLEL_MODEL_RESULTS = {
        {{"A", "C"},
         {

                 // alignment
                 {0, 1, 2, 4, 3, 5, 6},
                 {"BPMN_START", "A", "BPMN_PARALLEL", "C", "B", "BPMN_PARALLEL", "BPMN_END"},
                 {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE",
                  "GATEWAY_MOVE"},
                 {0, 0, 0, 1, 0, 1, 1},
                 // association
                 {0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 3},
                 {0, 1, 2, 3, 5, 6, 2, 4, 5, 2, 5, 1, 4, 6},
                 // edge_class
                 {0, 1, 2, 3},
                 {"SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "L1_MISSING"}}},
        {
                {"A", "B", "C"},
                {{0, 1, 2, 3, 4, 5, 6},
                 {"BPMN_START", "A", "BPMN_PARALLEL", "B", "C", "BPMN_PARALLEL", "BPMN_END"},
                 {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
                  "GATEWAY_MOVE"},
                 {0, 0, 0, 1, 2, 2, 2},
                 {0, 0, 0, 0, 0, 0, 1, 1, 1},
                 {0, 1, 2, 3, 5, 6, 2, 4, 5},
                 {0, 1},
                 {"SYNC_EDGE", "SYNC_EDGE"}},
        },
        {{"C", "B", "B"},
         {{0, 1, 2, 4, 3, 3, 5, 6},
          {"BPMN_START", "A", "BPMN_PARALLEL", "C", "B", "B", "BPMN_PARALLEL", "BPMN_END"},
          {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "LOG_MOVE", "GATEWAY_MOVE",
           "GATEWAY_MOVE"},
          {0, 0, 0, 0, 1, 2, 1, 2},
          {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5},
          {2, 3, 6, 7, 2, 4, 6, 0, 1, 2, 0, 2, 4, 5, 7, 0, 1, 3},
          {0, 1, 2, 3, 4, 5},
          {"SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "LOG_EDGE", "L1_MISSING"}}},
        {{"C", "B", "null", "B"},
         {{0, 1, 2, 4, 3, 3, 5, 6},
          {"BPMN_START", "A", "BPMN_PARALLEL", "C", "B", "B", "BPMN_PARALLEL", "BPMN_END"},
          {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "LOG_MOVE", "GATEWAY_MOVE",
           "GATEWAY_MOVE"},
          {0, 0, 0, 0, 1, 3, 1, 3},
          {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5},
          {2, 3, 6, 7, 2, 4, 6, 0, 1, 2, 0, 2, 4, 5, 7, 0, 1, 3},
          {0, 1, 2, 3, 4, 5},
          {"SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "LOG_EDGE", "L1_MISSING"}}},
        {{"A", "null", "null", "C"},
         {// alignment
          {0, 1, 2, 4, 3, 5, 6},
          {"BPMN_START", "A", "BPMN_PARALLEL", "C", "B", "BPMN_PARALLEL", "BPMN_END"},
          {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {0, 0, 0, 3, 0, 3, 3},
          // association
          {0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 3},
          {0, 1, 2, 3, 5, 6, 2, 4, 5, 2, 5, 1, 4, 6},
          // edge_class
          {0, 1, 2, 3},
          {"SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "L1_MISSING"}}},
        {{"null", "A", "null", "C", "null"},
         {// alignment
          {0, 1, 2, 4, 3, 5, 6},
          {"BPMN_START", "A", "BPMN_PARALLEL", "C", "B", "BPMN_PARALLEL", "BPMN_END"},
          {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {1, 1, 1, 3, 1, 3, 3},
          // association
          {0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 3},
          {0, 1, 2, 3, 5, 6, 2, 4, 5, 2, 5, 1, 4, 6},
          // edge_class
          {0, 1, 2, 3},
          {"SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "L1_MISSING"}}}};

const CelonisAlignModelTest::ResultMap CelonisAlignModelTest::LOOP_MODEL_RESULTS = {
        {{"A", "B", "C", "A", "B"},
         {{0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6},
          {"BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE", "C", "BPMN_EXCLUSIVE_CHOICE", "A",
           "B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
          {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
           "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {0, 0, 0, 1, 1, 2, 2, 3, 4, 4, 4},
          {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
          {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
          {0},
          {"SYNC_EDGE"}}},
        {{"A", "B", "C"},
         {{0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6},
          {"BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE", "C", "BPMN_EXCLUSIVE_CHOICE", "A",
           "B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
          {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
           "MODEL_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2},
          {0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 4, 4, 4, 4},
          {0, 1, 2, 3, 4, 5, 6, 9, 10, 6, 7, 8, 9, 6, 9, 5, 7, 8, 10},
          {0, 1, 2, 3, 4},
          {"SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "L1_MISSING"}}},
        {{"A", "B", "A", "B"},
         {{0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6},
          {"BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE", "C", "BPMN_EXCLUSIVE_CHOICE", "A",
           "B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
          {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE",
           "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {0, 0, 0, 1, 1, 1, 1, 2, 3, 3, 3},
          {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4},
          {0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 4, 5, 6, 4, 6, 3, 5, 7},
          {0, 1, 2, 3, 4},
          {"SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "L1_MISSING"}}}};

TEST_F(CelonisAlignModelTest, Parallel) {
    VariantRows variants = {{"A", "C"}, {"A", "B", "C"}, {"C", "B", "null", "B"}};
    std::vector<Result> expected = {PARALLEL_MODEL_RESULTS.at(variants[0]), PARALLEL_MODEL_RESULTS.at(variants[1]),
                                    PARALLEL_MODEL_RESULTS.at(variants[2])};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisAlignModelTest, Loop) {
    VariantRows variants = {{"A", "B", "C", "A", "B"}, {"A", "B", "C"}, {"A", "B", "A", "B"}};
    std::vector<Result> expected = {LOOP_MODEL_RESULTS.at(variants[0]), LOOP_MODEL_RESULTS.at(variants[1]),
                                    LOOP_MODEL_RESULTS.at(variants[2])};
    Run(variants, LOOP_MODEL, expected);
}

TEST_F(CelonisAlignModelTest, Parallel_DuplicatedVariants) {
    VariantRows variants = {{"A", "C"}, {"A", "B", "C"}, {"A", "C"}, {"C", "B", "null", "B"}};
    std::vector<Result> expected = {PARALLEL_MODEL_RESULTS.at(variants[0]), PARALLEL_MODEL_RESULTS.at(variants[1]),
                                    PARALLEL_MODEL_RESULTS.at(variants[2]), PARALLEL_MODEL_RESULTS.at(variants[3])};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisAlignModelTest, Parallel_NULL) {
    VariantRows variants = {{"A", "C"},
                            {"A", "B", "C"},
                            {},
                            {"null", "null"},
                            {"C", "B", "B"},
                            {"C", "B", "null", "B"},
                            {"A", "null", "null", "C"},
                            {"null", "A", "null", "C", "null"},
                            {"A", "null", "null", "C"},
                            {}};
    std::vector<Result> expected = {PARALLEL_MODEL_RESULTS.at({"A", "C"}),
                                    PARALLEL_MODEL_RESULTS.at({"A", "B", "C"}),
                                    {},
                                    {},
                                    PARALLEL_MODEL_RESULTS.at({"C", "B", "B"}),
                                    PARALLEL_MODEL_RESULTS.at({"C", "B", "null", "B"}),
                                    PARALLEL_MODEL_RESULTS.at({"A", "null", "null", "C"}),
                                    PARALLEL_MODEL_RESULTS.at({"null", "A", "null", "C", "null"}),
                                    PARALLEL_MODEL_RESULTS.at({"A", "null", "null", "C"}),
                                    {}};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisAlignModelTest, Parallel_ONLYNULL) {
    VariantRows variants = {{}, {}};
    std::vector<Result> expected = {{}, {}};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisAlignModelTest, InvalidModel) {
    auto variants = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), /*nullable=*/true);
    variants->append_nulls(1);
    auto model_column = ColumnHelper::create_const_column<TYPE_VARCHAR>("Invalid JSON", 1);

    Columns columns;
    columns.push_back(variants);
    columns.push_back(model_column);

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types_), return_type_));
    ctx->set_constant_columns(columns);

    ASSERT_OK(CelonisAlignModel::align_model_prepare(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    ASSERT_OK(CelonisAlignModel::align_model_prepare(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));

    const auto result = CelonisAlignModel::align_model(ctx.get(), columns);
    ASSERT_FALSE(result.ok());
    EXPECT_TRUE(result.status().is_invalid_argument());

    ASSERT_OK(CelonisAlignModel::align_model_close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
    ASSERT_OK(CelonisAlignModel::align_model_close(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
}

TEST_F(CelonisAlignModelTest, LongVariant) {
    // Test that variants longer than 40000 activities are now supported
    // Previously this would throw an InternalError
    std::vector<std::string> long_variant;
    long_variant.reserve(40005);

    // Create a variant with 40005 activities
    for (int i = 0; i < 40005; i++) {
        long_variant.push_back("A");
    }

    VariantRows variants = {long_variant};

    auto array_column = build_variant_column(variants);
    auto model_column = ColumnHelper::create_const_column<TYPE_VARCHAR>(PARALLEL_MODEL, 1);

    Columns columns;
    columns.push_back(array_column);
    columns.push_back(model_column);

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types_), return_type_));
    ctx->set_constant_columns(columns);

    ASSERT_OK(CelonisAlignModel::align_model_prepare(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    ASSERT_OK(CelonisAlignModel::align_model_prepare(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));

    // This should not throw an InternalError about variants longer than 40000
    const auto result = CelonisAlignModel::align_model(ctx.get(), columns);
    ASSERT_TRUE(result.ok()) << "Expected variants longer than 40000 to be supported after CPML fix, but got error: "
                             << result.status().message();

    ASSERT_OK(CelonisAlignModel::align_model_close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
    ASSERT_OK(CelonisAlignModel::align_model_close(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
}

TEST_F(CelonisAlignModelTest, Concurrency) {
    int num_inputs = 100;
    int num_threads = 1000;

    const std::vector<std::string> models{PARALLEL_MODEL, LOOP_MODEL};
    std::vector<ResultMap::const_iterator> results[2];
    for (auto it = PARALLEL_MODEL_RESULTS.cbegin(); it != PARALLEL_MODEL_RESULTS.cend(); it++) {
        results[0].push_back(it);
    }
    for (auto it = LOOP_MODEL_RESULTS.cbegin(); it != LOOP_MODEL_RESULTS.cend(); it++) {
        results[1].push_back(it);
    }

    struct Input {
        Input(const std::string& model, VariantRows variants, std::vector<Result> expected)
                : model(model), variants(std::move(variants)), expected(std::move(expected)) {}

        const std::string& model;
        VariantRows variants;
        std::vector<Result> expected;
    };

    std::vector<Input> inputs;
    inputs.reserve(num_threads);

    std::random_device rd;
    std::uniform_int_distribution<size_t> model_d(0, 1);
    std::uniform_int_distribution<size_t> num_variants_d(1, 20);

    for (int i = 0; i < num_inputs; i++) {
        int model = model_d(rd);
        VariantRows variants;
        std::vector<Result> expected;
        int num_variants = num_variants_d(rd);
        std::uniform_int_distribution<size_t> variant_d(0, results[model].size() - 1);
        for (int j = 0; j < num_variants; j++) {
            int variant = variant_d(rd);
            variants.push_back(results[model][variant]->first);
            expected.push_back(results[model][variant]->second);
        }
        inputs.emplace_back(models[model], std::move(variants), std::move(expected));
    }

    std::uniform_int_distribution<size_t> input_d(0, num_inputs - 1);
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        int id = input_d(rd);
        threads.emplace_back([&](const Input& input) { Run(input.variants, input.model, input.expected); }, inputs[id]);
    }
    for (auto& t : threads) {
        t.join();
    }
}

} // namespace starrocks