#include <gtest/gtest.h>

#include <algorithm>
#include <optional>

#include "column/column_builder.h"
#include "column/fixed_length_column.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/celonis/agg/enumerate_node_paths.h"
#include "exprs/celonis/anyval_util.h"
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

class CelonisEnumerateTransitiveEdgesTest : public testing::Test {
public:
    CelonisEnumerateTransitiveEdgesTest() = default;

protected:
    void SetUp() override {}
    void TearDown() override {}

private:
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
        struct_type.children.emplace_back(value_type);
        struct_type.field_names.emplace_back("from");
        struct_type.children.emplace_back(value_type);
        struct_type.field_names.emplace_back("to");

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

        for (int i = 0; i < 2; ++i) {
            if (ctx->get_constant_column(i) == nullptr) {
                add_arrays(*ctx->get_arg_type(i));
            }
        }
        struct_type.children.emplace_back(TypeDescriptor::from_logical_type(TYPE_VARBINARY));
        struct_type.field_names.emplace_back(StrCat("c", 2));

        return struct_type;
    }

    std::unique_ptr<FunctionContext> get_ctx(const TypeDescriptor& value_type) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                CelonisAnyValUtil::column_type_to_type_desc(value_type), // outColumns
                CelonisAnyValUtil::column_type_to_type_desc(value_type), // inColumns
                CelonisAnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT)) // maxLength
        };
        auto return_type = CelonisAnyValUtil::column_type_to_type_desc(get_return_type(value_type));
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_test_context(std::move(arg_types), return_type));
    }

    ColumnPtr prepare_input_column(FunctionContext* ctx, const std::vector<std::vector<DatumArray>>& input, int index,
                                   int size) {
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

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*> RunUpdate(
            const std::vector<LogicalType>& value_logical_types, const std::vector<std::vector<DatumArray>>& input,
            const std::optional<int64_t>& max_length) {
        auto value_type = logical_types_to_struct_type(value_logical_types);

        auto local_ctx = get_ctx(value_type);

        const AggregateFunction* func =
                get_aggregate_function("celonis_enumerate_transitive_edges", TYPE_STRUCT, TYPE_STRUCT, false);

        int size = input[0][0].size();
        Columns columns;
        for (int i = 0; i < 2; ++i) {
            columns.push_back(prepare_input_column(local_ctx.get(), input, i, size));
        }
        columns.push_back(max_length.has_value() ? ColumnHelper::create_const_column<TYPE_BIGINT>(*max_length, size)
                                                 : ColumnHelper::create_const_null_column(size));

        std::vector<ColumnPtr> const_columns;
        std::vector<const Column*> raw_columns;
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

    void Evaluate(Column* result, const std::vector<std::vector<DatumArray>>& expected, bool reset_expected_set = true,
                  bool check_remaining_expected_set = true) {
        ASSERT_EQ(expected.size(), 2);
        int num_fields = expected[0].size();
        int num_rows = expected[0][0].size();
        std::vector<bool> is_slice;
        for (int field = 0; field < num_fields; ++field) {
            // Alternatively, we can get this from value_logical_types or ctx if we pass them here.
            is_slice.push_back(std::holds_alternative<Slice>(expected[0][field][0].convert2DatumKey()));
            ASSERT_EQ(expected[0][field].size(), num_rows);
            ASSERT_EQ(expected[1][field].size(), num_rows);
        }
        if (reset_expected_set) {
            for (int row = 0; row < num_rows; ++row) {
                std::string from = "from:";
                std::string to = ",to:";
                for (int field = 0; field < num_fields; ++field) {
                    if (is_slice[field]) {
                        StrAppend(&from, expected[0][field][row].get_slice().to_string());
                        StrAppend(&to, expected[1][field][row].get_slice().to_string());
                    } else {
                        StrAppend(&from, expected[0][field][row].get_int64());
                        StrAppend(&to, expected[1][field][row].get_int64());
                    }
                }
                expected_set_.insert(from + to);
            }
        }

        auto& fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(result))->fields_column();
        ASSERT_EQ(fields.size(), 2);
        auto& from_fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[0].get()))->fields_column();
        ASSERT_EQ(from_fields.size(), num_fields);
        auto& to_fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[1].get()))->fields_column();
        ASSERT_EQ(to_fields.size(), num_fields);
        for (int row = 0; row < result->size(); ++row) {
            std::string from = "from:";
            std::string to = ",to:";
            for (int field = 0; field < num_fields; ++field) {
                if (is_slice[field]) {
                    StrAppend(&from, from_fields[field]->get(row).get_slice().to_string());
                    StrAppend(&to, to_fields[field]->get(row).get_slice().to_string());
                } else {
                    StrAppend(&from, from_fields[field]->get(row).get_int64());
                    StrAppend(&to, to_fields[field]->get(row).get_int64());
                }
            }
            auto erased = expected_set_.erase(from + to);
            EXPECT_EQ(erased, 1) << "Not expected but received " << from << to;
        }
        if (check_remaining_expected_set) {
            for (const auto& item : expected_set_) {
                EXPECT_TRUE(false) << "Expected but not received " << item;
            }
        }
    }

    void RunMerge(const std::vector<LogicalType>& value_logical_types,
                  const std::vector<std::vector<DatumArray>>& input1,
                  const std::vector<std::vector<DatumArray>>& input2, int max_length,
                  const std::vector<std::vector<DatumArray>>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(value_logical_types, input1, max_length);
        auto [local_ctx2, state2, func2] = RunUpdate(value_logical_types, input2, max_length);

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
                       const std::vector<std::vector<DatumArray>>& input1,
                       const std::vector<std::vector<DatumArray>>& input2, int max_length,
                       const std::vector<std::vector<DatumArray>>& expected) {
        auto [local_ctx1, state1, func] = RunUpdate(value_logical_types, input1, max_length);
        auto [local_ctx2, state2, func2] = RunUpdate(value_logical_types, input2, max_length);
        auto local_ctx3 = get_ctx(logical_types_to_struct_type(value_logical_types));

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

    void Run(const std::vector<LogicalType>& value_logical_types, const std::vector<std::vector<DatumArray>>& input1,
             const std::vector<std::vector<DatumArray>>& input2, int max_length,
             const std::vector<std::vector<DatumArray>>& expected) {
        RunMerge(value_logical_types, input1, input2, max_length, expected);
        RunMergeToNew(value_logical_types, input1, input2, max_length, expected);
    }

    phmap::flat_hash_set<std::string> expected_set_;
};

