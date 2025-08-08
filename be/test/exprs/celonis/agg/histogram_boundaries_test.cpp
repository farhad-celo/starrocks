#include <algorithm>
#include <gtest/gtest.h>

#include "../util.h"
#include "column/struct_column.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate_factory.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/agg/histogram_boundaries.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"

namespace starrocks {

namespace {

class ManagedAggrState {
public:
    ~ManagedAggrState() { _func->destroy(_ctx, _state); }

    static std::unique_ptr<ManagedAggrState> create(FunctionContext *ctx, const AggregateFunction *func) {
        return std::make_unique<ManagedAggrState>(ctx, func);
    }

    AggDataPtr state() { return _state; }

private:
    ManagedAggrState(FunctionContext *ctx, const AggregateFunction *func) : _ctx(ctx), _func(func) {
        _state = _mem_pool.allocate_aligned(func->size(), func->alignof_size());
        _func->create(_ctx, _state);
    }

    FunctionContext *_ctx;
    const AggregateFunction *_func;
    MemPool _mem_pool;
    AggDataPtr _state;
};

} // namespace

class CelonisHistogramBoundariesTest : public testing::Test {
protected:
    CelonisHistogramBoundariesTest() = default;

    void SetUp() override {}
    void TearDown() override {}

    TypeDescriptor get_return_type(LogicalType logical_type) {
        TypeDescriptor type_return_struct;
        type_return_struct.type = LogicalType::TYPE_STRUCT;
        type_return_struct.children.emplace_back(celonis::array_type(logical_type));
        type_return_struct.field_names.emplace_back("class_bounds_lower");
        type_return_struct.children.emplace_back(celonis::array_type(logical_type));
        type_return_struct.field_names.emplace_back("class_bounds_upper");
        type_return_struct.children.emplace_back(celonis::array_type(TYPE_BIGINT));
        type_return_struct.field_names.emplace_back("class_count");

        return type_return_struct;
    }

    std::unique_ptr<FunctionContext> get_ctx(LogicalType logical_type) {
        std::vector<FunctionContext::TypeDesc> arg_types = {
                TypeDescriptor::from_logical_type(logical_type), // input
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN), // no_lower_bound
                TypeDescriptor::from_logical_type(TYPE_BOOLEAN), // no_upper_bound
                celonis::array_type(logical_type)                // boundaries
        };
        auto return_type = get_return_type(logical_type);
        mem_pools_.emplace_back(std::make_unique<MemPool>());
        return std::unique_ptr<FunctionContext>(
                FunctionContext::create_context(nullptr, mem_pools_.back().get(), return_type, std::move(arg_types)));
    }

    template<LogicalType LT>
    void EvaluateField(StructColumn* st, const std::string& field_name, const DatumArray& expected) {
        auto field = st->field_column(field_name);
        ASSERT_EQ(field->size(), 1);
        auto result_array = field->get(0).get_array();
        ASSERT_EQ(result_array.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i) {
            auto debug_string = [&]() {
                return fmt::format("field: {}, index: {}", field_name, i);
            };
            if (expected[i].is_null()) {
                EXPECT_TRUE(result_array[i].is_null()) << debug_string();
            } else if (field->is_null(i)) {
                EXPECT_FALSE(result_array[i].is_null()) << debug_string();
            } else {
                EXPECT_EQ(result_array[i].get<RunTimeCppType<LT>>(), expected[i].get<RunTimeCppType<LT>>())
                        << debug_string();
            }
        }
    }

    template<LogicalType LT>
    void Evaluate(Column* result, const DatumStruct& expected) {
        if (expected.empty()) {
            EXPECT_TRUE(result->is_null(0));
            return;
        }
        auto* st = down_cast<StructColumn*>(ColumnHelper::get_data_column(result));
        EvaluateField<LT>(st, "class_bounds_lower", expected[0].get_array());
        EvaluateField<LT>(st, "class_bounds_upper", expected[1].get_array());
        EvaluateField<TYPE_BIGINT>(st, "class_count", expected[2].get_array());
    }

    std::tuple<std::unique_ptr<FunctionContext>, std::unique_ptr<ManagedAggrState>, const AggregateFunction*>
    RunUpdate(LogicalType logical_type, const DatumArray& input, const DatumArray& boundaries, bool no_lower_bound,
              bool no_upper_bound) {
        auto local_ctx = get_ctx(logical_type);

        const AggregateFunction *func =
                get_aggregate_function("celonis_histogram_boundaries", logical_type, TYPE_STRUCT, false);

        auto input_col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(logical_type), true);
        for (const auto& datum : input) {
            input_col->append_datum(datum);
        }

