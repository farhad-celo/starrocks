#include <algorithm>
#include <gtest/gtest.h>

#include "column/column_builder.h"
#include "column/fixed_length_column.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/agg/enumerate_node_paths.h"
#include "gutil/strings/strcat.h"
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

class CelonisEnumerateNodePathsTest : public testing::Test {
public:
    CelonisEnumerateNodePathsTest() = default;

protected:
    void SetUp() override {}
    void TearDown() override {}

    TypeDescriptor logical_types_to_struct_type(const std::vector<LogicalType>& logical_types) {
        TypeDescriptor struct_type;
        struct_type.type = LogicalType::TYPE_STRUCT;
        for (int i = 0; i < logical_types.size(); ++i) {
            struct_type.children.emplace_back(logical_types[i]);
            struct_type.field_names.emplace_back(StrCat("col", i));
        }
        return struct_type;
    }

    TypeDescriptor get_return_type(const TypeDescriptor& value_type) {
        TypeDescriptor struct_type;
        struct_type.type = LogicalType::TYPE_STRUCT;
        for (int i = 0; i < value_type.children.size(); ++i) {
            TypeDescriptor array_type;
            array_type.type = LogicalType::TYPE_ARRAY;
            array_type.children.emplace_back(value_type.children[i]);
            struct_type.children.emplace_back(array_type);
            struct_type.field_names.emplace_back(value_type.field_names[i]);
        }

        return struct_type;
    }

    TypeDescriptor get_intermediate_type(FunctionContext* ctx) {
        TypeDescriptor struct_type;
        struct_type.type = LogicalType::TYPE_STRUCT;
        auto add_arrays = [&struct_type](const FunctionContext::TypeDesc& type) {
            for (int i = 0; i < type.children.size(); ++i) {
                TypeDescriptor array_type;
                array_type.type = LogicalType::TYPE_ARRAY;
                array_type.children.emplace_back(TypeDescriptor::from_logical_type(type.children[i].type));
                struct_type.children.emplace_back(array_type);
                struct_type.field_names.emplace_back(StrCat("c", i, "_", type.field_names[i]));
            }
        };

        for (int i = 0; i < 3; ++i) {
            if (ctx->get_constant_column(i) == nullptr) {
                add_arrays(*ctx->get_arg_type(i));
            }
        }
        for (int i = 3; i < 9; ++i) {
            if (ctx->get_constant_column(i) == nullptr) {
                TypeDescriptor array_type;
                array_type.type = LogicalType::TYPE_ARRAY;
                array_type.children.emplace_back(TypeDescriptor::from_logical_type(TYPE_BOOLEAN));
                struct_type.children.emplace_back(array_type);
                struct_type.field_names.emplace_back(StrCat("c", i));
            }
        }
        struct_type.children.emplace_back(TypeDescriptor::from_logical_type(TYPE_VARBINARY));
        struct_type.field_names.emplace_back(StrCat("c", 9));

        return struct_type;
    }

