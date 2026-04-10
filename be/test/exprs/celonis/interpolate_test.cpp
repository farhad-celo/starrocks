#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "column/datum.h"
#include "column/nullable_column.h"
#include "column/type_traits.h"
#include "exprs/agg/window.h"
#include "exprs/celonis/agg/linear_interpolate.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "runtime/types.h"
#include "testutil/function_utils.h"

namespace starrocks {

namespace {

class ManagedWindowState {
public:
    static std::unique_ptr<ManagedWindowState> create(FunctionContext* ctx, const AggregateFunction* func) {
        return std::make_unique<ManagedWindowState>(ctx, func);
    }

    AggDataPtr state() const { return _state; }

    ~ManagedWindowState() { _func->destroy(_ctx, _state); }

    ManagedWindowState(FunctionContext* ctx, const AggregateFunction* func) : _ctx(ctx), _func(func) {
        _state = _mem_pool.allocate_aligned(func->size(), func->alignof_size());
        _func->create(_ctx, _state);
    }

private:
    FunctionContext* _ctx;
    const AggregateFunction* _func;
    MemPool _mem_pool;
    AggDataPtr _state;
};

} // namespace

// ── InterpolateTestBase ───────────────────────────────────────────────────────
// Shared infrastructure for all Interpolate window function tests.
//
// The main entry point is RunAndEvaluate<InputLT, OutputLT, FuncType>, which:
//   1. Builds a nullable input column from a DatumArray.
//   2. Runs the window function row-by-row over the entire partition.
//   3. Asserts the result against an expected DatumArray.
class CelonisInterpolateTestBase : public ::testing::Test {
protected:
    CelonisInterpolateTestBase() = default;

    void SetUp() override {}

    void TearDown() override {}

    // Build a nullable input column of the given logical type from a DatumArray.
    template <LogicalType InputLT>
    static ColumnPtr BuildInput(const DatumArray& values) {
        auto col = ColumnHelper::create_column(TypeDescriptor::from_logical_type(InputLT), /*nullable=*/true);
        for (const auto& d : values) {
            col->append_datum(d);
        }
        return col;
    }

    // Run the window function across the entire input as a single partition
    // (ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW), row by row.
    template <typename FuncType, LogicalType OutputLT>
    static NullableColumn::Ptr RunPartition(const ColumnPtr& input) {
        using OutputColType = typename RunTimeTypeTraits<OutputLT>::ColumnType;

        FunctionUtils utils;
        FuncType func;
        FunctionContext* ctx = utils.get_fn_ctx();

        const int64_t n = static_cast<int64_t>(input->size());
        auto managed = ManagedWindowState::create(ctx, &func);
        AggDataPtr state = managed->state();
        func.reset(ctx, {}, state);

        auto result = NullableColumn::create(OutputColType::create(), NullColumn::create());
        result->resize(n);

        const Column* cols[1] = {input.get()};
        for (int64_t row = 0; row < n; ++row) {
            func.update_batch_single_state_with_frame(ctx, state, cols,
                                                      /*peer_group_start=*/0,
                                                      /*peer_group_end=*/n,
                                                      /*frame_start=*/0,
                                                      /*frame_end=*/row + 1);
            func.get_values(ctx, state, result.get(), row, row + 1);
        }

        return result;
    }

    // Validate every row of `result` against the `expected` DatumArray.
    // A null Datum in `expected` asserts that the result row is null.
    template <LogicalType OutputLT>
    static void Evaluate(const NullableColumn::Ptr& result, const DatumArray& expected) {
        using OutputCppType = RunTimeCppType<OutputLT>;

        ASSERT_EQ(result->size(), expected.size());
        for (size_t row = 0; row < expected.size(); ++row) {
            if (expected[row].is_null()) {
                EXPECT_TRUE(result->is_null(row)) << "row " << row << " should be null";
            } else {
                EXPECT_FALSE(result->is_null(row)) << "row " << row << " should be non-null";
                if constexpr (std::is_floating_point_v<OutputCppType>) {
                    EXPECT_NEAR(result->get(row).get_double(), expected[row].get<OutputCppType>(), 1e-9)
                            << "row " << row;
                } else {
                    EXPECT_EQ(result->get(row).get_int64(), expected[row].get<OutputCppType>()) << "row " << row;
                }
            }
        }
    }

