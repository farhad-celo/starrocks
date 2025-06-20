#include "exprs/celonis/mo_bpmn_graph.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <testutil/assert.h>

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/vectorized_fwd.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/function_context.h"
#include "rapidjson/document.h"
#include "runtime/mem_pool.h"
#include "testutil/function_utils.h"

namespace starrocks {

namespace {

class ManagedAggrState {
public:
    ~ManagedAggrState() { _func->destroy(_ctx, _state); }

    static std::unique_ptr<ManagedAggrState> create(FunctionContext* ctx, const AggregateFunction* func) {
        return std::make_unique<ManagedAggrState>(ctx, func);
    }

    AggDataPtr state() { return _state; }

private:
    ManagedAggrState(FunctionContext* ctx, const AggregateFunction* func) : _ctx(ctx), _func(func) {
        _state = _mem_pool.allocate_aligned(func->size(), func->alignof_size());
        _func->create(_ctx, _state);
    }

    FunctionContext* _ctx;
    const AggregateFunction* _func;
    MemPool _mem_pool;
    AggDataPtr _state;
};

} // namespace

class TestJsonEvaluator {
public:
    explicit TestJsonEvaluator(const std::string& json_string) {
        document_.Parse(json_string.c_str());
    }

    template <typename T>
    void evaluate(const char* table, const char* column, const std::vector<T>& values) {
        ASSERT_FALSE(document_.HasParseError());
        ASSERT_TRUE(document_.HasMember(table));
        const rapidjson::Value& table_values = document_[table];
        ASSERT_EQ(table_values.Size(), values.size()) << "table: " << table;
        for (rapidjson::SizeType i = 0; i < table_values.Size(); ++i) {
            ASSERT_TRUE(table_values[i].HasMember(column))
                    << "table: " << table << ", column: " << column << ", row: " << i;
            if constexpr (std::is_same_v<T, int>) {
                EXPECT_EQ(table_values[i][column].GetInt(), values[i])
                                    << "table: " << table << ", column: " << column << ", row: " << i;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                EXPECT_EQ(table_values[i][column].GetInt64(), values[i])
                                    << "table: " << table << ", column: " << column << ", row: " << i;
            } else if constexpr (std::is_same_v<T, std::string>) {
                EXPECT_EQ(table_values[i][column].GetString(), values[i])
                                    << "table: " << table << ", column: " << column << ", row: " << i;
            }
        }
    }

private:
    rapidjson::Document document_;
};

class CelonisMoBpmnGraphTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}

private:
    typedef std::vector<std::vector<std::string>> VariantRows;

    StatusOr<std::string> run(const std::vector<Slice>& input) {
        Columns input_columns;
        for (auto item : input) {
            auto input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), false);
            input_column->append_datum(item);
            input_columns.push_back(std::move(input_column));
        }

        std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
        auto context = ctx.get();
        context->set_constant_columns(input_columns);

        ASSIGN_OR_RETURN(auto result, CelonisMoBpmnGraph::mo_bpmn_graph(context, input_columns));
        EXPECT_EQ(result->size(), 1);
        return result->get(0).get_slice().to_string();
    }

    ArrayColumn::Ptr build_variant_column(const std::vector<std::vector<std::string>>& rows) {
        ColumnBuilder<TYPE_VARCHAR> builder(config::vector_chunk_size);
        auto offsets = UInt32Column::create();
        int offset = 0;

        offsets->append(offset);
        for (int i = 0; i < rows.size(); i++) {
            for (int j = 0; j < rows[i].size(); j++) {
                if (rows[i][j] == "null") {
                    builder.append_null();
                } else {
                    builder.append(Slice(rows[i][j]));
                }
            }
            offset += rows[i].size();
            offsets->append(offset);
        }

        auto data_col = builder.build_nullable_column();
        return ArrayColumn::create(data_col, offsets);
    }

    std::string inductive_miner(const VariantRows& variant_rows, int64_t weight = 1L) {
        const AggregateFunction* func = get_aggregate_function("celonis_inductive_miner", TYPE_ARRAY, TYPE_VARCHAR, false);
        auto variant_column = build_variant_column(variant_rows);
        auto weight_column = ColumnHelper::create_const_column<TYPE_BIGINT>(weight, variant_rows.size());
        auto threshold_column = ColumnHelper::create_const_column<TYPE_DOUBLE>(0.0, variant_rows.size());

        FunctionUtils utils;
        auto ctx = utils.get_fn_ctx();

        Columns columns;
        columns.push_back(variant_column);
        columns.push_back(weight_column);
        columns.push_back(threshold_column);
        ctx->set_constant_columns(columns);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = variant_column.get();
        raw_columns[1] = weight_column.get();
        raw_columns[2] = threshold_column.get();
        auto state = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, variant_column->size(), raw_columns.data(), state->state());

        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, state->state(), result.get());

        return result->get_slice(0).to_string();
    }
};