    std::unique_ptr<FunctionContext> get_ctx(const TypeDescriptor& value_type, const TypeDescriptor& pk_type) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                value_type,                                      // outColumns
                value_type,                                      // inColumns
                pk_type,                                         // pkColumns
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN), // outStart
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN), // outEnd
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN), // inStart
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN), // inEnd
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN), // outAll
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN), // inAll
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN), // allowCycles
                TypeDescriptor::from_logical_type(TYPE_TINYINT), // lengthComparison
                TypeDescriptor::from_logical_type(TYPE_BIGINT)   // length
        };
        auto return_type = get_return_type(value_type);
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_test_context(std::move(arg_types), return_type));
    }

    ColumnPtr prepare_input_column(FunctionContext* ctx, const std::vector<std::vector<DatumArray>>& input, int index, int size) {
        if (index >= input.size() || input[index].empty()) {
            return ColumnHelper::create_const_null_column(size);
        }
        auto column = ctx->create_column(*ctx->get_arg_type(index), false);
        auto& fields = down_cast<StructColumn*>(column.get())->fields_column();
        for (int i = 0; i < input[index].size(); ++i) {
            DCHECK_EQ(input[index][i].size(), size);
            for (const auto& datum : input[index][i]) {
                fields[i]->append_datum(datum);
            }
        }
        return column;
    }

    ColumnPtr prepare_option_column(FunctionContext* ctx, const std::vector<DatumArray>& options, int index, int size) {
        if (index >= options.size() || options[index].empty()) {
            return ColumnHelper::create_const_null_column(size);
        }
        DCHECK_EQ(options[index].size(), size);
        auto column = ColumnHelper::create_column(TypeDescriptor::from_logical_type(TYPE_BOOLEAN), false);
        for (int i = 0; i < size; ++i) {
            column->append_datum(options[index][i]);
        }
        return column;
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*>
    RunUpdate(const std::vector<LogicalType>& value_logical_types, const std::vector<LogicalType>& pk_logical_types,
              const std::vector<std::vector<DatumArray>>& input, const std::vector<DatumArray>& options,
              bool allow_cycles, const std::string& length_comparison, int length) {
        auto value_type = logical_types_to_struct_type(value_logical_types);
        auto pk_type = logical_types_to_struct_type(pk_logical_types);
        auto bool_type = TypeDescriptor::from_logical_type(TYPE_BOOLEAN);

        auto local_ctx = get_ctx(value_type, pk_type);

        const AggregateFunction* func = get_aggregate_function("celonis_enumerate_node_paths", TYPE_STRUCT, TYPE_STRUCT,
                                                               false);

        int size = input[0][0].size();
        Columns columns;
        for (int i = 0; i < 3; ++i) {
            columns.push_back(prepare_input_column(local_ctx.get(), input, i, size));
        }
        for (int i = 0; i < 6; ++i) {
            columns.push_back(prepare_option_column(local_ctx.get(), options, i, size));
        }
        columns.push_back(ColumnHelper::create_const_column<TYPE_BOOLEAN>(allow_cycles, size));
        columns.push_back(ColumnHelper::create_const_column<TYPE_VARCHAR>(length_comparison, size));
        columns.push_back(ColumnHelper::create_const_column<TYPE_BIGINT>(length, size));

        std::vector<ColumnPtr> const_columns;
        std::vector<const Column *> raw_columns;
        for (auto& column : columns) {
            if (column->is_constant()) {
                const_columns.push_back(column);
            } else {
                const_columns.push_back(nullptr);
            }
            raw_columns.push_back(column.get());
        }

        local_ctx->set_constant_columns(std::move(const_columns));

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), size, raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    void Evaluate(Column* result, const std::vector<std::vector<DatumArray>>& expected) {
        auto& fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(result))->fields_column();
        ASSERT_EQ(fields.size(), expected.size());
        for (int f = 0; f < fields.size(); ++f) {
            ASSERT_EQ(fields[f]->size(), expected[f].size());
            for (int row = 0; row < fields[f]->size(); ++row) {
                auto result_array = fields[f]->get(row).get_array();
                auto expected_array = expected[f][row];
                ASSERT_EQ(result_array.size(), expected_array.size()) << "row: " << row << "\n" << result->debug_string();
                bool is_slice = std::holds_alternative<Slice>(result_array[0].convert2DatumKey());
                for (int i = 0; i < expected_array.size(); ++i) {
                    auto debug_string = [&]() {
                        return fmt::format("field: {}, row: {}, index: {}\n{}", f, row, i, result->debug_string());
                    };
                    if (expected_array[i].is_null()) {
                        EXPECT_TRUE(result_array[i].is_null()) << debug_string();
                    } else if (result_array[i].is_null()) {
                        EXPECT_FALSE(result_array[i].is_null()) << debug_string();
                    } else if (is_slice) {
                        EXPECT_EQ(result_array[i].get_slice(), expected_array[i].get_slice()) << debug_string();
                    } else {
                        EXPECT_EQ(result_array[i].get_int64(), expected_array[i].get_int64()) << debug_string();
                    }
                }
            }
        }
    }

    void RunMerge(const std::vector<LogicalType>& value_logical_types, const std::vector<LogicalType>& pk_logical_types,
                  const std::vector<std::vector<DatumArray>>& input1, const std::vector<DatumArray>& options1,
                  const std::vector<std::vector<DatumArray>>& input2, const std::vector<DatumArray>& options2,
                  bool allow_cycles, const std::string& length_comparison, int length,
                  const std::vector<std::vector<DatumArray>>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(value_logical_types, pk_logical_types, input1, options1,
                                                    allow_cycles, length_comparison, length);
        auto [local_ctx2, state2, func2] = RunUpdate(value_logical_types, pk_logical_types, input2, options2,
                                                     allow_cycles, length_comparison, length);

        auto intermediate_type = get_intermediate_type(local_ctx1.get());

        // Serialize state2
        auto serde_col = ColumnHelper::create_column(intermediate_type, false);
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge state2 into state1
        func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

        // Get the result
        auto result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
        func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

        Evaluate(result.get(), expected);
    }

    void RunMergeToNew(const std::vector<LogicalType>& value_logical_types,
                       const std::vector<LogicalType>& pk_logical_types,
                       const std::vector<std::vector<DatumArray>>& input1, const std::vector<DatumArray>& options1,
                       const std::vector<std::vector<DatumArray>>& input2, const std::vector<DatumArray>& options2,
                       bool allow_cycles, const std::string& length_comparison, int length,
                       const std::vector<std::vector<DatumArray>>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(value_logical_types, pk_logical_types, input1, options1,
                                                    allow_cycles, length_comparison, length);
        auto [local_ctx2, state2, func2] = RunUpdate(value_logical_types, pk_logical_types, input2, options2,
                                                     allow_cycles, length_comparison, length);
        auto local_ctx3 = get_ctx(logical_types_to_struct_type(value_logical_types),
                                  logical_types_to_struct_type(pk_logical_types));

        auto intermediate_type = get_intermediate_type(local_ctx1.get());

        // Serialize state1 and state2
        auto serde_col = ColumnHelper::create_column(intermediate_type, false);
        func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
        func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

        // Merge to a new state
        auto state3 = ManagedAggrState::create(local_ctx3.get(), func);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
        func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);

        // Get the result
        auto result = local_ctx3->create_column(local_ctx3->get_return_type(), false);
        func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());

        Evaluate(result.get(), expected);
    }

    void Run(const std::vector<LogicalType>& value_logical_types,
                  const std::vector<LogicalType>& pk_logical_types,
                  const std::vector<std::vector<DatumArray>>& input1, const std::vector<DatumArray>& options1,
                  const std::vector<std::vector<DatumArray>>& input2, const std::vector<DatumArray>& options2,
                  bool allow_cycles, const std::string& length_comparison, int length,
                  const std::vector<std::vector<DatumArray>>& expected) {
        RunMerge(value_logical_types, pk_logical_types, input1, options1, input2, options2, allow_cycles,
                 length_comparison, length, expected);
        RunMergeToNew(value_logical_types, pk_logical_types, input1, options1, input2, options2, allow_cycles,
                      length_comparison, length, expected);
    }
};

