#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/match_process.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-02-11T09:19:49+01:00
Running ./be/build_Release/src/bench/celonis/output/match_process_bench
Run on (20 X 5200 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 1.60, 2.00, 1.36
// Args: Number of rows/ Length of each variant / Number of possible values / Number of nfa states / Percentage to the next states
***WARNING*** CPU scaling is enabled, the benchmark real time measurements may be noisy and will incur extra overhead.
----------------------------------------------------------------------------------------------------
Benchmark                                          Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------
BM_MatchProcessConfig/100000/50/50/2/5     167148634 ns    167141914 ns            4 RowInvRate=1.67142us
BM_MatchProcessConfig/100000/50/100/2/5    163680802 ns    163675658 ns            4 RowInvRate=1.63676us
BM_MatchProcessConfig/100000/50/150/2/5    190933701 ns    189828409 ns            4 RowInvRate=1.89828us
BM_MatchProcessConfig/100000/50/50/3/5     185952099 ns    185932307 ns            4 RowInvRate=1.85932us
BM_MatchProcessConfig/100000/50/100/3/5    177738335 ns    177721675 ns            4 RowInvRate=1.77722us
BM_MatchProcessConfig/100000/50/150/3/5    187748883 ns    187685565 ns            4 RowInvRate=1.87686us
BM_MatchProcessConfig/100000/50/50/10/5    184965540 ns    184962540 ns            4 RowInvRate=1.84963us
BM_MatchProcessConfig/100000/50/100/10/5   182911565 ns    182901799 ns            4 RowInvRate=1.82902us
BM_MatchProcessConfig/100000/50/150/10/5   204646398 ns    204640507 ns            3 RowInvRate=2.04641us
BM_MatchProcessConfig/100000/50/50/20/5    174739542 ns    174712631 ns            4 RowInvRate=1.74713us
BM_MatchProcessConfig/100000/50/100/20/5   188506784 ns    188492218 ns            4 RowInvRate=1.88492us
BM_MatchProcessConfig/100000/50/150/20/5   189714767 ns    189712966 ns            3 RowInvRate=1.89713us
BM_MatchProcessConfig/100000/50/50/30/5    181154224 ns    181144803 ns            4 RowInvRate=1.81145us
BM_MatchProcessConfig/100000/50/100/30/5   187982768 ns    187981822 ns            4 RowInvRate=1.87982us
BM_MatchProcessConfig/100000/50/150/30/5   198110915 ns    198111883 ns            3 RowInvRate=1.98112us
BM_MatchProcessConfig/100000/50/50/40/5    178922536 ns    178919240 ns            4 RowInvRate=1.78919us
BM_MatchProcessConfig/100000/50/100/40/5   174953333 ns    174954021 ns            4 RowInvRate=1.74954us
BM_MatchProcessConfig/100000/50/150/40/5   205280911 ns    205272693 ns            3 RowInvRate=2.05273us
BM_MatchProcessConfig/100000/50/50/2/10    166129873 ns    166122650 ns            4 RowInvRate=1.66123us
BM_MatchProcessConfig/100000/50/100/2/10   166726088 ns    166705804 ns            4 RowInvRate=1.66706us
BM_MatchProcessConfig/100000/50/150/2/10   188299630 ns    188296294 ns            4 RowInvRate=1.88296us
BM_MatchProcessConfig/100000/50/50/3/10    172087060 ns    172070022 ns            4 RowInvRate=1.7207us
BM_MatchProcessConfig/100000/50/100/3/10   156221866 ns    156213843 ns            5 RowInvRate=1.56214us
BM_MatchProcessConfig/100000/50/150/3/10   176370505 ns    176354438 ns            4 RowInvRate=1.76354us
BM_MatchProcessConfig/100000/50/50/10/10   198664055 ns    198653516 ns            3 RowInvRate=1.98654us
BM_MatchProcessConfig/100000/50/100/10/10  200477702 ns    200470228 ns            3 RowInvRate=2.0047us
BM_MatchProcessConfig/100000/50/150/10/10  218228555 ns    218213796 ns            3 RowInvRate=2.18214us
BM_MatchProcessConfig/100000/50/50/20/10   202490421 ns    202483487 ns            3 RowInvRate=2.02483us
BM_MatchProcessConfig/100000/50/100/20/10  200066450 ns    200055628 ns            4 RowInvRate=2.00056us
BM_MatchProcessConfig/100000/50/150/20/10  220612667 ns    220602799 ns            3 RowInvRate=2.20603us
BM_MatchProcessConfig/100000/50/50/30/10   205114346 ns    205110903 ns            3 RowInvRate=2.05111us
BM_MatchProcessConfig/100000/50/100/30/10  203063328 ns    203053759 ns            4 RowInvRate=2.03054us
BM_MatchProcessConfig/100000/50/150/30/10  223717002 ns    223715799 ns            3 RowInvRate=2.23716us
BM_MatchProcessConfig/100000/50/50/40/10   202510165 ns    202511166 ns            3 RowInvRate=2.02511us
BM_MatchProcessConfig/100000/50/100/40/10  202834291 ns    202825956 ns            3 RowInvRate=2.02826us
BM_MatchProcessConfig/100000/50/150/40/10  222282713 ns    222266594 ns            3 RowInvRate=2.22267us
BM_MatchProcessConfig/100000/50/50/2/20    153836233 ns    153827339 ns            5 RowInvRate=1.53827us
BM_MatchProcessConfig/100000/50/100/2/20   149693779 ns    149681176 ns            5 RowInvRate=1.49681us
BM_MatchProcessConfig/100000/50/150/2/20   167678994 ns    167667524 ns            4 RowInvRate=1.67668us
BM_MatchProcessConfig/100000/50/50/3/20    149601291 ns    149596151 ns            5 RowInvRate=1.49596us
BM_MatchProcessConfig/100000/50/100/3/20   146432273 ns    146426351 ns            5 RowInvRate=1.46426us
BM_MatchProcessConfig/100000/50/150/3/20   164079567 ns    164078366 ns            4 RowInvRate=1.64078us
BM_MatchProcessConfig/100000/50/50/10/20   211536200 ns    211530222 ns            3 RowInvRate=2.1153us
BM_MatchProcessConfig/100000/50/100/10/20  215523072 ns    215504083 ns            3 RowInvRate=2.15504us
BM_MatchProcessConfig/100000/50/150/10/20  224008655 ns    223988190 ns            3 RowInvRate=2.23988us
BM_MatchProcessConfig/100000/50/50/20/20   233863979 ns    233852329 ns            3 RowInvRate=2.33852us
BM_MatchProcessConfig/100000/50/100/20/20  236568546 ns    236563531 ns            3 RowInvRate=2.36564us
BM_MatchProcessConfig/100000/50/150/20/20  256378694 ns    256374705 ns            3 RowInvRate=2.56375us
BM_MatchProcessConfig/100000/50/50/30/20   235262239 ns    235256746 ns            3 RowInvRate=2.35257us
BM_MatchProcessConfig/100000/50/100/30/20  241384516 ns    241375847 ns            3 RowInvRate=2.41376us
BM_MatchProcessConfig/100000/50/150/30/20  260498903 ns    260487311 ns            3 RowInvRate=2.60487us
BM_MatchProcessConfig/100000/50/50/40/20   232326469 ns    232326029 ns            3 RowInvRate=2.32326us
BM_MatchProcessConfig/100000/50/100/40/20  238362142 ns    238341707 ns            3 RowInvRate=2.38342us
BM_MatchProcessConfig/100000/50/150/40/20  255800166 ns    255789171 ns            3 RowInvRate=2.55789us
BM_MatchProcessConfig/100000/50/50/2/50    142270886 ns    142266330 ns            5 RowInvRate=1.42266us
BM_MatchProcessConfig/100000/50/100/2/50   138979325 ns    138968083 ns            5 RowInvRate=1.38968us
BM_MatchProcessConfig/100000/50/150/2/50   159249991 ns    159245611 ns            4 RowInvRate=1.59246us
BM_MatchProcessConfig/100000/50/50/3/50    151092346 ns    151088382 ns            5 RowInvRate=1.51088us
BM_MatchProcessConfig/100000/50/100/3/50   137798951 ns    137793308 ns            5 RowInvRate=1.37793us
BM_MatchProcessConfig/100000/50/150/3/50   162044383 ns    162033710 ns            5 RowInvRate=1.62034us
BM_MatchProcessConfig/100000/50/50/10/50   173953295 ns    173950887 ns            4 RowInvRate=1.73951us
BM_MatchProcessConfig/100000/50/100/10/50  166923510 ns    166921635 ns            4 RowInvRate=1.66922us
BM_MatchProcessConfig/100000/50/150/10/50  183571199 ns    183570309 ns            4 RowInvRate=1.8357us
BM_MatchProcessConfig/100000/50/50/20/50   235391291 ns    235374937 ns            3 RowInvRate=2.35375us
BM_MatchProcessConfig/100000/50/100/20/50  229774036 ns    229770381 ns            3 RowInvRate=2.2977us
BM_MatchProcessConfig/100000/50/150/20/50  257231119 ns    257224798 ns            3 RowInvRate=2.57225us
BM_MatchProcessConfig/100000/50/50/30/50   297854404 ns    297842070 ns            2 RowInvRate=2.97842us
BM_MatchProcessConfig/100000/50/100/30/50  289835233 ns    289668750 ns            2 RowInvRate=2.89669us
BM_MatchProcessConfig/100000/50/150/30/50  307464334 ns    307463334 ns            2 RowInvRate=3.07463us
BM_MatchProcessConfig/100000/50/50/40/50   304275206 ns    304275549 ns            2 RowInvRate=3.04276us
BM_MatchProcessConfig/100000/50/100/40/50  292972458 ns    292964319 ns            2 RowInvRate=2.92964us
BM_MatchProcessConfig/100000/50/150/40/50  309496006 ns    309480094 ns            2 RowInvRate=3.0948us
*/

namespace {

[[nodiscard]] std::unique_ptr<NFA> create_nfa(int num_of_states, int pass_percentage,
                                              const std::vector<std::string>& values) {
    DCHECK(num_of_states > 0);

    auto create_match_trans = [](int to_state, const std::vector<std::string>& activities) {
        std::unique_ptr<NFA::Transition> transition = std::make_unique<NFA::Transition>();
        transition->type = NFA::EXACT_MATCH;
        transition->to_states.push_back(to_state);
        for (const auto& activity : activities) {
            transition->activity_names.insert(activity);
        }
        return transition;
    };

    auto create_unmatch_trans = [](int to_state) {
        std::unique_ptr<NFA::Transition> transition = std::make_unique<NFA::Transition>();
        transition->type = NFA::UNMATCHED;
        transition->to_states.push_back(to_state);
        return transition;
    };

    std::unique_ptr<NFA> result = std::make_unique<NFA>();

    for (int i{0}; i < num_of_states - 1; ++i) {
        std::unique_ptr<NFA::State> state = std::make_unique<NFA::State>();

        auto to_self_match = create_match_trans(i, {values.at(i)});
        auto to_self_unmatched = create_unmatch_trans(i);
        auto to_next_match = create_match_trans(
                i + 1, std::vector(values.begin(), values.begin() + values.size() * pass_percentage / 100));

        state->transitions.push_back(std::move(to_self_match));
        state->transitions.push_back(std::move(to_self_unmatched));
        state->transitions.push_back(std::move(to_next_match));
        state->is_final = false;

        result->states.push_back(std::move(state));
    }

    std::unique_ptr<NFA::State> state = std::make_unique<NFA::State>();
    auto to_self_unmatched = create_unmatch_trans(num_of_states - 1);
    state->transitions.push_back(std::move(to_self_unmatched));
    state->is_final = true;

    result->states.push_back(std::move(state));

    return result;
}

} // namespace

static void do_bench(benchmark::State& state) {
    const int num_rows = state.range(0);
    const int variant_length = state.range(1);
    const int num_values = state.range(2);
    const int num_nfa_states = state.range(3);
    const int percentage_to_next_state = state.range(4);

    if (num_nfa_states > variant_length || percentage_to_next_state > 100) {
        throw std::runtime_error("Wrong parameter");
    }

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_values - 1);

    std::vector<std::string> values;
    values.reserve(num_values);
    for (int i = 0; i < num_values; i++) {
        values.push_back("value" + std::to_string(i));
    }

    auto gen_rand_element = [&]() { return Slice(values[uniform_value(rng)]); };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        for (int i = 0; i < num_elements; i++) {
            array.emplace_back(gen_rand_element());
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_ARRAY))};
    auto return_type = AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BIGINT));
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto variant_column = ColumnHelper::create_column(
                TypeDescriptor::create_array_type(TypeDescriptor::create_varchar_type(20)), false);
        for (int i = 0; i < num_rows; i++) {
            variant_column->append_datum(gen_rand_array(variant_length));
        }
        ctx->set_constant_columns({nullptr});

        auto nfa = create_nfa(num_nfa_states, percentage_to_next_state, values);

        state.ResumeTiming();
        ASSERT_TRUE(CelonisMatchProcess::match_process_prepare_benchmark_only(ctx.get(), std::move(nfa),
                                                                              FunctionContext::FRAGMENT_LOCAL)
                            .ok());
        ASSERT_TRUE(CelonisMatchProcess::celonis_match_process(ctx.get(), {variant_column}).ok());
        ASSERT_TRUE(CelonisMatchProcess::match_process_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_MatchProcessConfig(benchmark::State& state) {
    do_bench(state);
}

BENCHMARK(BM_MatchProcessConfig)
        ->ArgsProduct({{100000}, {50}, {50, 100, 150}, {2, 3, 10, 20, 30, 40}, {5, 10, 20, 50}});

} // namespace starrocks

BENCHMARK_MAIN();
