#include <algorithm>
#include <gtest/gtest.h>
#include <random>
#include <re2/re2.h>

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/fixed_length_column.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/celonis/variant_stats.h"
#include "runtime/mem_pool.h"
#include "testutil/function_utils.h"
#include "util/slice.h"

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

class CelonisAlignModelTest : public testing::Test {
public:
    CelonisAlignModelTest() = default;

    typedef std::vector<std::vector<std::string>> VariantRows;

    void SetUp() override {
        utils = new FunctionUtils();
        ctx = utils->get_fn_ctx();
    }

    void TearDown() override { delete utils; }

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

    Column::Ptr build_weight_column(const std::vector<int>& weights) {
        ColumnBuilder<TYPE_BIGINT> builder(config::vector_chunk_size);

        for (int i = 0; i < weights.size(); i++) {
            builder.append(weights[i]);
        }
        return builder.build(false);
    }

    Column::Ptr build_random_weight_column(int size) {
        ColumnBuilder<TYPE_BIGINT> builder(config::vector_chunk_size);

        std::random_device rd;
        std::uniform_int_distribution<size_t> weight(1, 10000);

        for (int i = 0; i < size; i++) {
            builder.append(weight(rd));
        }
        return builder.build(false);
    }

    Column::Ptr build_const_weight_column(int weight, int size) {
        return ColumnHelper::create_const_column<TYPE_BIGINT>(weight, size);
    }

    void Run(const VariantRows& variant_rows, Column::Ptr weight_column,
             const std::string& bpmn_model_description_json, std::string expected) {
        const AggregateFunction* func = get_aggregate_function("celonis_align_model", TYPE_ARRAY, TYPE_VARCHAR, false);
        auto variants = build_variant_column(variant_rows);
        auto model_column =
                ColumnHelper::create_const_column<TYPE_VARCHAR>(bpmn_model_description_json, variant_rows.size());

        Columns columns;
        columns.push_back(variants);
        columns.push_back(weight_column);
        columns.push_back(model_column);
        ctx->set_constant_columns(columns);

        std::vector<const Column*> raw_columns;
        raw_columns.resize(3);
        raw_columns[0] = variants.get();
        raw_columns[1] = weight_column.get();
        raw_columns[2] = model_column.get();
        auto state = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, variants->size(), raw_columns.data(), state->state());

        // Get the result
        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, state->state(), result.get());

        ASSERT_EQ(result->size(), 1);
        auto rs = result->get_slice(0).to_string();
        std::cout << rs;
        re2::RE2::GlobalReplace(&rs, "[\\t\\n ]+", "");
        re2::RE2::GlobalReplace(&expected, "[\\t\\n ]+", "");
        EXPECT_EQ(rs, expected);
    }

    void Run(const VariantRows& variant_rows, const std::string& bpmn_model_description_json,
             const std::string& expected) {
        int size = variant_rows.size();
        Run(variant_rows, build_const_weight_column(1, size), bpmn_model_description_json, expected);
        Run(variant_rows, build_const_weight_column(1000, size), bpmn_model_description_json, expected);
        for (int i = 0; i < 100; i++) {
            Run(variant_rows, build_random_weight_column(size), bpmn_model_description_json, expected);
        }
    }

private:
    FunctionUtils* utils{};
    FunctionContext* ctx{};
};

TEST_F(CelonisAlignModelTest, Parallel) {
    VariantRows variants = {{"A", "C"},
                            {"A", "B", "C"},
                            {"C", "B", "B"}};
    std::string model =
        R"json({
            "nodes": [
                {
                    "node_id": 0,
                    "node_type": 4
                },
                {
                    "node_id": 1,
                    "node_type": 1,
                    "task_name": "A"
                },
                {
                    "node_id": 2,
                    "node_type": 3
                },
                {
                    "node_id": 3,
                    "node_type": 1,
                    "task_name": "B"
                },
                {
                    "node_id": 4,
                    "node_type": 1,
                    "task_name": "C"
                },
                {
                    "node_id": 5,
                    "node_type": 3
                },
                {
                    "node_id": 6,
                    "node_type": 5
                }
            ],
            "edges": [
                {
                    "from": 0,
                    "to": 1
                },
                {
                    "from": 1,
                    "to": 2
                },
                {
                    "from": 2,
                    "to": 3
                },
                {
                    "from": 2,
                    "to": 4
                },
                {
                    "from": 3,
                    "to": 5
                },
                {
                    "from": 4,
                    "to": 5
                },
                {
                    "from": 5,
                    "to": 6
                }
            ],
            "cache_key": "CACHE_KEY"
        })json";
    std::string expected =
        R"json({
            "alignment": [
                {
                    "variant": ["A", "C"],
                    "model_vertex_id": [0, 1, 2, 4, 3, 5, 6],
                    "vertex_label": [ "BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK C", "BPMN_TASK B", "BPMN_PARALLEL", "BPMN_END"],
                    "move_type": ["GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"],
                    "activity_index": [0, 0, 0, 1, 0, 1, 1]
                },
                {
                    "variant": ["A", "B", "C"],
                    "model_vertex_id": [0, 1, 2, 3, 4, 5, 6],
                    "vertex_label": ["BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK B", "BPMN_TASK C", "BPMN_PARALLEL", "BPMN_END"],
                    "move_type": ["GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"],
                    "activity_index": [0, 0, 0, 1, 2, 2, 2]
                },
                {
                    "variant": ["C", "B", "B"],
                    "model_vertex_id": [0, 1, 2, 4, 3, 3, 5, 6],
                    "vertex_label": ["BPMN_START", "BPMN_TASK A", "BPMN_PARALLEL", "BPMN_TASK C", "BPMN_TASK B", "BPMN_TASK B", "BPMN_PARALLEL", "BPMN_END"],
                    "move_type": ["GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "LOG_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"],
                    "activity_index": [0, 0, 0, 0, 1, 2, 1, 2]
                }
            ],
            "association": [
                {
                    "variant": ["A", "C"],
                    "edge_class": [0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2],
                    "alignment_index": [0, 1, 2, 3, 5, 6, 2, 4, 5, 2, 5]
                },
                {
                    "variant": ["A", "B", "C"],
                    "edge_class": [0, 0, 0, 0, 0, 0, 1, 1, 1],
                    "alignment_index": [0, 1, 2, 3, 5, 6, 2, 4, 5]
                },
                {
                    "variant": ["C", "B", "B"],
                    "edge_class": [0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4],
                    "alignment_index": [2, 3, 6, 7, 2, 4, 6, 0, 1, 2, 0, 2, 4, 5, 7]
                }
            ],
            "edge_class": [
                {
                    "variant": ["A", "C"],
                    "id": [0, 1, 2],
                    "type": ["SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE"]
                },
                {
                    "variant": ["A", "B", "C"],
                    "id": [0, 1],
                    "type": ["SYNC_EDGE", "SYNC_EDGE"]
                },
                {
                    "variant": ["C", "B", "B"],
                    "id": [0, 1, 2, 3, 4],
                    "type": ["SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE", "LOG_EDGE"]
                }
            ]
        })json";
    Run(variants, model, expected);
}