TEST_F(CelonisMoBpmnGraphTest, pql_mo_bpmn_graph_example) {
    Slice input1(
            R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A",
                    "object_count": 0
                },
                {
                    "process_tree_type": 1,
                    "activity": "B",
                    "object_count": 0
                },
                {
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "C",
                    "object_count": 0
                },
                {
                    "process_tree_type": 1,
                    "activity": "D",
                    "object_count": 0
                }
            ],
            "edge_properties": [
                {
                    "edge_source_id": 0,
                    "edge_target_id": 1
                },
                {
                    "edge_source_id": 0,
                    "edge_target_id": 2
                },
                {
                    "edge_source_id": 0,
                    "edge_target_id": 3
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 5
                }
            ],
            "statistics": [
                {
                    "key":"m2a_precision_times_1E4",
                    "value":"0"
                },
                {
                    "key":"flower_fallback_count",
                    "value":"0"
                },
                {
                    "key":"has_seq_xor",
                    "value":"0"
                },
                {
                    "key":"slack_tau_loop_count",
                    "value":"0"
                },
                {
                    "key":"activity_once_per_trace_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_subfinds_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_find_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_count",
                    "value":"0"
                },
                {
                    "key":"noisy_loop_count",
                    "value":"0"
                },
                {
                    "key":"noisy_par_count",
                    "value":"0"
                },
                {
                    "key":"tree_size",
                    "value":"0"
                },
                {
                    "key":"strict_tau_loop_count",
                    "value":"0"
                },
                {
                    "key":"empty_traces_base_case_count",
                    "value":"0"
                },
                {
                    "key":"par_count",
                    "value":"0"
                },
                {
                    "key":"activity_count",
                    "value":"0"
                },
                {
                    "key":"noisy_xor_count",
                    "value":"0"
                },
                {
                    "key":"tau_transitions_count",
                    "value":"0"
                },
                {
                    "key":"empty_log_base_case_count",
                    "value":"0"
                },
                {
                    "key":"noisy_single_activity_base_case_count",
                    "value":"0"
                },
                {
                    "key":"single_activity_base_case_count",
                    "value":"4"
                },
                {
                    "key":"xor_count",
                    "value":"1"
                },
                {
                    "key":"noisy_seq_count",
                    "value":"0"
                },
                {
                    "key":"seq_count",
                    "value":"1"
                },
                {
                    "key":"loop_count",
                    "value":"0"
                }
            ]
        })json");

    Slice input2(
            R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "B",
                    "object_count": 0
                },
                {
                    "process_tree_type": 1,
                    "activity": "C",
                    "object_count": 0
                },
                {
                    "process_tree_type": 0,
                    "activity": null,
                    "object_count": 0
                },
                {
                    "process_tree_type": 1,
                    "activity": "E",
                    "object_count": 0
                }
            ],
            "edge_properties": [
                {
                    "edge_source_id": 0,
                    "edge_target_id": 1
                },
                {
                    "edge_source_id": 0,
                    "edge_target_id": 2
                },
                {
                    "edge_source_id": 0,
                    "edge_target_id": 3
                },
                {
                    "edge_source_id": 1,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 1,
                    "edge_target_id": 5
                }
            ],
            "statistics": [
                {
                    "key":"m2a_precision_times_1E4",
                    "value":"0"
                },
                {
                    "key":"flower_fallback_count",
                    "value":"0"
                },
                {
                    "key":"has_seq_xor",
                    "value":"0"
                },
                {
                    "key":"slack_tau_loop_count",
                    "value":"0"
                },
                {
                    "key":"activity_once_per_trace_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_subfinds_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_find_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_count",
                    "value":"0"
                },
                {
                    "key":"noisy_loop_count",
                    "value":"0"
                },
                {
                    "key":"noisy_par_count",
                    "value":"0"
                },
                {
                    "key":"tree_size",
                    "value":"0"
                },
                {
                    "key":"strict_tau_loop_count",
                    "value":"0"
                },
                {
                    "key":"empty_traces_base_case_count",
                    "value":"1"
                },
                {
                    "key":"par_count",
                    "value":"0"
                },
                {
                    "key":"activity_count",
                    "value":"0"
                },
                {
                    "key":"noisy_xor_count",
                    "value":"0"
                },
                {
                    "key":"tau_transitions_count",
                    "value":"0"
                },
                {
                    "key":"empty_log_base_case_count",
                    "value":"0"
                },
                {
                    "key":"noisy_single_activity_base_case_count",
                    "value":"0"
                },
                {
                    "key":"single_activity_base_case_count",
                    "value":"3"
                },
                {
                    "key":"xor_count",
                    "value":"0"
                },
                {
                    "key":"noisy_seq_count",
                    "value":"0"
                },
                {
                    "key":"seq_count",
                    "value":"1"
                },
                {
                    "key":"loop_count",
                    "value":"0"
                }
            ]
        })json");

    auto result = run({input1, input2});
    ASSERT_TRUE(result.ok());

    TestJsonEvaluator e(result.value());

    e.evaluate<int>("bpmn_edges", "SOURCE_ID", {0, 2, 3, 4, 6, 4, 7, 5, 8, 10, 10, 12, 11, 3, 6});
    e.evaluate<int>("bpmn_edges", "TARGET_ID", {2, 3, 4, 6, 5, 7, 5, 1, 10, 11, 12, 11, 3, 6, 9});
    e.evaluate<int>("bpmn_edges", "OBJECT_ID", {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1});
    e.evaluate<int>("bpmn_edges", "OBJECT_COUNT", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

    e.evaluate<int>("bpmn_nodes", "NODE_ID", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    e.evaluate<int>("bpmn_nodes", "NODE_TYPE", {1, 2, 0, 0, 3, 3, 0, 0, 1, 2, 3, 3, 0});

    e.evaluate<std::string>("bpmn_activities", "ACTIVITY_NAME", {"A", "B", "C", "D", "E"});
    e.evaluate<int>("bpmn_activities", "NODE_ID", {2, 3, 6, 7, 12});

    e.evaluate<int>("bpmn_model_descriptions", "OBJECT_ID", {0, 1});
    e.evaluate<std::string>("bpmn_model_descriptions", "BPMN_MODEL_DESCRIPTION", {
            "[[0 BPMN_START][1 BPMN_END][2 BPMN_TASK 'A'][3 BPMN_TASK 'B'][4 BPMN_EXCLUSIVE_CHOICE][5 "
            "BPMN_EXCLUSIVE_CHOICE][6 BPMN_TASK 'C'][7 BPMN_TASK 'D']],[[0 2][2 3][3 4][4 6][4 7][5 1][6 5][7 5]]",
            "[[3 BPMN_TASK 'B'][6 BPMN_TASK 'C'][8 BPMN_START][9 BPMN_END][10 BPMN_EXCLUSIVE_CHOICE][11 "
            "BPMN_EXCLUSIVE_CHOICE][12 BPMN_TASK 'E']],[[3 6][6 9][8 10][10 11][10 12][11 3][12 11]]"});
}

TEST_F(CelonisMoBpmnGraphTest, pql_mo_bpmn_graph_example_with_inductive_miner_consistency) {

    auto evaluate = [](const std::string& json_result) {
        TestJsonEvaluator e(json_result);

        e.evaluate<int>("bpmn_edges", "SOURCE_ID", {0, 2, 3, 4, 6, 4, 7, 5, 8, 10, 10, 12, 11, 3, 6});
        e.evaluate<int>("bpmn_edges", "TARGET_ID", {2, 3, 4, 6, 5, 7, 5, 1, 10, 11, 12, 11, 3, 6, 9});
        e.evaluate<int>("bpmn_edges", "OBJECT_ID", {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1});
        e.evaluate<int>("bpmn_edges", "OBJECT_COUNT", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

        e.evaluate<int>("bpmn_nodes", "NODE_ID", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
        e.evaluate<int>("bpmn_nodes", "NODE_TYPE", {1, 2, 0, 0, 3, 3, 0, 0, 1, 2, 3, 3, 0});

        e.evaluate<std::string>("bpmn_activities", "ACTIVITY_NAME", {"A", "B", "C", "D", "E"});
        e.evaluate<int>("bpmn_activities", "NODE_ID", {2, 3, 6, 7, 12});

        e.evaluate<int>("bpmn_model_descriptions", "OBJECT_ID", {0, 1});
        e.evaluate<std::string>("bpmn_model_descriptions", "BPMN_MODEL_DESCRIPTION", {
                "[[0 BPMN_START][1 BPMN_END][2 BPMN_TASK 'A'][3 BPMN_TASK 'B'][4 BPMN_EXCLUSIVE_CHOICE][5 "
                "BPMN_EXCLUSIVE_CHOICE][6 BPMN_TASK 'C'][7 BPMN_TASK 'D']],[[0 2][2 3][3 4][4 6][4 7][5 1][6 5][7 5]]",
                "[[3 BPMN_TASK 'B'][6 BPMN_TASK 'C'][8 BPMN_START][9 BPMN_END][10 BPMN_EXCLUSIVE_CHOICE][11 "
                "BPMN_EXCLUSIVE_CHOICE][12 BPMN_TASK 'E']],[[3 6][6 9][8 10][10 11][10 12][11 3][12 11]]"});
    };

    {
        VariantRows variants1 = {{"A", "B", "C"},
                                 {"A", "B", "D"}};
        VariantRows variants2 = {{"E", "B", "C"},
                                 {"B", "C"}};

        auto result = run({Slice(inductive_miner(variants1)), Slice(inductive_miner(variants2))});
        ASSERT_TRUE(result.ok());
        evaluate(result.value());
    }
    {
        VariantRows variants1 = {{"A", "B", "D"},
                                 {"A", "B", "C"}};
        VariantRows variants2 = {{"E", "B", "C"},
                                 {"B", "C"}};

        auto result = run({Slice(inductive_miner(variants1)), Slice(inductive_miner(variants2))});
        ASSERT_TRUE(result.ok());
        evaluate(result.value());
    }
    {
        VariantRows variants1 = {{"A", "B", "C"},
                                 {"A", "B", "D"}};
        VariantRows variants2 = {{"B", "C"},
                                 {"E", "B", "C"}};

        auto result = run({Slice(inductive_miner(variants1)), Slice(inductive_miner(variants2))});
        ASSERT_TRUE(result.ok());
        evaluate(result.value());
    }
    {
        VariantRows variants1 = {{"A", "B", "D"},
                                 {"A", "B", "C"}};
        VariantRows variants2 = {{"B", "C"},
                                 {"E", "B", "C"}};

        auto result = run({Slice(inductive_miner(variants1)), Slice(inductive_miner(variants2))});
        ASSERT_TRUE(result.ok());
        evaluate(result.value());
    }
}

TEST_F(CelonisMoBpmnGraphTest, tiny_mo_scenario_with_inductive_miner) {
    VariantRows variants = {{"A", "B"}, {"C"}, {"D"}};

    auto result = run({Slice(inductive_miner(variants))});
    ASSERT_TRUE(result.ok());

    TestJsonEvaluator e(result.value());
    e.evaluate<int>("bpmn_edges", "SOURCE_ID", {0, 2, 4, 5, 2, 6, 2, 7, 3});
    e.evaluate<int>("bpmn_edges", "TARGET_ID", {2, 4, 5, 3, 6, 3, 7, 3, 1});
    e.evaluate<int>("bpmn_nodes", "NODE_ID", {0, 1, 2, 3, 4, 5, 6, 7});
    e.evaluate<std::string>("bpmn_activities", "ACTIVITY_NAME", {"A", "B", "C", "D"});
}

TEST_F(CelonisMoBpmnGraphTest, tiny_mo_scenario_repeated_traces_with_inductive_miner) {
    VariantRows variants = {{"A", "B"}, {"C"}, {"D"}, {"C"}, {"D"}};

    auto result = run({Slice(inductive_miner(variants))});
    ASSERT_TRUE(result.ok());

    TestJsonEvaluator e(result.value());
    e.evaluate<int>("bpmn_edges", "SOURCE_ID", {0, 2, 4, 5, 2, 6, 2, 7, 3});
    e.evaluate<int>("bpmn_edges", "TARGET_ID", {2, 4, 5, 3, 6, 3, 7, 3, 1});
    e.evaluate<int>("bpmn_edges", "OBJECT_ID", {0, 0, 0, 0, 0, 0, 0, 0, 0});
    e.evaluate<int>("bpmn_edges", "OBJECT_COUNT", {0, 0, 0, 0, 0, 0, 0, 0, 0});

    e.evaluate<int>("bpmn_nodes", "NODE_ID", {0, 1, 2, 3, 4, 5, 6, 7});
    e.evaluate<int>("bpmn_nodes", "NODE_TYPE", {1, 2, 3, 3, 0, 0, 0, 0});

    e.evaluate<std::string>("bpmn_activities", "ACTIVITY_NAME", {"A", "B", "C", "D"});
    e.evaluate<int>("bpmn_activities", "NODE_ID", {4, 5, 6, 7});
}

TEST_F(CelonisMoBpmnGraphTest, tiny_mo_scenario_with_inductive_miner_high_variant_count) {
    VariantRows variants = {{"A", "B"}, {"C"}, {"D"}};

    auto result = run({Slice(inductive_miner(variants, 10'000'000'000L))});
    ASSERT_TRUE(result.ok());

    TestJsonEvaluator e(result.value());
    e.evaluate<int>("bpmn_edges", "SOURCE_ID", {0, 2, 4, 5, 2, 6, 2, 7, 3});
    e.evaluate<int>("bpmn_edges", "TARGET_ID", {2, 4, 5, 3, 6, 3, 7, 3, 1});
    e.evaluate<int>("bpmn_nodes", "NODE_ID", {0, 1, 2, 3, 4, 5, 6, 7});
    e.evaluate<int64_t>("bpmn_edges", "OBJECT_COUNT", {0, 0, 0, 0, 0, 0, 0, 0, 0});
    e.evaluate<std::string>("bpmn_activities", "ACTIVITY_NAME", {"A", "B", "C", "D"});
}

TEST_F(CelonisMoBpmnGraphTest, high_object_count) {
    Slice input1(
            R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A",
                    "object_count": 0
                },
                {
                    "process_tree_type": 1,
                    "activity": "B",
                    "object_count": 0
                },
                {
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "C",
                    "object_count": 0
                },
                {
                    "process_tree_type": 1,
                    "activity": "D",
                    "object_count": 0
                }
            ],
            "edge_properties": [
                {
                    "edge_source_id": 0,
                    "edge_target_id": 1
                },
                {
                    "edge_source_id": 0,
                    "edge_target_id": 2
                },
                {
                    "edge_source_id": 0,
                    "edge_target_id": 3
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 5
                }
            ],
            "statistics": [
                {
                    "key":"m2a_precision_times_1E4",
                    "value":"0"
                },
                {
                    "key":"flower_fallback_count",
                    "value":"0"
                },
                {
                    "key":"has_seq_xor",
                    "value":"0"
                },
                {
                    "key":"slack_tau_loop_count",
                    "value":"0"
                },
                {
                    "key":"activity_once_per_trace_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_subfinds_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_find_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_count",
                    "value":"0"
                },
                {
                    "key":"noisy_loop_count",
                    "value":"0"
                },
                {
                    "key":"noisy_par_count",
                    "value":"0"
                },
                {
                    "key":"tree_size",
                    "value":"0"
                },
                {
                    "key":"strict_tau_loop_count",
                    "value":"0"
                },
                {
                    "key":"empty_traces_base_case_count",
                    "value":"0"
                },
                {
                    "key":"par_count",
                    "value":"0"
                },
                {
                    "key":"activity_count",
                    "value":"0"
                },
                {
                    "key":"noisy_xor_count",
                    "value":"0"
                },
                {
                    "key":"tau_transitions_count",
                    "value":"0"
                },
                {
                    "key":"empty_log_base_case_count",
                    "value":"0"
                },
                {
                    "key":"noisy_single_activity_base_case_count",
                    "value":"0"
                },
                {
                    "key":"single_activity_base_case_count",
                    "value":"4"
                },
                {
                    "key":"xor_count",
                    "value":"1"
                },
                {
                    "key":"noisy_seq_count",
                    "value":"0"
                },
                {
                    "key":"seq_count",
                    "value":"1"
                },
                {
                    "key":"loop_count",
                    "value":"0"
                }
            ]
        })json");

    Slice input2(
            R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "B",
                    "object_count": 0
                },
                {
                    "process_tree_type": 1,
                    "activity": "C",
                    "object_count": 0
                },
                {
                    "process_tree_type": 0,
                    "activity": null,
                    "object_count": 0
                },
                {
                    "process_tree_type": 1,
                    "activity": "E",
                    "object_count": 0
                }
            ],
            "edge_properties": [
                {
                    "edge_source_id": 0,
                    "edge_target_id": 1
                },
                {
                    "edge_source_id": 0,
                    "edge_target_id": 2
                },
                {
                    "edge_source_id": 0,
                    "edge_target_id": 3
                },
                {
                    "edge_source_id": 1,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 1,
                    "edge_target_id": 5
                }
            ],
            "statistics": [
                {
                    "key":"m2a_precision_times_1E4",
                    "value":"0"
                },
                {
                    "key":"flower_fallback_count",
                    "value":"0"
                },
                {
                    "key":"has_seq_xor",
                    "value":"0"
                },
                {
                    "key":"slack_tau_loop_count",
                    "value":"0"
                },
                {
                    "key":"activity_once_per_trace_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_subfinds_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_find_count",
                    "value":"0"
                },
                {
                    "key":"activity_concurrent_count",
                    "value":"0"
                },
                {
                    "key":"noisy_loop_count",
                    "value":"0"
                },
                {
                    "key":"noisy_par_count",
                    "value":"0"
                },
                {
                    "key":"tree_size",
                    "value":"0"
                },
                {
                    "key":"strict_tau_loop_count",
                    "value":"0"
                },
                {
                    "key":"empty_traces_base_case_count",
                    "value":"1"
                },
                {
                    "key":"par_count",
                    "value":"0"
                },
                {
                    "key":"activity_count",
                    "value":"0"
                },
                {
                    "key":"noisy_xor_count",
                    "value":"0"
                },
                {
                    "key":"tau_transitions_count",
                    "value":"0"
                },
                {
                    "key":"empty_log_base_case_count",
                    "value":"0"
                },
                {
                    "key":"noisy_single_activity_base_case_count",
                    "value":"0"
                },
                {
                    "key":"single_activity_base_case_count",
                    "value":"3"
                },
                {
                    "key":"xor_count",
                    "value":"0"
                },
                {
                    "key":"noisy_seq_count",
                    "value":"0"
                },
                {
                    "key":"seq_count",
                    "value":"1"
                },
                {
                    "key":"loop_count",
                    "value":"0"
                }
            ]
        })json");

    auto result = run({input1, input2});
    ASSERT_TRUE(result.ok());

    TestJsonEvaluator e(result.value());

    e.evaluate<int>("bpmn_edges", "SOURCE_ID", {0, 2, 3, 4, 6, 4, 7, 5, 8, 10, 10, 12, 11, 3, 6});
    e.evaluate<int>("bpmn_edges", "TARGET_ID", {2, 3, 4, 6, 5, 7, 5, 1, 10, 11, 12, 11, 3, 6, 9});
    e.evaluate<int>("bpmn_edges", "OBJECT_ID", {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1});
    e.evaluate<int64_t>("bpmn_edges", "OBJECT_COUNT", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

    e.evaluate<int>("bpmn_nodes", "NODE_ID", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    e.evaluate<int>("bpmn_nodes", "NODE_TYPE", {1, 2, 0, 0, 3, 3, 0, 0, 1, 2, 3, 3, 0});

    e.evaluate<std::string>("bpmn_activities", "ACTIVITY_NAME", {"A", "B", "C", "D", "E"});
    e.evaluate<int>("bpmn_activities", "NODE_ID", {2, 3, 6, 7, 12});

    e.evaluate<int>("bpmn_model_descriptions", "OBJECT_ID", {0, 1});
    e.evaluate<std::string>("bpmn_model_descriptions", "BPMN_MODEL_DESCRIPTION", {
            "[[0 BPMN_START][1 BPMN_END][2 BPMN_TASK 'A'][3 BPMN_TASK 'B'][4 BPMN_EXCLUSIVE_CHOICE][5 "
            "BPMN_EXCLUSIVE_CHOICE][6 BPMN_TASK 'C'][7 BPMN_TASK 'D']],[[0 2][2 3][3 4][4 6][4 7][5 1][6 5][7 5]]",
            "[[3 BPMN_TASK 'B'][6 BPMN_TASK 'C'][8 BPMN_START][9 BPMN_END][10 BPMN_EXCLUSIVE_CHOICE][11 "
            "BPMN_EXCLUSIVE_CHOICE][12 BPMN_TASK 'E']],[[3 6][6 9][8 10][10 11][10 12][11 3][12 11]]"});
}
TEST_F(CelonisMoBpmnGraphTest, invalid_json_spec) {
    Slice no_statistics(
            R"json({
            "vertex_properties": [],
            "edge_properties": []
            })json");

    auto result = run({no_statistics});
    EXPECT_TRUE(result.status().is_invalid_argument());
    EXPECT_THAT(std::string(result.status().message()), testing::HasSubstr("does not contain 'statistics'."));
}

TEST_F(CelonisMoBpmnGraphTest, null_input) {
    Columns input_columns;
    auto input_column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_VARCHAR), true);
    input_column->append_nulls(1);
    input_columns.push_back(std::move(input_column));

    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context());
    auto result = CelonisMoBpmnGraph::mo_bpmn_graph(ctx.get(), input_columns);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value()->size(), 1);
    EXPECT_TRUE(result.value()->get(0).is_null());
}

} // namespace starrocks