    template <LogicalType InputLT, LogicalType OutputLT, typename FuncType>
    static void RunAndEvaluate(const DatumArray& input, const DatumArray& expected) {
        Evaluate<OutputLT>(RunPartition<FuncType, OutputLT>(BuildInput<InputLT>(input)), expected);
    }
};

// ── LinearInterpolateTest ─────────────────────────────────────────────────────
// For linear interpolation the output is always TYPE_DOUBLE, regardless of the input type.
class CelonisLinearInterpolateTest : public CelonisInterpolateTestBase {
protected:
    template <LogicalType LT>
    static void RunAndEvaluate(const DatumArray& input, const DatumArray& expected) {
        CelonisInterpolateTestBase::RunAndEvaluate<LT, TYPE_DOUBLE, CelonisLinearInterpolateWindowFunction<LT>>(
                input, expected);
    }
};

// ── Linear – TYPE_BIGINT ─────────────────────────────────────────────────────

TEST_F(CelonisLinearInterpolateTest, forward_fill_bigint) {
    // Gap between two anchors (slope=2) + trailing null (forward-filled).
    const auto input = DatumArray{2L, kNullDatum, kNullDatum, 8L, kNullDatum};
    const auto expected = DatumArray{2.0, 4.0, 6.0, 8.0, 8.0};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, leading_nulls_bigint) {
    // Leading nulls are back-filled with the first non-null; trailing null is forward-filled.
    const auto input = DatumArray{kNullDatum, kNullDatum, 5L, kNullDatum};
    const auto expected = DatumArray{5.0, 5.0, 5.0, 5.0};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, all_null_partition_bigint) {
    const auto input = DatumArray{kNullDatum, kNullDatum, kNullDatum};
    const auto expected = DatumArray{kNullDatum, kNullDatum, kNullDatum};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, no_nulls_bigint) {
    const auto input = DatumArray{1L, 2L, 3L};
    const auto expected = DatumArray{1.0, 2.0, 3.0};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, leading_nulls_and_forward_fill_bigint) {
    // Leading null back-filled with 4; gap 4→10 interpolated (slope=2); trailing null forward-filled.
    const auto input = DatumArray{kNullDatum, 4L, kNullDatum, kNullDatum, 10L, kNullDatum};
    const auto expected = DatumArray{4.0, 4.0, 6.0, 8.0, 10.0, 10.0};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, all_leading_nulls_single_value_at_end_bigint) {
    // All nulls are leading; back-filled with the single non-null at the end.
    const auto input = DatumArray{kNullDatum, kNullDatum, 42L};
    const auto expected = DatumArray{42.0, 42.0, 42.0};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, interpolation_with_trailing_nulls) {
    // One null gap (1→2, slope=0.5) followed by two trailing nulls (forward-filled with 2).
    const auto input = DatumArray{1L, kNullDatum, 2L, kNullDatum, kNullDatum};
    const auto expected = DatumArray{1.0, 1.5, 2.0, 2.0, 2.0};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, multiple_null_segments) {
    // Two independent null gaps: 1→2 (slope=0.5) and 2→4 (slope=1.0).
    const auto input = DatumArray{1L, kNullDatum, 2L, kNullDatum, 4L};
    const auto expected = DatumArray{1.0, 1.5, 2.0, 3.0, 4.0};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, trailing_nulls) {
    // Single anchor followed by trailing nulls, all forward-filled.
    const auto input = DatumArray{42L, kNullDatum, kNullDatum};
    const auto expected = DatumArray{42.0, 42.0, 42.0};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, single_element_partition) {
    const auto input = DatumArray{42L};
    const auto expected = DatumArray{42.0};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, single_null_value) {
    const auto input = DatumArray{kNullDatum};
    const auto expected = DatumArray{kNullDatum};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, negative_slope) {
    const auto input = DatumArray{10L, kNullDatum, 4L};
    const auto expected = DatumArray{10.0, 7.0, 4.0};
    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, huge_gap_nulls) {
    // Input array: [0, (100 nulls), 101]
    DatumArray input;
    input.emplace_back(0L); // Start point
    for (int i = 0; i < 100; ++i) {
        input.push_back(kNullDatum);
    }
    input.emplace_back(101L); // End point

    // Because the gap is 101 steps (101 - 0), the increment is exactly 1.0 per index.
    DatumArray expected;
    for (int i = 0; i <= 101; ++i) {
        expected.emplace_back(static_cast<double>(i));
    }

    RunAndEvaluate<TYPE_BIGINT>(input, expected);
}

// ── Linear – TYPE_DOUBLE ─────────────────────────────────────────────────────

TEST_F(CelonisLinearInterpolateTest, forward_fill_double) {
    const auto input = DatumArray{2.0, kNullDatum, kNullDatum, 8.0, kNullDatum};
    const auto expected = DatumArray{2.0, 4.0, 6.0, 8.0, 8.0};
    RunAndEvaluate<TYPE_DOUBLE>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, leading_nulls_double) {
    const auto input = DatumArray{kNullDatum, kNullDatum, 5.0, kNullDatum};
    const auto expected = DatumArray{5.0, 5.0, 5.0, 5.0};
    RunAndEvaluate<TYPE_DOUBLE>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, all_null_partition_double) {
    const auto input = DatumArray{kNullDatum, kNullDatum, kNullDatum};
    const auto expected = DatumArray{kNullDatum, kNullDatum, kNullDatum};
    RunAndEvaluate<TYPE_DOUBLE>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, no_nulls_double) {
    const auto input = DatumArray{1.0, 2.0, 3.0};
    const auto expected = DatumArray{1.0, 2.0, 3.0};
    RunAndEvaluate<TYPE_DOUBLE>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, leading_nulls_and_forward_fill_double) {
    const auto input = DatumArray{kNullDatum, 4.0, kNullDatum, kNullDatum, 10.0, kNullDatum};
    const auto expected = DatumArray{4.0, 4.0, 6.0, 8.0, 10.0, 10.0};
    RunAndEvaluate<TYPE_DOUBLE>(input, expected);
}

TEST_F(CelonisLinearInterpolateTest, all_leading_nulls_single_value_at_end_double) {
    const auto input = DatumArray{kNullDatum, kNullDatum, 42.0};
    const auto expected = DatumArray{42.0, 42.0, 42.0};
    RunAndEvaluate<TYPE_DOUBLE>(input, expected);
}

} // namespace starrocks