        auto no_lower_bound_col = ColumnHelper::create_const_column<TYPE_BOOLEAN>(no_lower_bound, input.size());
        auto no_upper_bound_col = ColumnHelper::create_const_column<TYPE_BOOLEAN>(no_upper_bound, input.size());
        auto boundaries_col = ColumnHelper::create_column(celonis::array_type(logical_type), true);
        boundaries_col->append_datum(boundaries);

        std::vector<const Column *> raw_columns;
        raw_columns.resize(4);
        raw_columns[0] = input_col.get();
        raw_columns[1] = no_lower_bound_col.get();
        raw_columns[2] = no_upper_bound_col.get();
        raw_columns[3] = boundaries_col.get();
        local_ctx->set_constant_columns({nullptr, no_lower_bound_col, no_upper_bound_col, boundaries_col});

        auto state = ManagedAggrState::create(local_ctx.get(), func);
        func->update_batch_single_state(local_ctx.get(), input.size(), raw_columns.data(), state->state());

        return {std::move(local_ctx), std::move(state), func};
    }

    template<LogicalType LT>
    void Run(const DatumArray& input, const DatumArray& boundaries, bool no_lower_bound, bool no_upper_bound,
             const DatumStruct& expected) {
        auto [local_ctx, state, func] = RunUpdate(LT, input, boundaries, no_lower_bound, no_upper_bound);

        auto result = ColumnHelper::create_column(get_return_type(LT), true);
        func->finalize_to_column(local_ctx.get(), state->state(), result.get());

        Evaluate<LT>(result.get(), expected);
    }

    std::vector<std::unique_ptr<MemPool>> mem_pools_;
};

TEST_F(CelonisHistogramBoundariesTest, histogram11_bigint_no_upper_bound_merge) {
    auto logical_type = TYPE_BIGINT;
    auto input1 = DatumArray{0L, 100L, 100L};
    auto input2 = DatumArray{100L, 100L, 500L};
    auto boundaries = DatumArray{100L, 150L, 300L};
    auto no_lower_bound = false;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{100L, 150L, 300L},
                                DatumArray{150L, 300L, kNullDatum},
                                DatumArray{4L, 0L, 1L}};

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, boundaries, no_lower_bound, no_upper_bound);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, boundaries, no_lower_bound, no_upper_bound);

    // Serialize state2
    ColumnPtr serde_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(logical_type), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    Evaluate<TYPE_BIGINT>(result.get(), expected);
}

