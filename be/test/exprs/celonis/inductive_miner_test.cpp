#include <algorithm>
#include <gtest/gtest.h>
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

class CelonisInductiveMinerTest : public testing::Test {
public:
    CelonisInductiveMinerTest() = default;

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

    void Run(const VariantRows& variant_rows, std::string expected) {
        const AggregateFunction* func = get_aggregate_function("celonis_inductive_miner", TYPE_ARRAY, TYPE_VARCHAR, false);
        auto variants = build_variant_column(variant_rows);
        auto weights = ColumnHelper::create_const_column<TYPE_BIGINT>(1, variants->size());
        std::vector<const Column*> raw_columns;
        raw_columns.resize(2);
        raw_columns[0] = variants.get();
        raw_columns[1] = weights.get();
        auto state = ManagedAggrState::create(ctx, func);
        func->update_batch_single_state(ctx, variants->size(), raw_columns.data(), state->state());

        // Get the result
        auto result = BinaryColumn::create();
        func->finalize_to_column(ctx, state->state(), result.get());

        ASSERT_EQ(result->size(), 1);
        auto rs = result->get_slice(0).to_string();
        // TODO(j.kim): Make tests less fragile.
        re2::RE2::GlobalReplace(&rs, "[\\t ]+", "");
        re2::RE2::GlobalReplace(&expected, "[\\t ]+", "");
        EXPECT_EQ(rs, expected);
    }

private:
    FunctionUtils* utils{};
    FunctionContext* ctx{};
};

