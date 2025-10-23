#include "exprs/celonis/align_model_v2.h"

#include <gtest/gtest.h>

#include <random>
#include <thread>

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

class CelonisAlignModelV2Test : public testing::Test {
protected:
    CelonisAlignModelV2Test()
            : arg_types_{{AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                          AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))}},
              return_type_(AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_STRUCT))) {
        // Initialize the struct type descriptor properly
        auto struct_desc = TypeDescriptor::from_logical_type(TYPE_STRUCT);
        struct_desc.children = {celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_BIGINT),
                                celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_BIGINT),
                                celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_BIGINT),
                                celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_BIGINT),
                                celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_BIGINT),
                                celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_BIGINT),
                                celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_BIGINT),
                                celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_VARCHAR),
                                celonis::array_type(TYPE_BIGINT),  celonis::array_type(TYPE_BIGINT),
                                celonis::array_type(TYPE_VARCHAR), celonis::array_type(TYPE_VARCHAR)};
        struct_desc.field_names = {"variant",
                                   "alignment_model_vertex_id",
                                   "alignment_vertex_label",
                                   "alignment_move_type",
                                   "alignment_activity_index",
                                   "alignment_deviation_category",
                                   "exclusive_violation_alignment_index",
                                   "exclusive_violation_deviation_category",
                                   "exclusive_violation_edge_class",
                                   "exclusive_violation_model_vertex_id",
                                   "exclusive_violation_model_type",
                                   "exclusive_violation_vertex_label",
                                   "log_edge_alignment_index",
                                   "log_edge_deviation_category",
                                   "log_edge_edge_class",
                                   "log_edge_model_vertex_id",
                                   "log_edge_model_type",
                                   "log_edge_vertex_label",
                                   "missing_violation_alignment_index",
                                   "missing_violation_deviation_category",
                                   "missing_violation_edge_class",
                                   "missing_violation_model_vertex_id",
                                   "missing_violation_model_type",
                                   "missing_violation_vertex_label",
                                   "model_edge_alignment_index",
                                   "model_edge_deviation_category",
                                   "model_edge_edge_class",
                                   "model_edge_model_vertex_id",
                                   "model_edge_model_type",
                                   "model_edge_vertex_label",
                                   "skip_edge_alignment_index",
                                   "skip_edge_deviation_category",
                                   "skip_edge_edge_class",
                                   "skip_edge_model_vertex_id",
                                   "skip_edge_model_type",
                                   "skip_edge_vertex_label",
                                   "sync_edge_alignment_index",
                                   "sync_edge_deviation_category",
                                   "sync_edge_edge_class",
                                   "sync_edge_model_vertex_id",
                                   "sync_edge_model_type",
                                   "sync_edge_vertex_label",
                                   "unmapped_edge_alignment_index",
                                   "unmapped_edge_deviation_category",
                                   "unmapped_edge_edge_class",
                                   "unmapped_edge_model_vertex_id",
                                   "unmapped_edge_model_type",
                                   "unmapped_edge_vertex_label"};
        return_type_ = AnyValUtil::column_type_to_type_desc(struct_desc);
    }

    void SetUp() override {}

    void TearDown() override {}