TEST_F(CelonisEnumerateTransitiveEdgesTest, constant_null_max_length_uses_default) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};
    std::vector<std::vector<DatumArray>> input = {{DatumArray{"A", "B"}}, {DatumArray{"B", "C"}}};

    auto [local_ctx, state, func] = RunUpdate(value_lts, input, std::nullopt);
    const auto& state_impl = *reinterpret_cast<const CelonisEnumerateAggregateState*>(state->state());
    EXPECT_EQ(0, state_impl.length);
}

TEST_F(CelonisEnumerateTransitiveEdgesTest, basic) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "B"}}, {DatumArray{kNullDatum, "C"}}};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"C"}}, {DatumArray{"D"}}};
    int max_len = 10;

    std::vector<std::vector<DatumArray>> expected = {{DatumArray{"A", "B", "B", "B", "C", "C", "D"}},
                                                     {DatumArray{"A", "B", "C", "D", "C", "D", "D"}}};
    Run(value_lts, input1, input2, max_len, expected);
}

TEST_F(CelonisEnumerateTransitiveEdgesTest, multiple_visit) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "B", "C", "D"}}, {DatumArray{"B", "C", "D", "B"}}};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"A", "E", "A", "F", "G"}},
                                                   {DatumArray{"E", "D", "F", "G", "D"}}};
    int max_len = 10;

    // A-B-C-D, A-E-D-B-C, A-F-G-D-B-C
    // B-C-D
    // C-D
    // E-D-B-C
    // F-G-D-B-C
    // G-D-B-C
    std::vector<std::vector<DatumArray>> expected = {
            {DatumArray{"A", "A", "A", "A", "A", "A", "A", "B", "B", "B", "C", "C", "C", "D", "D",
                        "D", "E", "E", "E", "E", "F", "F", "F", "F", "F", "G", "G", "G", "G"}},
            {DatumArray{"A", "B", "C", "D", "E", "F", "G", "B", "C", "D", "C", "D", "B", "D", "B",
                        "C", "E", "D", "B", "C", "F", "G", "D", "B", "C", "G", "D", "B", "C"}}};
    Run(value_lts, input1, input2, max_len, expected);
}

TEST_F(CelonisEnumerateTransitiveEdgesTest, max_length) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "B", "C", "D"}}, {DatumArray{"B", "C", "D", "B"}}};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"A", "E", "A", "F", "G"}},
                                                   {DatumArray{"E", "D", "F", "G", "D"}}};
    int max_len = 3;

    // A-B-C, A-E-D, A-F-G
    // B-C-D
    // C-D
    // E-D-B
    // F-G-D
    // G-D-B
    std::vector<std::vector<DatumArray>> expected = {
            {DatumArray{"A", "A", "A", "A", "A", "A", "A", "B", "B", "B", "C", "C", "C",
                        "D", "D", "D", "E", "E", "E", "F", "F", "F", "G", "G", "G"}},
            {DatumArray{"A", "B", "C", "D", "E", "F", "G", "B", "C", "D", "C", "D", "B",
                        "D", "B", "C", "E", "D", "B", "F", "G", "D", "G", "D", "B"}}};
    Run(value_lts, input1, input2, max_len, expected);
}