TEST_F(CelonisEnumerateNodePathsTest, ex1_basic) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};
    auto pk_lts = std::vector<LogicalType>{LogicalType::TYPE_BIGINT};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "B", "C", "C"}},
                                                   {DatumArray{kNullDatum, "C", "D", "E"}}};
    std::vector<DatumArray> options1 = {};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"D", "D", "E", "E", "F"}},
                                                   {DatumArray{"F", "G", "G", "H", "G"}}};
    std::vector<DatumArray> options2 = {};
    bool allow_cycles = false;
    std::string lc = "LESS_EQUAL";
    int len = 10;

    std::vector<std::vector<DatumArray>> expected = {{DatumArray{"B", "C", "E", "H"},
                                                      DatumArray{"B", "C", "E", "G"},
                                                      DatumArray{"B", "C", "D", "G"},
                                                      DatumArray{"B", "C", "D", "F", "G"},
                                                      DatumArray{"A"}}};
    Run(value_lts, pk_lts, input1, options1, input2, options2, allow_cycles, lc, len, expected);
}

TEST_F(CelonisEnumerateNodePathsTest, ex2_options) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};
    auto pk_lts = std::vector<LogicalType>{LogicalType::TYPE_BIGINT};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "B", "C", "C"}},
                                                   {DatumArray{kNullDatum, "C", "D", "E"}}};
    std::vector<DatumArray> options1 = {DatumArray{false, false, true, true},
                                        DatumArray{false, false, false, false},
                                        DatumArray{false, true, false, false},
                                        DatumArray{false, false, false, false}};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"D", "D", "E", "E", "F"}},
                                                   {DatumArray{"F", "G", "G", "H", "G"}}};
    std::vector<DatumArray> options2 = {DatumArray{false, false, false, false, false},
                                        DatumArray{false, false, false, false, true},
                                        DatumArray{false, false, false, false, false},
                                        DatumArray{true, true, true, true, true}};
    bool allow_cycles = false;
    std::string lc = "LESS_EQUAL";
    int len = 10;

    std::vector<std::vector<DatumArray>> expected = {{DatumArray{"C", "E", "H"},
                                                      DatumArray{"C", "E", "G"},
                                                      DatumArray{"C", "D", "G"},
                                                      DatumArray{"C", "D", "F"},
                                                      DatumArray{"C", "D", "F", "G"}}};
    Run(value_lts, pk_lts, input1, options1, input2, options2, allow_cycles, lc, len, expected);
}

