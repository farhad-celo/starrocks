#include <gtest/gtest.h>

#include "column/array_column.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/align_model.h"
#include "exprs/celonis/variant_stats.h"
#include "runtime/mem_pool.h"
#include "testutil/function_utils.h"
#include "util.h"
#include "util/slice.h"

namespace starrocks {

class CelonisAlignModelTest : public testing::Test {
protected:
    CelonisAlignModelTest() = default;

    void SetUp() override {}

    void TearDown() override {}

private:
    typedef std::tuple<std::vector<int64_t>, std::vector<std::string>, std::vector<std::string>, std::vector<int64_t>,
                       std::vector<int64_t>, std::vector<int64_t>, std::vector<int64_t>, std::vector<std::string>>
            Result;

    typedef std::vector<std::vector<std::string>> VariantRows;

    static const std::string PARALLEL_MODEL;
    static const std::string LOOP_MODEL;

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
        std::vector<FunctionContext::TypeDesc> arg_types = {
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY)),
                AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
        FunctionContext::TypeDesc return_type{
                .type = TYPE_STRUCT,
                .children = {
                        TYPEDESC_ARRAY_BIGINT, TYPEDESC_ARRAY_VARCHAR, TYPEDESC_ARRAY_VARCHAR, TYPEDESC_ARRAY_BIGINT,
                        TYPEDESC_ARRAY_BIGINT, TYPEDESC_ARRAY_BIGINT, TYPEDESC_ARRAY_BIGINT, TYPEDESC_ARRAY_VARCHAR
                },
                .field_names = {
                        "alignment_model_vertex_id", "alignment_vertex_label", "alignment_move_type",
                        "alignment_activity_index", "association_edge_class", "association_alignment_index",
                        "edge_class_id", "edge_class_type"
                }
        };
        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

        auto variants = build_variant_column(variant_rows);
        auto model_column =
                ColumnHelper::create_const_column<TYPE_VARCHAR>(bpmn_model_description_json, variant_rows.size());

        Columns columns;
        columns.push_back(variants);
        columns.push_back(model_column);
        ctx->set_constant_columns(columns);

        const auto result = CelonisAlignModel::align_model(ctx.get(), columns).value();
        ASSERT_TRUE(result->is_struct());
        StructColumn* st = down_cast<StructColumn*>(result.get());
        Evaluator evaluator(*st, expected, return_type);
        evaluator.evaluate();
    }
};

const std::string CelonisAlignModelTest::PARALLEL_MODEL =
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