private:
    typedef std::vector<std::string> Variant;
    typedef std::vector<Variant> VariantRows;
    typedef std::tuple<std::vector<std::string>, std::vector<int64_t>, std::vector<std::string>,
                       std::vector<std::string>, std::vector<int64_t>, std::vector<std::string>, std::vector<int64_t>,
                       std::vector<std::string>, std::vector<int64_t>, std::vector<int64_t>, std::vector<std::string>,
                       std::vector<std::string>, std::vector<int64_t>, std::vector<std::string>, std::vector<int64_t>,
                       std::vector<int64_t>, std::vector<std::string>, std::vector<std::string>, std::vector<int64_t>,
                       std::vector<std::string>, std::vector<int64_t>, std::vector<int64_t>, std::vector<std::string>,
                       std::vector<std::string>, std::vector<int64_t>, std::vector<std::string>, std::vector<int64_t>,
                       std::vector<int64_t>, std::vector<std::string>, std::vector<std::string>, std::vector<int64_t>,
                       std::vector<std::string>, std::vector<int64_t>, std::vector<int64_t>, std::vector<std::string>,
                       std::vector<std::string>, std::vector<int64_t>, std::vector<std::string>, std::vector<int64_t>,
                       std::vector<int64_t>, std::vector<std::string>, std::vector<std::string>, std::vector<int64_t>,
                       std::vector<std::string>, std::vector<int64_t>, std::vector<int64_t>, std::vector<std::string>,
                       std::vector<std::string>>
            Result;
    typedef std::map<Variant, Result> ResultMap;

    static const std::string PARALLEL_MODEL;
    static const std::string LOOP_MODEL;
    static const ResultMap PARALLEL_MODEL_RESULTS;
    static const ResultMap LOOP_MODEL_RESULTS;

    FunctionContext::TypeDesc TYPEDESC_ARRAY_VARCHAR =
            AnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_VARCHAR));
    FunctionContext::TypeDesc TYPEDESC_ARRAY_BIGINT =
            AnyValUtil::column_type_to_type_desc(celonis::array_type(TYPE_BIGINT));

    class Evaluator {
    public:
        Evaluator(const StructColumn& result, const std::vector<Result>& expected,
                  const FunctionContext::TypeDesc& return_type)
                : result_(result), expected_(expected), return_type_(return_type) {}

        void evaluate() {
            if (result_.size() != expected_.size()) {
                return;
            }
            ASSERT_EQ(result_.size(), expected_.size());

            for (int row = 0; row < result_.size(); ++row) {
                compare_array<0, std::string>(row);
                compare_array<1, int64_t>(row);
                compare_array<2, std::string>(row);
                compare_array<3, std::string>(row);
                compare_array<4, int64_t>(row);
                compare_array<5, std::string>(row);

                compare_array<6, int64_t>(row);
                compare_array<7, std::string>(row);
                compare_array<8, int64_t>(row);
                compare_array<9, int64_t>(row);
                compare_array<10, std::string>(row);
                compare_array<11, std::string>(row);

                compare_array<12, int64_t>(row);
                compare_array<13, std::string>(row);
                compare_array<14, int64_t>(row);
                compare_array<15, int64_t>(row);
                compare_array<16, std::string>(row);
                compare_array<17, std::string>(row);

                compare_array<18, int64_t>(row);
                compare_array<19, std::string>(row);
                compare_array<20, int64_t>(row);
                compare_array<21, int64_t>(row);
                compare_array<22, std::string>(row);
                compare_array<23, std::string>(row);

                compare_array<24, int64_t>(row);
                compare_array<25, std::string>(row);
                compare_array<26, int64_t>(row);
                compare_array<27, int64_t>(row);
                compare_array<28, std::string>(row);
                compare_array<29, std::string>(row);

                compare_array<30, int64_t>(row);
                compare_array<31, std::string>(row);
                compare_array<32, int64_t>(row);
                compare_array<33, int64_t>(row);
                compare_array<34, std::string>(row);
                compare_array<35, std::string>(row);

                compare_array<36, int64_t>(row);
                compare_array<37, std::string>(row);
                compare_array<38, int64_t>(row);
                compare_array<39, int64_t>(row);
                compare_array<40, std::string>(row);
                compare_array<41, std::string>(row);

                compare_array<42, int64_t>(row);
                compare_array<43, std::string>(row);
                compare_array<44, int64_t>(row);
                compare_array<45, int64_t>(row);
                compare_array<46, std::string>(row);
                compare_array<47, std::string>(row);
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
                [&ctx] { CelonisAlignModelV2::align_model_v2_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL); });
        ASSERT_OK(CelonisAlignModelV2::align_model_v2_prepare(ctx.get(),
                                                              FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
        DeferOp close_thread_local(
                [&ctx] { CelonisAlignModelV2::align_model_v2_close(ctx.get(), FunctionContext::THREAD_LOCAL); });
        ASSERT_OK(CelonisAlignModelV2::align_model_v2_prepare(ctx.get(),
                                                              FunctionContext::FunctionStateScope::THREAD_LOCAL));

        const auto result = CelonisAlignModelV2::align_model_v2(ctx.get(), columns).value();
        ASSERT_TRUE(result->is_struct());
        const StructColumn* st = down_cast<const StructColumn*>(result.get());
        Evaluator evaluator(*st, expected, return_type_);
        evaluator.evaluate();
    }

    std::vector<FunctionContext::TypeDesc> arg_types_;
    FunctionContext::TypeDesc return_type_;
};

const std::string CelonisAlignModelV2Test::PARALLEL_MODEL =
        R"json({
            "nodes": [
                {
                    "node_id": "0",
                    "node_type": 4
                },
                {
                    "node_id": "1",
                    "node_type": 1,
                    "task_name": "A"
                },
                {
                    "node_id": "2",
                    "node_type": 3
                },
                {
                    "node_id": "3",
                    "node_type": 1,
                    "task_name": "B"
                },
                {
                    "node_id": "4",
                    "node_type": 1,
                    "task_name": "C"
                },
                {
                    "node_id": "5",
                    "node_type": 3
                },
                {
                    "node_id": "6",
                    "node_type": 5
                }
            ],
            "edges": [
                {
                    "from": "0",
                    "to": "1"
                },
                {
                    "from": "1",
                    "to": "2"
                },
                {
                    "from": "2",
                    "to": "3"
                },
                {
                    "from": "2",
                    "to": "4"
                },
                {
                    "from": "3",
                    "to": "5"
                },
                {
                    "from": "4",
                    "to": "5"
                },
                {
                    "from": "5",
                    "to": "6"
                }
            ],
            "cache_key": "CACHE_KEY"
        })json";