TEST_F(CelonisEnumerateNodePathsTest, ex3_all) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};
    auto pk_lts = std::vector<LogicalType>{LogicalType::TYPE_BIGINT};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "B", "C", "C", kNullDatum}},
                                                   {DatumArray{kNullDatum, "C", "D", "E", "I"}}};
    std::vector<DatumArray> options1 = {DatumArray{},
                                        DatumArray{},
                                        DatumArray{},
                                        DatumArray{},
                                        DatumArray{true, true, true, true, true},
                                        DatumArray{true, true, false, true, true}};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"D", "D", "E", "E", "F"}},
                                                   {DatumArray{"F", "G", "G", "H", "G"}}};
    std::vector<DatumArray> options2 = {DatumArray{},
                                        DatumArray{},
                                        DatumArray{},
                                        DatumArray{},
                                        DatumArray{false, false, true, true, true},
                                        DatumArray{true, true, true, true, true}};

    bool allow_cycles = false;
    std::string lc = "LESS_EQUAL";
    int len = 10;

    std::vector<std::vector<DatumArray>> expected = {
            {
                    DatumArray{"I"},
                    DatumArray{"B", "C", "E", "H"},
                    DatumArray{"B", "C", "E", "G"},
                    DatumArray{"A"},
            }
    };
    Run(value_lts, pk_lts, input1, options1, input2, options2, allow_cycles, lc, len, expected);
}

TEST_F(CelonisEnumerateNodePathsTest, ex4_pk_cycles) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};
    auto pk_lts = std::vector<LogicalType>{LogicalType::TYPE_BIGINT};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "A", "B", "B"}},
                                                   {DatumArray{"B", "B", "C", "C"}},
                                                   {DatumArray{1L, 2L, 3L, 4L}}};
    std::vector<DatumArray> options1 = {};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"B", "B", "C", "C", "F"}},
                                                   {DatumArray{"C", "E", "B", "D", "B"}},
                                                   {DatumArray{5L, 6L, 7L, 8L, 9L}}};
    std::vector<DatumArray> options2 = {};
    bool allow_cycles = true;
    std::string lc = "LESS_EQUAL";
    int len = 10;

    std::vector<std::vector<DatumArray>> expected = {{
                                                             DatumArray{"F", "B", "E"},
                                                             DatumArray{"F", "B", "C", "D"},
                                                             DatumArray{"F", "B", "C", "B", "E"},
                                                             DatumArray{"F", "B", "C", "B", "C", "D"},
                                                             DatumArray{"A", "B", "E"},
                                                             DatumArray{"A", "B", "C", "D"},
                                                             DatumArray{"A", "B", "C", "B", "E"},
                                                             DatumArray{"A", "B", "C", "B", "C", "D"},
                                                     }};
    Run(value_lts, pk_lts, input1, options1, input2, options2, allow_cycles, lc, len, expected);
}