const std::string CelonisAlignModelTest::LOOP_MODEL =
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

    TEST_F(CelonisAlignModelTest, Parallel) {
    VariantRows variants = {{"A", "C"},
                            {"A", "B", "C"},
                            {"C", "B", "B"}};
    std::vector<Result> expected = {
            {
                    {0, 1, 2, 4, 3, 5, 6},
                    {"BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK C", "BPMN_TASK B", "BPMN_PARALLEL",
                            "BPMN_END"},
                    {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE",
                            "GATEWAY_MOVE"},
                    {0, 0, 0, 1, 0, 1, 1},
                    {0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2},
                    {0, 1, 2, 3, 5, 6, 2, 4, 5, 2, 5},
                    {0, 1, 2},
                    {"SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE"}
            },
            {
                    {0, 1, 2, 3, 4, 5, 6},
                    {"BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK B", "BPMN_TASK C", "BPMN_PARALLEL",
                            "BPMN_END"},
                    {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
                            "GATEWAY_MOVE"},
                    {0, 0, 0, 1, 2, 2, 2},
                    {0, 0, 0, 0, 0, 0, 1, 1, 1},
                    {0, 1, 2, 3, 5, 6, 2, 4, 5},
                    {0, 1},
                    {"SYNC_EDGE", "SYNC_EDGE"}
            },
            {
                    {0, 1, 2, 4, 3, 3, 5, 6},
                    {"BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK C", "BPMN_TASK B", "BPMN_TASK B",
                            "BPMN_PARALLEL", "BPMN_END"},
                    {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "LOG_MOVE",
                            "GATEWAY_MOVE", "GATEWAY_MOVE"},
                    {0, 0, 0, 0, 1, 2, 1, 2},
                    {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4},
                    {2, 3, 6, 7, 2, 4, 6, 0, 1, 2, 0, 2, 4, 5, 7},
                    {0, 1, 2, 3, 4},
                    {"SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "LOG_EDGE"}
            }
    };
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisAlignModelTest, Loop) {
    VariantRows variants = {{"A", "B", "C", "A", "B"},
                            {"A", "B", "C"},
                            {"A", "B", "A", "B"}};
    std::vector<Result> expected = {
            {
                    {0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6},
                    {"BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B", "BPMN_EXCLUSIVE_CHOICE",
                            "BPMN_TASK C", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B",
                            "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
                    {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE",
                            "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
                    {0, 0, 0, 1, 1, 2, 2, 3, 4, 4, 4},
                    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
                    {0},
                    {"SYNC_EDGE"}
            },
            {
                    {0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6},
                    { "BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B", "BPMN_EXCLUSIVE_CHOICE",
                            "BPMN_TASK C", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B",
                            "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
                    {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE",
                            "GATEWAY_MOVE", "MODEL_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
                    {0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2},
                    {0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 3, 3},
                    {0, 1, 2, 3, 4, 5, 6, 9, 10, 6, 7, 8, 9, 6, 9},
                    {0, 1, 2, 3},
                    {"SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE"}
            },
            {
                    {0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6},
                    { "BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B", "BPMN_EXCLUSIVE_CHOICE",
                            "BPMN_TASK C", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B",
                            "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"},
                    {"GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "MODEL_MOVE",
                            "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"},
                    {0, 0, 0, 1, 1, 1, 1, 2, 3, 3, 3},
                    {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3},
                    {0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 4, 5, 6, 4, 6},
                    {0, 1, 2, 3},
                    {"SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE"}
            }
    };
    Run(variants, LOOP_MODEL, expected);
}

TEST_F(CelonisAlignModelTest, Parallel_DuplicatedVariants) {
    VariantRows variants = {{"A", "C"},
                            {"A", "B", "C"},
                            {"A", "C"},
                            {"C", "B", "B"}};
    std::vector<Result> expected = {
            {
                    {0, 1, 2, 4, 3, 5, 6},
                    {"BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK C", "BPMN_TASK B", "BPMN_PARALLEL",
                            "BPMN_END"},
                    {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE",
                            "GATEWAY_MOVE"},
                    {0, 0, 0, 1, 0, 1, 1},
                    {0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2},
                    {0, 1, 2, 3, 5, 6, 2, 4, 5, 2, 5},
                    {0, 1, 2},
                    {"SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE"}
            },
            {
                    {0, 1, 2, 3, 4, 5, 6},
                    {"BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK B", "BPMN_TASK C", "BPMN_PARALLEL",
                            "BPMN_END"},
                    {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
                            "GATEWAY_MOVE"},
                    {0, 0, 0, 1, 2, 2, 2},
                    {0, 0, 0, 0, 0, 0, 1, 1, 1},
                    {0, 1, 2, 3, 5, 6, 2, 4, 5},
                    {0, 1},
                    {"SYNC_EDGE", "SYNC_EDGE"}
            },
            {
                    {0, 1, 2, 4, 3, 5, 6},
                    {"BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK C", "BPMN_TASK B", "BPMN_PARALLEL",
                            "BPMN_END"},
                    {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE",
                            "GATEWAY_MOVE"},
                    {0, 0, 0, 1, 0, 1, 1},
                    {0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2},
                    {0, 1, 2, 3, 5, 6, 2, 4, 5, 2, 5},
                    {0, 1, 2},
                    {"SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE"}
            },
            {
                    {0, 1, 2, 4, 3, 3, 5, 6},
                    {"BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK C", "BPMN_TASK B", "BPMN_TASK B",
                            "BPMN_PARALLEL", "BPMN_END"},
                    {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "LOG_MOVE",
                            "GATEWAY_MOVE", "GATEWAY_MOVE"},
                    {0, 0, 0, 0, 1, 2, 1, 2},
                    {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4},
                    {2, 3, 6, 7, 2, 4, 6, 0, 1, 2, 0, 2, 4, 5, 7},
                    {0, 1, 2, 3, 4},
                    {"SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "LOG_EDGE"}
            }
    };
    Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisAlignModelTest, Parallel_NULL) {
    VariantRows variants = {{"A", "C"},
                            {"A", "B", "C"},
                            {},
                            {"null", "null"},
                            {"C", "null", "B", "B"},
                            {"C", "B", "B"},
                            {}};
    std::vector<Result> expected = {
            {
                    {0, 1, 2, 4, 3, 5, 6},
                    {"BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK C", "BPMN_TASK B", "BPMN_PARALLEL",
                            "BPMN_END"},
                    {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE",
                            "GATEWAY_MOVE"},
                    {0, 0, 0, 1, 0, 1, 1},
                    {0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2},
                    {0, 1, 2, 3, 5, 6, 2, 4, 5, 2, 5},
                    {0, 1, 2},
                    {"SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE"}
            },
            {
                    {0, 1, 2, 3, 4, 5, 6},
                    {"BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK B", "BPMN_TASK C", "BPMN_PARALLEL",
                            "BPMN_END"},
                    {"GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE",
                            "GATEWAY_MOVE"},
                    {0, 0, 0, 1, 2, 2, 2},
                    {0, 0, 0, 0, 0, 0, 1, 1, 1},
                    {0, 1, 2, 3, 5, 6, 2, 4, 5},
                    {0, 1},
                    {"SYNC_EDGE", "SYNC_EDGE"}
            },
            {
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {}
            },
            {
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {}
            },
            {
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {}
            },
            {
                    {0, 1, 2, 4, 3, 3, 5, 6},
                    {"BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK C", "BPMN_TASK B", "BPMN_TASK B",
                            "BPMN_PARALLEL", "BPMN_END"},
                    {"GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "LOG_MOVE",
                            "GATEWAY_MOVE", "GATEWAY_MOVE"},
                    {0, 0, 0, 0, 1, 2, 1, 2},
                    {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4},
                    {2, 3, 6, 7, 2, 4, 6, 0, 1, 2, 0, 2, 4, 5, 7},
                    {0, 1, 2, 3, 4},
                    {"SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "LOG_EDGE"}
            },
            {
                {},
                {},
                {},
                {},
                {},
                {},
                {},
                {}
            }
    };
        Run(variants, PARALLEL_MODEL, expected);
}

TEST_F(CelonisAlignModelTest, Parallel_ONLYNULL) {
    VariantRows variants = {{},
                            {}};
    std::vector<Result> expected = {
            {
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {}
            },
            {
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {}
            }
    };
    Run(variants, PARALLEL_MODEL, expected);
}

} // namespace starrocks