TEST_F(CelonisInductiveMinerTest, Q1) {
    VariantRows variants = {{"A", "B", "E"},
                            {"A", "B", "F"}};
    std::string expected =
        R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "E"
                },
                {
                    "process_tree_type": 1,
                    "activity": "F"
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
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, Q2) {
    VariantRows variants = {{"A", "C", "D"},
                            {"A", "D", "C"},
                            {"A", "A", "C", "D"},
                            {"B", "C", "D"}};
    std::string expected =
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
                    "process_tree_type": 4,
                    "activity": null
                },
                {
                    "process_tree_type": 5,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 1,
                    "activity": "D"
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 0,
                    "activity": null
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
                    "edge_source_id": 1,
                    "edge_target_id": 3
                },
                {
                    "edge_source_id": 1,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 5
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 6
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 7
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 8
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L1) {
    VariantRows variants = {{"A", "B", "C", "D"},
                            {"A", "C", "B", "D"},
                            {"A", "E", "D"}};
    std::string expected =
        R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "D"
                },
                {
                    "process_tree_type": 4,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "E"
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
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
                    "edge_source_id": 2,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 5
                },
                {
                    "edge_source_id": 4,
                    "edge_target_id": 6
                },
                {
                    "edge_source_id": 4,
                    "edge_target_id": 7
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L2) {
    VariantRows variants = {{"A", "B", "C", "D"},
                            {"A", "C", "B", "D"},
                            {"A", "B", "C", "E", "F", "B", "C", "D"},
                            {"A", "C", "B", "E", "F", "B", "C", "D"},
                            {"A", "B", "C", "E", "F", "C", "B", "D"},
                            {"A", "C", "B", "E", "F", "B", "C", "E", "F", "C", "B", "D"}};
    std::string expected =
        R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 5,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "D"
                },
                {
                    "process_tree_type": 4,
                    "activity": null
                },
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 1,
                    "activity": "E"
                },
                {
                    "process_tree_type": 1,
                    "activity": "F"
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
                    "edge_source_id": 2,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 5
                },
                {
                    "edge_source_id": 4,
                    "edge_target_id": 6
                },
                {
                    "edge_source_id": 4,
                    "edge_target_id": 7
                },
                {
                    "edge_source_id": 5,
                    "edge_target_id": 8
                },
                {
                    "edge_source_id": 5,
                    "edge_target_id": 9
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L3) {
    VariantRows variants = {{"A", "B", "C", "D", "E", "F", "B", "D", "C", "E", "G"},
                            {"A", "B", "D", "C", "E", "G"},
                            {"A", "B", "C", "D", "E", "F", "B", "C", "D", "E", "F", "B", "D", "C", "E", "G"}};
    std::string expected =
        R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 5,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "G"
                },
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "F"
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 4,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "E"
                },
                {
                    "process_tree_type": 1,
                    "activity": "D"
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
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
                    "edge_source_id": 2,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 5
                },
                {
                    "edge_source_id": 4,
                    "edge_target_id": 6
                },
                {
                    "edge_source_id": 4,
                    "edge_target_id": 7
                },
                {
                    "edge_source_id": 4,
                    "edge_target_id": 8
                },
                {
                    "edge_source_id": 7,
                    "edge_target_id": 9
                },
                {
                    "edge_source_id": 7,
                    "edge_target_id": 10
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L4) {
    VariantRows variants = {{"A", "C", "D"},
                            {"B", "C", "D"},
                            {"A", "C", "E"},
                            {"B", "C", "E"}};
    std::string expected =
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
                    "activity": "C"
                },
                {
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 1,
                    "activity": "D"
                },
                {
                    "process_tree_type": 1,
                    "activity": "E"
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
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 6
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 7
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L5) {
    VariantRows variants = {{"A", "B", "E", "F"},
                            {"A", "B", "E", "C", "D", "B", "F"},
                            {"A", "B", "C", "E", "D", "B", "F"},
                            {"A", "B", "C", "D", "E", "B", "F"},
                            {"A", "E", "B", "C", "D", "B", "F"}};
    std::string expected =
        R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 4,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "F"
                },
                {
                    "process_tree_type": 1,
                    "activity": "E"
                },
                {
                    "process_tree_type": 5,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
                },
                {
                    "process_tree_type": 1,
                    "activity": "D"
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
                    "edge_source_id": 2,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 5
                },
                {
                    "edge_source_id": 5,
                    "edge_target_id": 6
                },
                {
                    "edge_source_id": 5,
                    "edge_target_id": 7
                },
                {
                    "edge_source_id": 7,
                    "edge_target_id": 8
                },
                {
                    "edge_source_id": 7,
                    "edge_target_id": 9
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L6) {
    VariantRows variants = {{"A", "C", "E", "G"},
                            {"A", "E", "C", "G"},
                            {"B", "D", "F", "G"},
                            {"B", "F", "D", "G"}};
    std::string expected =
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
                    "activity": "G"
                },
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 4,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 4,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "E"
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
                },
                {
                    "process_tree_type": 1,
                    "activity": "F"
                },
                {
                    "process_tree_type": 1,
                    "activity": "D"
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
                    "edge_source_id": 1,
                    "edge_target_id": 3
                },
                {
                    "edge_source_id": 1,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 5
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 6
                },
                {
                    "edge_source_id": 4,
                    "edge_target_id": 7
                },
                {
                    "edge_source_id": 4,
                    "edge_target_id": 8
                },
                {
                    "edge_source_id": 6,
                    "edge_target_id": 9
                },
                {
                    "edge_source_id": 6,
                    "edge_target_id": 10
                },
                {
                    "edge_source_id": 8,
                    "edge_target_id": 11
                },
                {
                    "edge_source_id": 8,
                    "edge_target_id": 12
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L7) {
    VariantRows variants = {{"A", "C"},
                            {"A", "B", "C"},
                            {"A", "B", "B", "C"},
                            {"A", "B", "B", "B", "B", "C"}};
    std::string expected =
        R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 5,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
                },
                {
                    "process_tree_type": 0,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
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
                    "edge_source_id": 2,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 5
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L8) {
    VariantRows variants = {{"A", "B", "D"},
                            {"A", "B", "C", "B", "D"},
                            {"A", "B", "C", "B", "C", "B", "D"}};
    std::string expected =
        R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 5,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "D"
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
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
                    "edge_source_id": 2,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 5
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L9) {
    VariantRows variants = {{"A", "C", "D"},
                            {"B", "C", "E"}};
    std::string expected =
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
                    "activity": "C"
                },
                {
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 1,
                    "activity": "D"
                },
                {
                    "process_tree_type": 1,
                    "activity": "E"
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
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 6
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 7
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L10) {
    VariantRows variants = {{"A", "A"}};
    std::string expected =
        R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 5,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 0,
                    "activity": null
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
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, L11) {
    VariantRows variants = {{"A", "B", "C"},
                            {"A", "C"}};
    std::string expected =
        R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
                },
                {
                    "process_tree_type": 0,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
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
                    "edge_source_id": 2,
                    "edge_target_id": 4
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 5
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, EmptyLogBaseCase) {
    VariantRows variants = {};
    std::string expected =
        R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 0,
                    "activity": null
                }
            ],
            "edge_properties": []
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, SequenceWithSkippedActivities) {
    VariantRows variants = {{"A", "B", "C"},
                            {"B", "C"},
                            {"A", "C"},
                            {"A", "B"}};
    std::string expected =
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
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 2,
                    "activity": null
                },
                {
                    "process_tree_type": 0,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 0,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 0,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
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
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 6
                },
                {
                    "edge_source_id": 2,
                    "edge_target_id": 7
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 8
                },
                {
                    "edge_source_id": 3,
                    "edge_target_id": 9
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, SequenceSkippingFirstActivity_2xBC_1xABC) {
    VariantRows variants = {{"B", "C"},
                            {"B", "C"},
                            {"A", "B", "C"}};
    std::string expected =
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
                    "activity": "B"
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
                },
                {
                    "process_tree_type": 0,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
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
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, SequenceSkippingFirstActivity_1xBC_2xABC) {
    VariantRows variants = {{"B", "C"},
                            {"A", "B", "C"},
                            {"A", "B", "C"}};
    std::string expected =
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
                    "activity": "B"
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
                },
                {
                    "process_tree_type": 0,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
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
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, SecretTauLoopFallback) {
    VariantRows variants = {{"A", "B", "C", "A", "B", "C"}};
    std::string expected =
            R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 5,
                    "activity": null
                },
                {
                    "process_tree_type": 3,
                    "activity": null
                },
                {
                    "process_tree_type": 0,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
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
                    "edge_source_id": 1,
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
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, RepeatedLogs) {
    VariantRows variants = {{"A", "B", "A"},
                            {"A", "B", "A"}};
    std::string expected =
            R"json({
            "vertex_properties": [
                {
                    "process_tree_type": 5,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "A"
                },
                {
                    "process_tree_type": 1,
                    "activity": "B"
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
                }
            ]
        })json";
    Run(variants, expected);
}

TEST_F(CelonisInductiveMinerTest, SkipFirst) {
    VariantRows variants = {{"E", "B", "C"},
                            {"B", "C"}};
    std::string expected =
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
                    "activity": "B"
                },
                {
                    "process_tree_type": 1,
                    "activity": "C"
                },
                {
                    "process_tree_type": 0,
                    "activity": null
                },
                {
                    "process_tree_type": 1,
                    "activity": "E"
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
            ]
        })json";
    Run(variants, expected);
}

// While there are 10 more tests in Saola inductive_miner_test.cpp, the above tests would be enough to check porting.

} // namespace starrocks