TEST_F(CelonisAlignModelTest, Loop) {
    VariantRows variants = {{"A", "B", "C", "A", "B"},
                            {"A", "B", "C"},
                            {"A", "B", "A", "B"}};
    std::string model =
            R"json({
            "nodes": [
                {
                    "node_id": 0,
                    "node_type": 4
                },
                {
                    "node_id": 1,
                    "node_type": 2
                },
                {
                    "node_id": 2,
                    "node_type": 1,
                    "task_name": "A"
                },
                {
                    "node_id": 3,
                    "node_type": 1,
                    "task_name": "B"
                },
                {
                    "node_id": 4,
                    "node_type": 2
                },
                {
                    "node_id": 5,
                    "node_type": 1,
                    "task_name": "C"
                },
                {
                    "node_id": 6,
                    "node_type": 5
                }
            ],
            "edges": [
                {
                    "from": 0,
                    "to": 1
                },
                {
                    "from": 1,
                    "to": 2
                },
                {
                    "from": 2,
                    "to": 3
                },
                {
                    "from": 3,
                    "to": 4
                },
                {
                    "from": 4,
                    "to": 5
                },
                {
                    "from": 4,
                    "to": 6
                },
                {
                    "from": 5,
                    "to": 1
                }
            ],
            "cache_key": "CACHE_KEY"
        })json";
    std::string expected =
            R"json({
            "alignment": [
                {
                    "variant": ["A", "B", "C", "A", "B"],
                    "model_vertex_id": [0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6],
                    "vertex_label": [ "BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK C", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"],
                    "move_type": ["GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"],
                    "activity_index": [0, 0, 0, 1, 1, 2, 2, 3, 4, 4, 4]
                },
                {
                    "variant": ["A", "B", "C"],
                    "model_vertex_id": [0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6],
                    "vertex_label": [ "BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK C", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"],
                    "move_type": ["GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "MODEL_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"],
                    "activity_index": [0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2]
                },
                {
                    "variant": ["A", "B", "A", "B"],
                    "model_vertex_id": [0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6],
                    "vertex_label": [ "BPMN_START", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK C", "BPMN_EXCLUSIVE_CHOICE", "BPMN_TASK A", "BPMN_TASK B", "BPMN_EXCLUSIVE_CHOICE", "BPMN_END"],
                    "move_type": ["GATEWAY_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "MODEL_MOVE", "GATEWAY_MOVE", "SYNC_MOVE", "SYNC_MOVE", "GATEWAY_MOVE", "GATEWAY_MOVE"],
                    "activity_index": [0, 0, 0, 1, 1, 1, 1, 2, 3, 3, 3]
                }
            ],
            "association": [
                {
                    "variant": ["A", "B", "C", "A", "B"],
                    "edge_class": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                    "alignment_index": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
                },
                {
                    "variant": ["A", "B", "C"],
                    "edge_class": [0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 3, 3],
                    "alignment_index": [0, 1, 2, 3, 4, 5, 6, 9, 10, 6, 7, 8, 9, 6, 9]
                },
                {
                    "variant": ["A", "B", "A", "B"],
                    "edge_class": [0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3],
                    "alignment_index": [0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 4, 5, 6, 4, 6]
                }
            ],
            "edge_class": [
                {
                    "variant": ["A", "B", "C", "A", "B"],
                    "id": [0],
                    "type": ["SYNC_EDGE"]
                },
                {
                    "variant": ["A", "B", "C"],
                    "id": [0, 1, 2, 3],
                    "type": ["SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE"]
                },
                {
                    "variant": ["A", "B", "A", "B"],
                    "id": [0, 1, 2, 3],
                    "type": ["SYNC_EDGE", "SYNC_EDGE", "MODEL_EDGE", "SKIP_EDGE"]
                }
            ]
        })json";
    Run(variants, model, expected);
}
} // namespace starrocks