TEST_F(CelonisEnumerateNodePathsTest, ex4_multiple_fields) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR, LogicalType::TYPE_BIGINT};
    auto pk_lts = std::vector<LogicalType>{LogicalType::TYPE_BIGINT, LogicalType::TYPE_VARCHAR};

    std::vector<std::vector<DatumArray>> input1 = {
            {DatumArray{"A", "A", "B", "B"}, DatumArray{1L, 1L, 2L, 2L}},
            {DatumArray{"B", "B", "C", "C"}, DatumArray{2L, 2L, 3L, 3L}},
            {DatumArray{1L, 2L, 3L, 4L}, DatumArray{"a", "b", "c", "d"}}
    };
    std::vector<DatumArray> options1 = {};
    std::vector<std::vector<DatumArray>> input2 = {
            {DatumArray{"B", "B", "C", "C", "F"}, DatumArray{2L, 2L, 3L, 3L, 6L}},
            {DatumArray{"C", "E", "B", "D", "B"}, DatumArray{3L, 5L, 2L, 4L, 2L}},
            {DatumArray{5L, 6L, 7L, 8L, 9L}, DatumArray{"e", "f", "g", "h", "i"}}
    };
    std::vector<DatumArray> options2 = {};
    bool allow_cycles = true;
    std::string lc = "LESS_EQUAL";
    int len = 10;

    std::vector<std::vector<DatumArray>> expected = {
            {
                    DatumArray{"F", "B", "E"},
                    DatumArray{"F", "B", "C", "D"},
                    DatumArray{"F", "B", "C", "B", "E"},
                    DatumArray{"F", "B", "C", "B", "C", "D"},
                    DatumArray{"A", "B", "E"},
                    DatumArray{"A", "B", "C", "D"},
                    DatumArray{"A", "B", "C", "B", "E"},
                    DatumArray{"A", "B", "C", "B", "C", "D"},
            },
            {
                    DatumArray{6L, 2L, 5L},
                    DatumArray{6L, 2L, 3L, 4L},
                    DatumArray{6L, 2L, 3L, 2L, 5L},
                    DatumArray{6L, 2L, 3L, 2L, 3L, 4L},
                    DatumArray{1L, 2L, 5L},
                    DatumArray{1L, 2L, 3L, 4L},
                    DatumArray{1L, 2L, 3L, 2L, 5L},
                    DatumArray{1L, 2L, 3L, 2L, 3L, 4L},
            },
    };
    Run(value_lts, pk_lts, input1, options1, input2, options2, allow_cycles, lc, len, expected);
}

TEST_F(CelonisEnumerateNodePathsTest, ex4_pk_with_multiple_chunks) {
    auto old_vector_chunk_size = config::vector_chunk_size;
    config::vector_chunk_size = 4;

    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};
    auto pk_lts = std::vector<LogicalType>{LogicalType::TYPE_BIGINT};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "A", "B", "B"}},
                                                   {DatumArray{"B", "B", "C", "C"}},
                                                   {DatumArray{1L, 2L, 3L, 4L}}};
    std::vector<DatumArray> options1 = {};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"B", "B", "C", "C", "F"}},
                                                   {DatumArray{"C", "E", "B", "D", "B"}},
                                                   {DatumArray{5L, 6L, 7L, 8L, 9L}}};
    std::vector<DatumArray> options2 = {};
    bool allow_cycles = true;
    std::string lc = "LESS_EQUAL";
    int len = 10;

    auto [local_ctx1, state1, func] = RunUpdate(value_lts, pk_lts, input1, options1, allow_cycles, lc, len);
    auto [local_ctx2, state2, func2] = RunUpdate(value_lts, pk_lts, input2, options2, allow_cycles, lc, len);

    auto intermediate_type = get_intermediate_type(local_ctx1.get());

    // Serialize state2
    auto serde_col = ColumnHelper::create_column(intermediate_type, false);
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result

    // First chunk
    auto result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    std::vector<std::vector<DatumArray>> expected = {{
                                                             DatumArray{"F", "B", "E"},
                                                             DatumArray{"F", "B", "C", "D"},
                                                             DatumArray{"F", "B", "C", "B", "E"},
                                                             DatumArray{"F", "B", "C", "B", "C", "D"},
                                                     }};
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 4);
    Evaluate(result.get(), expected);

    // Second chunk
    result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    expected = {{
                        DatumArray{"A", "B", "E"},
                        DatumArray{"A", "B", "C", "D"},
                        DatumArray{"A", "B", "C", "B", "E"},
                        DatumArray{"A", "B", "C", "B", "C", "D"},
                }};
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 4);
    Evaluate(result.get(), expected);

    // Last empty chunk
    result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 0);

    config::vector_chunk_size = old_vector_chunk_size;
}