TEST_F(CelonisHistogramBoundariesTest, merge_to_new_state) {
    auto logical_type = TYPE_BIGINT;
    auto input1 = DatumArray{0L, 100L, 100L};
    auto input2 = DatumArray{100L, 100L, 500L};
    auto boundaries = DatumArray{100L, 150L, 300L};
    auto no_lower_bound = false;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{100L, 150L, 300L},
                                DatumArray{150L, 300L, kNullDatum},
                                DatumArray{4L, 0L, 1L}};

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, boundaries, no_lower_bound, no_upper_bound);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, boundaries, no_lower_bound, no_upper_bound);


    // Serialize state1 and state2
    ColumnPtr serde_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    auto local_ctx3 = get_ctx(logical_type);
    auto no_lower_bound3 = ColumnHelper::create_const_column<TYPE_BOOLEAN>(no_lower_bound, 2);
    auto no_upper_bound3 = ColumnHelper::create_const_column<TYPE_BOOLEAN>(no_upper_bound, 2);
    // As of 2023-12-20, SR drops boundaries column from const columns in merge.
    local_ctx3->set_constant_columns({nullptr, no_lower_bound3, no_upper_bound3});
    auto state3 = ManagedAggrState::create(local_ctx3.get(), func);

    // Merge state1 and state3 into new state3.
    func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
    func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(logical_type), true);
    func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());

    Evaluate<TYPE_BIGINT>(result.get(), expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram12_bigint) {
    auto logical_type = TYPE_BIGINT;
    auto input1 = DatumArray{0L, 100L, 100L};
    auto input2 = DatumArray{100L, 100L, 200L};
    auto boundaries = DatumArray{100L, 150L, 300L};
    auto no_lower_bound = false;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{100L, 150L},
                                DatumArray{150L, 300L},
                                DatumArray{4L, 1L}};

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, boundaries, no_lower_bound, no_upper_bound);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, boundaries, no_lower_bound, no_upper_bound);

    // Serialize state2
    ColumnPtr serde_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(logical_type), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    Evaluate<TYPE_BIGINT>(result.get(), expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram23_bigint_no_merge) {
    auto input = DatumArray{1L, 2L, 3L, 4L, 5L};
    auto boundaries = DatumArray{2L, 5L, 4L, 3L, 1L};
    auto no_lower_bound = false;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{1L, 2L, 3L, 4L},
                                DatumArray{2L, 3L, 4L, 5L},
                                DatumArray{1L, 1L, 1L, 1L}};

    Run<TYPE_BIGINT>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram24_no_lower_bound) {
    auto input = DatumArray{100L, 200L, 300L, 500L};
    auto boundaries = DatumArray{500L};
    auto no_lower_bound = true;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{kNullDatum},
                                DatumArray{500L},
                                DatumArray{3L}};

    Run<TYPE_BIGINT>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram25_no_upper_bound) {
    auto input = DatumArray{100L, 200L, 300L, 500L};
    auto boundaries = DatumArray{200L};
    auto no_lower_bound = false;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{200L},
                                DatumArray{kNullDatum},
                                DatumArray{3L}};

    Run<TYPE_BIGINT>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram26_empty_boundaries) {
    auto input = DatumArray{0L, 200L, 300L, 500L};
    auto boundaries = DatumArray{};
    auto no_lower_bound = true;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{kNullDatum},
                                DatumArray{kNullDatum},
                                DatumArray{4L}};

    Run<TYPE_BIGINT>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram26_empty_boundaries_with_merge_to_new_state) {
    auto logical_type = TYPE_BIGINT;
    auto input1 = DatumArray{0L, 200L};
    auto input2 = DatumArray{300L, 500L};
    auto boundaries = DatumArray{};
    auto no_lower_bound = true;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{kNullDatum},
                                DatumArray{kNullDatum},
                                DatumArray{4L}};

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, boundaries, no_lower_bound, no_upper_bound);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, boundaries, no_lower_bound, no_upper_bound);

    // Serialize state1 and state2
    ColumnPtr serde_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    auto local_ctx3 = get_ctx(logical_type);
    auto no_lower_bound3 = ColumnHelper::create_const_column<TYPE_BOOLEAN>(no_lower_bound, 2);
    auto no_upper_bound3 = ColumnHelper::create_const_column<TYPE_BOOLEAN>(no_upper_bound, 2);
    // As of 2023-12-20, SR drops boundaries column from const columns in merge.
    local_ctx3->set_constant_columns({nullptr, no_lower_bound3, no_upper_bound3});
    auto state3 = ManagedAggrState::create(local_ctx3.get(), func);

    // Merge state1 and state3 into new state3.
    func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
    func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(logical_type), true);
    func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());

    Evaluate<TYPE_BIGINT>(result.get(), expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram29_double_merge) {
    auto logical_type = TYPE_DOUBLE;
    auto input1 = DatumArray{0., 200.};
    auto input2 = DatumArray{300., 500.};
    auto boundaries = DatumArray{100., 500.};
    auto no_lower_bound = false;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{100.},
                                DatumArray{500.},
                                DatumArray{2L}};

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, boundaries, no_lower_bound, no_upper_bound);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, boundaries, no_lower_bound, no_upper_bound);

    // Serialize state2
    ColumnPtr serde_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(logical_type), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    Evaluate<TYPE_DOUBLE>(result.get(), expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram30_double_no_lower_bound) {
    auto input = DatumArray{0., 200., 300., 500.};
    auto boundaries = DatumArray{500.};
    auto no_lower_bound = true;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{kNullDatum},
                                DatumArray{500.},
                                DatumArray{3L}};

    Run<TYPE_DOUBLE>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram31_double_no_upper_bound) {
    auto input = DatumArray{0., 200., 300., 500.};
    auto boundaries = DatumArray{200.};
    auto no_lower_bound = false;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{200.},
                                DatumArray{kNullDatum},
                                DatumArray{3L}};

    Run<TYPE_DOUBLE>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram32_double_empty_boundaries) {
    auto input = DatumArray{0., 200., 300., 500.};
    auto boundaries = DatumArray{};
    auto no_lower_bound = true;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{kNullDatum},
                                DatumArray{kNullDatum},
                                DatumArray{4L}};

    Run<TYPE_DOUBLE>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram33_no_bucket) {
    auto input = DatumArray{100L, 500L};
    auto boundaries = DatumArray{900L};
    auto no_lower_bound = false;
    auto no_upper_bound = false;
    auto expected = DatumStruct{}; // Expected is NULL but DatumStruct{} is used as convenience.

    Run<TYPE_BIGINT>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram44_varchar_no_lower_bound_no_upper_bound) {
    auto logical_type = TYPE_VARCHAR;
    auto input1 = DatumArray{"A", "B", "C", "D", "E"};
    auto input2 = DatumArray{"F", "G", "H", "I", "J"};
    auto boundaries = DatumArray{"D", "G", "J"};
    auto no_lower_bound = true;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{kNullDatum, "D", "G", "J"},
                                DatumArray{"D", "G", "J", kNullDatum},
                                DatumArray{3L, 3L, 3L, 1L}};

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, boundaries, no_lower_bound, no_upper_bound);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, boundaries, no_lower_bound, no_upper_bound);

    // Serialize state2
    ColumnPtr serde_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(logical_type), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    Evaluate<TYPE_VARCHAR>(result.get(), expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram44_varchar_merge_with_new_state) {
    auto logical_type = TYPE_VARCHAR;
    auto input1 = DatumArray{"A", "B", "C", "D", "E"};
    auto input2 = DatumArray{"F", "G", "H", "I", "J"};
    auto boundaries = DatumArray{"D", "G", "J"};
    auto no_lower_bound = true;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{kNullDatum, "D", "G", "J"},
                                DatumArray{"D", "G", "J", kNullDatum},
                                DatumArray{3L, 3L, 3L, 1L}};

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, boundaries, no_lower_bound, no_upper_bound);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, boundaries, no_lower_bound, no_upper_bound);

    // Serialize state1 and state2
    ColumnPtr serde_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx1.get(), state1->state(), serde_col.get());
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    auto local_ctx3 = get_ctx(logical_type);
    auto no_lower_bound3 = ColumnHelper::create_const_column<TYPE_BOOLEAN>(no_lower_bound, 2);
    auto no_upper_bound3 = ColumnHelper::create_const_column<TYPE_BOOLEAN>(no_upper_bound, 2);
    // As of 2023-12-20, SR drops boundaries column from const columns in merge.
    local_ctx3->set_constant_columns({nullptr, no_lower_bound3, no_upper_bound3});
    auto state3 = ManagedAggrState::create(local_ctx3.get(), func);

    // Merge state1 and state2 into new state3
    func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 0);
    func->merge(local_ctx3.get(), serde_col.get(), state3->state(), 1);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(logical_type), true);
    func->finalize_to_column(local_ctx3.get(), state3->state(), result.get());

    Evaluate<TYPE_VARCHAR>(result.get(), expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram45_varchar_no_upper_bound) {
    auto input = DatumArray{"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
    auto boundaries = DatumArray{"F"};
    auto no_lower_bound = false;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{"F"},
                                DatumArray{kNullDatum},
                                DatumArray{5L}};

    Run<TYPE_VARCHAR>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram46_varchar) {
    auto input = DatumArray{"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
    auto boundaries = DatumArray{"B", "E", "H"};
    auto no_lower_bound = false;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{"B", "E"},
                                DatumArray{"E", "H"},
                                DatumArray{3L, 3L}};

    Run<TYPE_VARCHAR>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram47_varchar) {
    auto input = DatumArray{"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
    auto boundaries = DatumArray{"A", "E", "H", "K"};
    auto no_lower_bound = false;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{"A", "E", "H"},
                                DatumArray{"E", "H", "K"},
                                DatumArray{4L, 3L, 3L}};

    Run<TYPE_VARCHAR>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram50_bigint_no_lower_bound_no_upper_bound) {
    auto input = DatumArray{0L, 200L, 300L, 500L};
    auto boundaries = DatumArray{200L};
    auto no_lower_bound = true;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{kNullDatum, 200L},
                                DatumArray{200L, kNullDatum},
                                DatumArray{1L, 3L}};

    Run<TYPE_BIGINT>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram51_double_no_lower_bound_no_upper_bound) {
    auto input = DatumArray{0., 200., 300., 500.};
    auto boundaries = DatumArray{200.};
    auto no_lower_bound = true;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{kNullDatum, 200.},
                                DatumArray{200., kNullDatum},
                                DatumArray{1L, 3L}};

    Run<TYPE_DOUBLE>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram_one_data_boundary_no_lower_bound) {
    auto logical_type = TYPE_DATETIME;
    auto input1 = DatumArray{TimestampValue::create(2019, 1, 1, 13, 0, 0),
                             TimestampValue::create(2019, 1, 2, 13, 0, 0),
                             TimestampValue::create(2019, 1, 3, 13, 0, 0)};
    auto input2 = DatumArray{TimestampValue::create(2019, 1, 4, 13, 0, 0),
                             TimestampValue::create(2019, 1, 5, 13, 0, 0),
                             TimestampValue::create(2019, 1, 6, 13, 0, 0)};
    auto boundaries = DatumArray{TimestampValue::create(2019, 1, 3, 13, 0, 0)};
    auto no_lower_bound = true;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{kNullDatum},
                                DatumArray{TimestampValue::create(2019, 1, 3, 13, 0, 0)},
                                DatumArray{2L}};

    auto [local_ctx1, state1, func] = RunUpdate(logical_type, input1, boundaries, no_lower_bound, no_upper_bound);
    auto [local_ctx2, state2, func2] = RunUpdate(logical_type, input2, boundaries, no_lower_bound, no_upper_bound);

    // Serialize state2
    ColumnPtr serde_col = BinaryColumn::create();
    func->serialize_to_column(local_ctx2.get(), state2->state(), serde_col.get());

    // Merge state2 into state1
    func->merge(local_ctx1.get(), serde_col.get(), state1->state(), 0);

    // Get the result
    auto result = ColumnHelper::create_column(get_return_type(logical_type), true);
    func->finalize_to_column(local_ctx1.get(), state1->state(), result.get());

    Evaluate<TYPE_DATETIME>(result.get(), expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram_one_data_boundary_no_upper_bound) {
    auto input = DatumArray{TimestampValue::create(2019, 1, 1, 13, 0, 0),
                       TimestampValue::create(2019, 1, 2, 13, 0, 0),
                       TimestampValue::create(2019, 1, 3, 13, 0, 0),
                       TimestampValue::create(2019, 1, 4, 13, 0, 0),
                       TimestampValue::create(2019, 1, 5, 13, 0, 0),
                       TimestampValue::create(2019, 1, 6, 13, 0, 0)};
    auto boundaries = DatumArray{TimestampValue::create(2019, 1, 3, 13, 0, 0)};
    auto no_lower_bound = false;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{TimestampValue::create(2019, 1, 3, 13, 0, 0)},
                                DatumArray{kNullDatum},
                                DatumArray{4L}};

    Run<TYPE_DATETIME>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram_one_data_boundary_no_lower_bound_no_upper_bound) {
    auto input = DatumArray{TimestampValue::create(2019, 1, 1, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 2, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 3, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 4, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 5, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 6, 13, 0, 0)};
    auto boundaries = DatumArray{TimestampValue::create(2019, 1, 3, 13, 0, 0)};
    auto no_lower_bound = true;
    auto no_upper_bound = true;
    auto expected = DatumStruct{DatumArray{kNullDatum, TimestampValue::create(2019, 1, 3, 13, 0, 0)},
                                DatumArray{TimestampValue::create(2019, 1, 3, 13, 0, 0), kNullDatum},
                                DatumArray{2L, 4L}};

    Run<TYPE_DATETIME>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, histogram_more_than_one_data_boundary) {
    auto input = DatumArray{TimestampValue::create(2019, 1, 1, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 2, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 3, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 4, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 5, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 6, 13, 0, 0)};
    auto boundaries = DatumArray{TimestampValue::create(2019, 1, 3, 13, 0, 0),
                                 TimestampValue::create(2019, 1, 5, 13, 0, 0)};
    auto no_lower_bound = false;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{TimestampValue::create(2019, 1, 3, 13, 0, 0)},
                                DatumArray{TimestampValue::create(2019, 1, 5, 13, 0, 0)},
                                DatumArray{2L}};

    Run<TYPE_DATETIME>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, null_value) {
    auto input = DatumArray{0L, 100L, kNullDatum, 100L, 100L, 100L, 200L, kNullDatum};
    auto boundaries = DatumArray{100L, 150L, 300L};
    auto no_lower_bound = false;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{100L, 150L},
                                DatumArray{150L, 300L},
                                DatumArray{4L, 1L}};

    Run<TYPE_BIGINT>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

TEST_F(CelonisHistogramBoundariesTest, unordered_duplicated_boundaries) {
    auto input = DatumArray{0L, 100L, 100L, 100L, 100L, 200L};
    auto boundaries = DatumArray{300L, 150L, 300L, 100L};
    auto no_lower_bound = false;
    auto no_upper_bound = false;
    auto expected = DatumStruct{DatumArray{100L, 150L},
                                DatumArray{150L, 300L},
                                DatumArray{4L, 1L}};

    Run<TYPE_BIGINT>(input, boundaries, no_lower_bound, no_upper_bound, expected);
}

} // namespace starrocks