TEST_F(CelonisEnumerateTransitiveEdgesTest, multiple_fields) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR, LogicalType::TYPE_BIGINT};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "B"}, DatumArray{1L, 2L}},
                                                   {DatumArray{kNullDatum, "C"}, DatumArray{kNullDatum, 3L}}};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"C"}, DatumArray{3L}},
                                                   {DatumArray{"D"}, DatumArray{4L}}};
    int max_len = 10;

    std::vector<std::vector<DatumArray>> expected = {
            {DatumArray{"A", "B", "B", "B", "C", "C", "D"}, DatumArray{1L, 2L, 2L, 2L, 3L, 3L, 4L}},
            {DatumArray{"A", "B", "C", "D", "C", "D", "D"}, DatumArray{1L, 2L, 3L, 4L, 3L, 4L, 4L}}};
    Run(value_lts, input1, input2, max_len, expected);
}

TEST_F(CelonisEnumerateTransitiveEdgesTest, multiple_fields_any_null_1) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR, LogicalType::TYPE_BIGINT};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "B"}, DatumArray{1L, 2L}},
                                                   {DatumArray{kNullDatum, "C"}, DatumArray{10L, 3L}}};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"C"}, DatumArray{3L}},
                                                   {DatumArray{"D"}, DatumArray{4L}}};
    int max_len = 10;

    std::vector<std::vector<DatumArray>> expected = {
            {DatumArray{"A", "B", "B", "B", "C", "C", "D"}, DatumArray{1L, 2L, 2L, 2L, 3L, 3L, 4L}},
            {DatumArray{"A", "B", "C", "D", "C", "D", "D"}, DatumArray{1L, 2L, 3L, 4L, 3L, 4L, 4L}}};
    Run(value_lts, input1, input2, max_len, expected);
}

TEST_F(CelonisEnumerateTransitiveEdgesTest, multiple_fields_any_null_2) {
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR, LogicalType::TYPE_BIGINT};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "B"}, DatumArray{1L, 2L}},
                                                   {DatumArray{"A", "C"}, DatumArray{kNullDatum, 3L}}};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"C"}, DatumArray{3L}},
                                                   {DatumArray{"D"}, DatumArray{4L}}};
    int max_len = 10;

    std::vector<std::vector<DatumArray>> expected = {
            {DatumArray{"A", "B", "B", "B", "C", "C", "D"}, DatumArray{1L, 2L, 2L, 2L, 3L, 3L, 4L}},
            {DatumArray{"A", "B", "C", "D", "C", "D", "D"}, DatumArray{1L, 2L, 3L, 4L, 3L, 4L, 4L}}};
    Run(value_lts, input1, input2, max_len, expected);
}

TEST_F(CelonisEnumerateTransitiveEdgesTest, multiple_chunks) {
    auto old_vector_chunk_size = config::vector_chunk_size;
    config::vector_chunk_size = 10;
    auto value_lts = std::vector<LogicalType>{LogicalType::TYPE_VARCHAR};

    std::vector<std::vector<DatumArray>> input1 = {{DatumArray{"A", "B", "C", "D"}}, {DatumArray{"B", "C", "D", "B"}}};
    std::vector<std::vector<DatumArray>> input2 = {{DatumArray{"A", "E", "A", "F", "G"}},
                                                   {DatumArray{"E", "D", "F", "G", "D"}}};
    int max_len = 10;

    // A-B-C-D, A-E-D-B-C, A-F-G-D-B-C
    // B-C-D
    // C-D
    // E-D-B-C
    // F-G-D-B-C
    // G-D-B-C
    std::vector<std::vector<DatumArray>> expected = {
            {DatumArray{"A", "A", "A", "A", "A", "A", "A", "B", "B", "B", "C", "C", "C", "D", "D",
                        "D", "E", "E", "E", "E", "F", "F", "F", "F", "F", "G", "G", "G", "G"}},
            {DatumArray{"A", "B", "C", "D", "E", "F", "G", "B", "C", "D", "C", "D", "B", "D", "B",
                        "C", "E", "D", "B", "C", "F", "G", "D", "B", "C", "G", "D", "B", "C"}}};

    auto [local_ctx1, state1, func] = RunUpdate(value_lts, input1, max_len);
    auto [local_ctx2, state2, func2] = RunUpdate(value_lts, input2, max_len);

    auto intermediate_type = get_intermediate_type(local_ctx1.get());

    // Serialize state2
    auto serde_col = ColumnHelper::create_column(intermediate_type, false);
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result

    // First chunk
    auto result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 10);
    Evaluate(result.get(), expected, true, false);

    // Second chunk
    result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 10);
    Evaluate(result.get(), expected, false, false);

    // Third chunk
    result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 9);
    Evaluate(result.get(), expected, false, true);

    // Last empty chunk
    result = local_ctx1->create_column(local_ctx1->get_return_type(), false);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());
    EXPECT_EQ(result->size(), 0);

    config::vector_chunk_size = old_vector_chunk_size;
}

} // namespace starrocks