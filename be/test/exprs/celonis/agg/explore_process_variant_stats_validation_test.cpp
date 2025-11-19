#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "column/column_builder.h"
#include "column/vectorized_fwd.h"
#include "explore_process_test_utils.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/anyval_util.h"
#include "exprs/arithmetic_operation.h"
#include "exprs/celonis/agg/variant_stats.h"
#include "exprs/celonis/base64.h"
#include "exprs/function_context.h"
#include "modules/query/variantstats.pb.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"
#include "testutil/function_utils.h"

/**
 * @file
 * @brief Generates random data to validate that `celonis_explore_process` produces the same results as
 *        `celonis_variant_stats` when `min_variant_count_threshold_on_leaf` <= 1.
 */

namespace starrocks {

class CelonisExploreProcessVariantStatsValidationTest : public testing::Test {
public:
    CelonisExploreProcessVariantStatsValidationTest() = default;

protected:
    void SetUp() override {
        runtime_state_vs = new RuntimeState();
        runtime_state_explore_process = new RuntimeState();
        utils_vs = new FunctionUtils(runtime_state_vs);
        utils_explore_process = new FunctionUtils(runtime_state_explore_process);
        ctx_vs = utils_vs->get_fn_ctx();
        ctx_explore_process = utils_explore_process->get_fn_ctx();
    }

    void TearDown() override {
        delete utils_vs;
        delete utils_explore_process;
        // FunctionUtils does not delete runtime_state.
        if (runtime_state_vs != nullptr) {
            delete runtime_state_vs;
        }
        if (runtime_state_explore_process != nullptr) {
            delete runtime_state_explore_process;
        }
    }

private:
    FunctionUtils* utils_vs{};
    FunctionUtils* utils_explore_process{};
    FunctionContext* ctx_vs{};
    FunctionContext* ctx_explore_process{};
    RuntimeState* runtime_state_vs{};
    RuntimeState* runtime_state_explore_process{};
};

TEST_F(CelonisExploreProcessVariantStatsValidationTest, test_basic) {
    auto data = {
            std::make_pair(build_variant_column(
                                   {{"a1", "a2"}, {"a10, a1", "a2", "a3"}, {"a3", "a4"}, {"a5", "a6", "a7, a1, a2"}}),
                           build_weight_column({10, 1, 1, 10})),
            std::make_pair(build_variant_column({{"a3", "a4", "a11"}, {"a9", "a6", "a7", "a8"}}),
                           build_weight_column({1, 15})),
            std::make_pair(build_variant_column({{"a9", "a10", "a3"}}), build_weight_column({1})),
    };
    std::string rs_vs = compute_result_variant_stats(data, ctx_vs);
    std::string rs_explore_process = compute_result_explore_process(data, 1, false, ctx_explore_process);
    match_result(rs_vs, rs_explore_process);
}

TEST_F(CelonisExploreProcessVariantStatsValidationTest, test_random_small) {
    auto data = {std::make_pair(build_random_variant_column(2, 2, 2, 2025), build_random_weight_column(2, 2, 1114)),
                 std::make_pair(build_random_variant_column(2, 2, 2, 2025), build_random_weight_column(2, 2, 1114)),
                 std::make_pair(build_random_variant_column(2, 2, 2, 2025), build_random_weight_column(2, 2, 1114)),
                 std::make_pair(build_random_variant_column(2, 2, 2, 2025), build_random_weight_column(2, 2, 1114)),
                 std::make_pair(build_random_variant_column(2, 2, 2, 2025), build_random_weight_column(2, 2, 1114))};
    std::string rs_vs = compute_result_variant_stats(data, ctx_vs);
    std::string rs_explore_process = compute_result_explore_process(data, 1, false, ctx_explore_process);
    match_result(rs_vs, rs_explore_process);
}

TEST_F(CelonisExploreProcessVariantStatsValidationTest, test_random_large) {
    auto data = {
            std::make_pair(build_random_variant_column(100, 10000, 10, 2026),
                           build_random_weight_column(10000, 100, 1115)),
            std::make_pair(build_random_variant_column(100, 10000, 10, 2026),
                           build_random_weight_column(10000, 100, 1115)),
            std::make_pair(build_random_variant_column(100, 10000, 10, 2026),
                           build_random_weight_column(10000, 100, 1115)),
            std::make_pair(build_random_variant_column(100, 10000, 10, 2026),
                           build_random_weight_column(10000, 100, 1115)),
            std::make_pair(build_random_variant_column(100, 10000, 10, 2026),
                           build_random_weight_column(10000, 100, 1115)),
    };
    std::string rs_vs = compute_result_variant_stats(data, ctx_vs);
    std::string rs_explore_process = compute_result_explore_process(data, 1, false, ctx_explore_process);
    match_result(rs_vs, rs_explore_process);
}

} // namespace starrocks