const CelonisAlignModelV2Test::ResultMap CelonisAlignModelV2Test::PARALLEL_MODEL_RESULTS = {
        {{"A", "C"},
         {{"A", "C"},
          {1, 4, 3},
          {"A", "C", "B"},
          {"SYNC_MOVE", "SYNC_MOVE", "MODEL_MOVE"},
          {0, 1, 0},
          {
                  "CONFORMING",
                  "CONFORMING",
                  "MISSING",
          },
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {0, 2, 1},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {1, 3, 6},
          {"SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"A", "B", "BPMN_END"},
          {0, 2, 1},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {2, 3, 5},
          {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"BPMN_PARALLEL", "B", "BPMN_PARALLEL"},
          {0, 1},
          {"CONFORMING", "CONFORMING"},
          {0, 0},
          {2, 5},
          {"GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_PARALLEL", "BPMN_PARALLEL"},
          {0, 0, 0, 1, 1, 1},
          {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
          {0, 0, 0, 0, 0, 0},
          {0, 1, 2, 4, 5, 6},
          {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "A", "BPMN_PARALLEL", "C", "BPMN_PARALLEL", "BPMN_END"},
          {},
          {},
          {},
          {},
          {},
          {}}},
        {{"A", "B", "C"},
         {{"A", "B", "C"},
          {1, 3, 4},
          {"A", "B", "C"},
          {"SYNC_MOVE", "SYNC_MOVE", "SYNC_MOVE"},
          {0, 1, 2},
          {"CONFORMING", "CONFORMING", "CONFORMING"},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {0, 0, 0, 1, 2, 2, 0, 2, 2},
          {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING",
           "CONFORMING", "CONFORMING"},
          {0, 0, 0, 0, 0, 0, 1, 1, 1},
          {0, 1, 2, 3, 5, 6, 2, 4, 5},
          {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE",
           "SYNC_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "A", "BPMN_PARALLEL", "B", "BPMN_PARALLEL", "BPMN_END", "BPMN_PARALLEL", "C", "BPMN_PARALLEL"},
          {},
          {},
          {},
          {},
          {},
          {}}},
        {{"C", "B", "null", "B"},
         {{"C", "B", "NULL", "B"},
          {1, 4, 3, 3},
          {"A", "C", "B", "B"},
          {"MODEL_MOVE", "SYNC_MOVE", "SYNC_MOVE", "LOG_MOVE"},
          {0, 0, 1, 3},
          {"MISSING", "CONFORMING", "CONFORMING", "EXCESSIVE"},
          {},
          {},
          {},
          {},
          {},
          {},
          {2, 3, 3},
          {"CONFORMING", "EXCESSIVE", "CONFORMING"},
          {0, 0, 0},
          {3, 3, 6},
          {"SYNC_MOVE", "LOG_MOVE", "GATEWAY_MOVE"},
          {"B", "B", "BPMN_END"},
          {0, 0, 1},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {0, 1, 4},
          {"GATEWAY_MOVE", "MODEL_MOVE", "SYNC_MOVE"},
          {"BPMN_START", "A", "C"},
          {0, 0, 0},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {0, 1, 2},
          {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "A", "BPMN_PARALLEL"},
          {0, 0},
          {"CONFORMING", "CONFORMING"},
          {0, 0},
          {0, 2},
          {"GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "BPMN_PARALLEL"},
          {0, 1, 2, 3, 0, 2, 2},
          {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
          {0, 0, 0, 0, 1, 1, 1},
          {2, 4, 5, 6, 2, 3, 5},
          {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE"},
          {"BPMN_PARALLEL", "C", "BPMN_PARALLEL", "BPMN_END", "BPMN_PARALLEL", "B", "BPMN_PARALLEL"},
          {},
          {},
          {},
          {},
          {},
          {}}},
        {{"C", "B", "B"},
         {{"C", "B", "B"},
          {1, 4, 3, 3},
          {"A", "C", "B", "B"},
          {"MODEL_MOVE", "SYNC_MOVE", "SYNC_MOVE", "LOG_MOVE"},
          {0, 0, 1, 2},
          {"MISSING", "CONFORMING", "CONFORMING", "EXCESSIVE"},
          {},
          {},
          {},
          {},
          {},
          {},
          {2, 3, 3},
          {"CONFORMING", "EXCESSIVE", "CONFORMING"},
          {0, 0, 0},
          {3, 3, 6},
          {"SYNC_MOVE", "LOG_MOVE", "GATEWAY_MOVE"},
          {"B", "B", "BPMN_END"},
          {0, 0, 1},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {0, 1, 4},
          {"GATEWAY_MOVE", "MODEL_MOVE", "SYNC_MOVE"},
          {"BPMN_START", "A", "C"},
          {0, 0, 0},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {0, 1, 2},
          {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "A", "BPMN_PARALLEL"},
          {0, 0},
          {"CONFORMING", "CONFORMING"},
          {0, 0},
          {0, 2},
          {"GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "BPMN_PARALLEL"},
          {0, 1, 2, 3, 0, 2, 2},
          {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
          {0, 0, 0, 0, 1, 1, 1},
          {2, 4, 5, 6, 2, 3, 5},
          {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE"},
          {"BPMN_PARALLEL", "C", "BPMN_PARALLEL", "BPMN_END", "BPMN_PARALLEL", "B", "BPMN_PARALLEL"},
          {},
          {},
          {},
          {},
          {},
          {}}},
        {{"A", "null", "null", "C"},
         {{"A", "NULL", "NULL", "C"},
          {1, 4, 3},
          {"A", "C", "B"},
          {"SYNC_MOVE", "SYNC_MOVE", "MODEL_MOVE"},
          {0, 3, 0},
          {"CONFORMING", "CONFORMING", "MISSING"},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {0, 2, 1},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {1, 3, 6},
          {"SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"A", "B", "BPMN_END"},
          {0, 2, 1},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {2, 3, 5},
          {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"BPMN_PARALLEL", "B", "BPMN_PARALLEL"},
          {0, 1},
          {"CONFORMING", "CONFORMING"},
          {0, 0},
          {2, 5},
          {"GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_PARALLEL", "BPMN_PARALLEL"},
          {0, 0, 0, 1, 1, 1},
          {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
          {0, 0, 0, 0, 0, 0},
          {0, 1, 2, 4, 5, 6},
          {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "A", "BPMN_PARALLEL", "C", "BPMN_PARALLEL", "BPMN_END"},
          {},
          {},
          {},
          {},
          {},
          {}}},
        {{"null", "A", "null", "C", "null"},
         {{"NULL", "A", "NULL", "C", "NULL"},
          {1, 4, 3},
          {"A", "C", "B"},
          {"SYNC_MOVE", "SYNC_MOVE", "MODEL_MOVE"},
          {1, 3, 1},
          {"CONFORMING", "CONFORMING", "MISSING"},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {0, 2, 1},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {1, 3, 6},
          {"SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"A", "B", "BPMN_END"},
          {0, 2, 1},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {2, 3, 5},
          {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"BPMN_PARALLEL", "B", "BPMN_PARALLEL"},
          {0, 1},
          {"CONFORMING", "CONFORMING"},
          {0, 0},
          {2, 5},
          {"GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_PARALLEL", "BPMN_PARALLEL"},
          {0, 0, 0, 1, 1, 1},
          {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
          {0, 0, 0, 0, 0, 0},
          {0, 1, 2, 4, 5, 6},
          {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "A", "BPMN_PARALLEL", "C", "BPMN_PARALLEL", "BPMN_END"},
          {},
          {},
          {},
          {},
          {},
          {}}},
};

const std::string CelonisAlignModelV2Test::LOOP_MODEL =
        R"json({
            "nodes": [
                {
                    "node_id": "0",
                    "node_type": 4
                },
                {
                    "node_id": "1",
                    "node_type": 2
                },
                {
                    "node_id": "2",
                    "node_type": 1,
                    "task_name": "A"
                },
                {
                    "node_id": "3",
                    "node_type": 1,
                    "task_name": "B"
                },
                {
                    "node_id": "4",
                    "node_type": 2
                },
                {
                    "node_id": "5",
                    "node_type": 1,
                    "task_name": "C"
                },
                {
                    "node_id": "6",
                    "node_type": 5
                }
            ],
            "edges": [
                {
                    "from": "0",
                    "to": "1"
                },
                {
                    "from": "1",
                    "to": "2"
                },
                {
                    "from": "2",
                    "to": "3"
                },
                {
                    "from": "3",
                    "to": "4"
                },
                {
                    "from": "4",
                    "to": "5"
                },
                {
                    "from": "4",
                    "to": "6"
                },
                {
                    "from": "5",
                    "to": "1"
                }
            ],
            "cache_key": "CACHE_KEY"
        })json";

const CelonisAlignModelV2Test::ResultMap CelonisAlignModelV2Test::LOOP_MODEL_RESULTS = {

        {{"A", "B", "C", "A", "B"},
         {{"A", "B", "C", "A", "B"},
          {2, 3, 5, 2, 3},
          {"A", "B", "C", "A", "B"},
          {"SYNC_MOVE", "SYNC_MOVE", "SYNC_MOVE", "SYNC_MOVE", "SYNC_MOVE"},
          {0, 1, 2, 3, 4},
          {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {0, 0, 0, 1, 1, 2, 2, 3, 4, 4, 4},
          {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING",
           "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING"},
          {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
          {0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6},
          {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
           "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE", "C", "BPMN_EXCLUSIVE_CHOICE", "A",
           "B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
          {},
          {},
          {},
          {},
          {},
          {}}},
        {{"A", "B", "C"},
         {{"A", "B", "C"},
          {2, 3, 5, 2, 3},
          {"A", "B", "C", "A", "B"},
          {"SYNC_MOVE", "SYNC_MOVE", "SYNC_MOVE", "MODEL_MOVE", "MODEL_MOVE"},
          {0, 1, 2, 2, 2},
          {"CONFORMING", "CONFORMING", "CONFORMING", "MISSING", "MISSING"},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {2, 3, 4, 4},
          {"CONFORMING", "MISSING", "MISSING", "CONFORMING"},
          {0, 0, 0, 0},
          {5, 2, 3, 6},
          {"SYNC_MOVE", "MODEL_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"C", "A", "B", "BPMN_END"},
          {2, 3, 4, 4},
          {"CONFORMING", "MISSING", "MISSING", "CONFORMING"},
          {0, 0, 0, 0},
          {1, 2, 3, 4},
          {"GATEWAY_MOVE", "MODEL_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE"},
          {2, 4},
          {"CONFORMING", "CONFORMING"},
          {0, 0},
          {1, 4},
          {"GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_EXCLUSIVE_CHOICE", "BPMN_EXCLUSIVE_CHOICE"},
          {0, 0, 0, 1, 1, 2, 2, 4, 4},
          {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING",
           "CONFORMING", "CONFORMING"},
          {0, 0, 0, 0, 0, 0, 0, 1, 1},
          {0, 1, 2, 3, 4, 5, 1, 4, 6},
          {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
           "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE", "C", "BPMN_EXCLUSIVE_CHOICE",
           "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
          {},
          {},
          {},
          {},
          {},
          {}}},
        {{"A", "B", "A", "B"},
         {{"A", "B", "A", "B"},
          {2, 3, 5, 2, 3},
          {"A", "B", "C", "A", "B"},
          {"SYNC_MOVE", "SYNC_MOVE", "MODEL_MOVE", "SYNC_MOVE", "SYNC_MOVE"},
          {0, 1, 1, 2, 3},
          {"CONFORMING", "CONFORMING", "MISSING", "CONFORMING", "CONFORMING"},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {1, 2, 3},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {3, 5, 2},
          {"SYNC_MOVE", "MODEL_MOVE", "SYNC_MOVE"},
          {"B", "C", "A"},
          {1, 2, 2},
          {"CONFORMING", "MISSING", "CONFORMING"},
          {0, 0, 0},
          {4, 5, 1},
          {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE"},
          {"BPMN_EXCLUSIVE_CHOICE", "C", "BPMN_EXCLUSIVE_CHOICE"},
          {1, 2},
          {"CONFORMING", "CONFORMING"},
          {0, 0},
          {4, 1},
          {"GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_EXCLUSIVE_CHOICE", "BPMN_EXCLUSIVE_CHOICE"},
          {0, 0, 0, 1, 1, 2, 3, 4, 4, 4},
          {"CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING", "CONFORMING",
           "CONFORMING", "CONFORMING", "CONFORMING"},
          {0, 0, 0, 0, 0, 1, 1, 1, 1, 1},
          {0, 1, 2, 3, 4, 1, 2, 3, 4, 6},
          {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE",
           "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
          {"BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "A", "B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_EXCLUSIVE_CHOICE", "A", "B",
           "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
          {},
          {},
          {},
          {},
          {},
          {}}}};

TEST_F(CelonisAlignModelV2Test, Parallel) {
    VariantRows variants = {{"A", "C"}, {"A", "B", "C"}, {"C", "B", "null", "B"}};
    std::vector<Result> expected = {PARALLEL_MODEL_RESULTS.at(variants[0]), PARALLEL_MODEL_RESULTS.at(variants[1]),
                                    PARALLEL_MODEL_RESULTS.at(variants[2])};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisAlignModelV2Test, Loop) {
    VariantRows variants = {{"A", "B", "C", "A", "B"}, {"A", "B", "C"}, {"A", "B", "A", "B"}};
    std::vector<Result> expected = {LOOP_MODEL_RESULTS.at(variants[0]), LOOP_MODEL_RESULTS.at(variants[1]),
                                    LOOP_MODEL_RESULTS.at(variants[2])};
    Run(variants, LOOP_MODEL, expected);
}

TEST_F(CelonisAlignModelV2Test, Parallel_DuplicatedVariants) {
    VariantRows variants = {{"A", "C"}, {"A", "B", "C"}, {"A", "C"}, {"C", "B", "null", "B"}};
    std::vector<Result> expected = {PARALLEL_MODEL_RESULTS.at(variants[0]), PARALLEL_MODEL_RESULTS.at(variants[1]),
                                    PARALLEL_MODEL_RESULTS.at(variants[2]), PARALLEL_MODEL_RESULTS.at(variants[3])};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisAlignModelV2Test, Parallel_NULL) {
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

TEST_F(CelonisAlignModelV2Test, Parallel_ONLYNULL) {
    VariantRows variants = {{}, {}};
    std::vector<Result> expected = {{}, {}};
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisAlignModelV2Test, InvalidModel) {
    auto variants = ColumnHelper::create_column(celonis::array_type(TYPE_VARCHAR), /*nullable=*/true);
    variants->append_nulls(1);
    auto model_column = ColumnHelper::create_const_column<TYPE_VARCHAR>("Invalid JSON", 1);

    Columns columns;
    columns.push_back(variants);
    columns.push_back(model_column);

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types_), return_type_));
    ctx->set_constant_columns(columns);

    ASSERT_OK(CelonisAlignModelV2::align_model_v2_prepare(ctx.get(),
                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    ASSERT_OK(
            CelonisAlignModelV2::align_model_v2_prepare(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));

    const auto result = CelonisAlignModelV2::align_model_v2(ctx.get(), columns);
    ASSERT_FALSE(result.ok());
    EXPECT_TRUE(result.status().is_invalid_argument());

    ASSERT_OK(CelonisAlignModelV2::align_model_v2_close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
    ASSERT_OK(
            CelonisAlignModelV2::align_model_v2_close(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
}

TEST_F(CelonisAlignModelV2Test, LongVariant) {
    // Test that variants longer than 40000 activities are now supported
    // Previously this would throw an InternalError
    std::vector<std::string> long_variant;
    long_variant.reserve(40005);

    // Create a variant with 40005 activities
    for (int i = 0; i < 40005; i++) {
        long_variant.emplace_back("A");
    }

    VariantRows variants = {long_variant};

    auto array_column = build_variant_column(variants);
    auto model_column = ColumnHelper::create_const_column<TYPE_VARCHAR>(PARALLEL_MODEL, 1);

    Columns columns;
    columns.push_back(array_column);
    columns.push_back(model_column);

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types_), return_type_));
    ctx->set_constant_columns(columns);

    ASSERT_OK(CelonisAlignModelV2::align_model_v2_prepare(ctx.get(),
                                                          FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
    ASSERT_OK(
            CelonisAlignModelV2::align_model_v2_prepare(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));

    // This should not throw an InternalError about variants longer than 40000
    const auto result = CelonisAlignModelV2::align_model_v2(ctx.get(), columns);
    ASSERT_TRUE(result.ok()) << "Expected variants longer than 40000 to be supported after CPML fix, but got error: "
                             << result.status().message();

    ASSERT_OK(CelonisAlignModelV2::align_model_v2_close(ctx.get(), FunctionContext::FunctionStateScope::THREAD_LOCAL));
    ASSERT_OK(
            CelonisAlignModelV2::align_model_v2_close(ctx.get(), FunctionContext::FunctionStateScope::FRAGMENT_LOCAL));
}

TEST_F(CelonisAlignModelV2Test, Concurrency) {
    int num_inputs = 100;
    int num_threads = 1000;

    const std::string* models[2] = {&PARALLEL_MODEL, &LOOP_MODEL};
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
        inputs.emplace_back(*models[model], std::move(variants), std::move(expected));
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