TEST_F(CelonisEnumerateNodePathsTest, ex4_pk_with_multiple_chunks_not_aligned) {
    auto old_vector_chunk_size = config::vector_chunk_size;
    config::vector_chunk_size = 3;

    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};
    auto pk_lts = std::vector<LogicalType>{LogicalType::TYPE_BIGINT};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "A", "B", "B"}},
                                                   {DatumArray{"B", "B", "C", "C"}},
                                                   {DatumArray{1L, 2L, 3L, 4L}}};
    std::vector<DatumArray> options1 = {};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"B", "B", "C", "C", "F"}},
                                                   {DatumArray{"C", "E", "B", "D", "B"}},
                                                   {DatumArray{5L, 6L, 7L, 8L, 9L}}};
    std::vector<DatumArray> options2 = {};
    bool allow_cycles = true;
    std::string lc = "LESS_EQUAL";
    int len = 10;

    auto [local_ctx1, state1, func] = RunUpdate(value_lts, pk_lts, input1, options1, allow_cycles, lc, len);
    auto [local_ctx2, state2, func2] = RunUpdate(value_lts, pk_lts, input2, options2, allow_cycles, lc, len);

    auto intermediate_type = get_intermediate_type(local_ctx1.get());

    // Serialize state2
    auto serde_col = ColumnHelper::create_column(intermediate_type, false);
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result

    // First chunk
    auto result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    std::vector<std::vector<DatumArray>> expected = {{
                                                             DatumArray{"F", "B", "E"},
                                                             DatumArray{"F", "B", "C", "D"},
                                                             DatumArray{"F", "B", "C", "B", "E"},
                                                     }};
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 3);
    Evaluate(result.get(), expected);

    // Second chunk
    result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    expected = {{
                        DatumArray{"F", "B", "C", "B", "C", "D"},
                        DatumArray{"A", "B", "E"},
                        DatumArray{"A", "B", "C", "D"},
                }};
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 3);
    Evaluate(result.get(), expected);

    // Third chunk
    result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    expected = {{
                        DatumArray{"A", "B", "C", "B", "E"},
                        DatumArray{"A", "B", "C", "B", "C", "D"},
                }};
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 2);
    Evaluate(result.get(), expected);

    // Last empty chunk
    result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 0);

    config::vector_chunk_size = old_vector_chunk_size;
}

TEST_F(CelonisEnumerateNodePathsTest, ex4_pk_greater_equal) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};
    auto pk_lts = std::vector<LogicalType>{LogicalType::TYPE_BIGINT};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "A", "B", "B"}},
                                                   {DatumArray{"B", "B", "C", "C"}},
                                                   {DatumArray{1L, 2L, 3L, 4L}}};
    std::vector<DatumArray> options1 = {};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"B", "B", "C", "C", "F"}},
                                                   {DatumArray{"C", "E", "B", "D", "B"}},
                                                   {DatumArray{5L, 6L, 7L, 8L, 9L}}};
    std::vector<DatumArray> options2 = {};
    bool allow_cycles = true;
    std::string lc = "GREATER_EQUAL";
    int len = 5;

    std::vector<std::vector<DatumArray>> expected = {{
                                                             DatumArray{"F", "B", "C", "B", "E"},
                                                             DatumArray{"F", "B", "C", "B", "C", "D"},
                                                             DatumArray{"A", "B", "C", "B", "E"},
                                                             DatumArray{"A", "B", "C", "B", "C", "D"},
                                                     }};
    Run(value_lts, pk_lts, input1, options1, input2, options2, allow_cycles, lc, len, expected);
}

} // namespace starrocks
