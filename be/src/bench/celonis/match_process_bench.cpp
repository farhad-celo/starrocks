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
2025-03-01T17:50:50+00:00
Running ./be/build_Release/src/bench/celonis/output/match_process_bench
Run on (32 X 3094.29 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 2.98, 3.64, 3.99
// Args: Number of rows/ Length of each variant / Number of possible values / Number of nfa states / Percentage to the next states
----------------------------------------------------------------------------------------------------
Benchmark                                          Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------
BM_MatchProcessConfig/100000/50/50/2/5      77134010 ns     77131190 ns            9 RowInvRate=771.312ns
BM_MatchProcessConfig/100000/50/100/2/5     92456521 ns     92435402 ns            8 RowInvRate=924.354ns
BM_MatchProcessConfig/100000/50/150/2/5    118852533 ns    118844998 ns            6 RowInvRate=1.18845us
BM_MatchProcessConfig/100000/50/50/3/5      76765362 ns     76758401 ns            9 RowInvRate=767.584ns
BM_MatchProcessConfig/100000/50/100/3/5     87516828 ns     87515446 ns            8 RowInvRate=875.154ns
BM_MatchProcessConfig/100000/50/150/3/5    109407104 ns    109401873 ns            6 RowInvRate=1094.02ns
BM_MatchProcessConfig/100000/50/50/10/5     77015821 ns     77007803 ns            9 RowInvRate=770.078ns
BM_MatchProcessConfig/100000/50/100/10/5    95870653 ns     95869212 ns            8 RowInvRate=958.692ns
BM_MatchProcessConfig/100000/50/150/10/5   128774652 ns    128772604 ns            5 RowInvRate=1.28773us
BM_MatchProcessConfig/100000/50/50/20/5     76375799 ns     76373792 ns            9 RowInvRate=763.738ns
BM_MatchProcessConfig/100000/50/100/20/5    98216707 ns     98211290 ns            6 RowInvRate=982.113ns
BM_MatchProcessConfig/100000/50/150/20/5   128531719 ns    128526041 ns            5 RowInvRate=1.28526us
BM_MatchProcessConfig/100000/50/50/30/5     76367664 ns     76364662 ns            9 RowInvRate=763.647ns
BM_MatchProcessConfig/100000/50/100/30/5    94857881 ns     94856324 ns            7 RowInvRate=948.563ns
BM_MatchProcessConfig/100000/50/150/30/5   127127545 ns    127121220 ns            5 RowInvRate=1.27121us
BM_MatchProcessConfig/100000/50/50/40/5     76380551 ns     76379221 ns            9 RowInvRate=763.792ns
BM_MatchProcessConfig/100000/50/100/40/5    96715333 ns     96710344 ns            8 RowInvRate=967.103ns
BM_MatchProcessConfig/100000/50/150/40/5   125580128 ns    125571346 ns            5 RowInvRate=1.25571us
BM_MatchProcessConfig/100000/50/50/2/10    115072746 ns    115069095 ns            7 RowInvRate=1.15069us
BM_MatchProcessConfig/100000/50/100/2/10   103916453 ns    103910288 ns            6 RowInvRate=1039.1ns
BM_MatchProcessConfig/100000/50/150/2/10    75331983 ns     75323922 ns            9 RowInvRate=753.239ns
BM_MatchProcessConfig/100000/50/50/3/10    105091339 ns    105087880 ns            7 RowInvRate=1050.88ns
BM_MatchProcessConfig/100000/50/100/3/10    93296515 ns     93294132 ns            9 RowInvRate=932.941ns
BM_MatchProcessConfig/100000/50/150/3/10    75222341 ns     75218636 ns            9 RowInvRate=752.186ns
BM_MatchProcessConfig/100000/50/50/10/10   107477143 ns    107470891 ns            8 RowInvRate=1074.71ns
BM_MatchProcessConfig/100000/50/100/10/10  112209149 ns    112201109 ns            7 RowInvRate=1.12201us
BM_MatchProcessConfig/100000/50/150/10/10   75617464 ns     75611988 ns            9 RowInvRate=756.12ns
BM_MatchProcessConfig/100000/50/50/20/10   106648336 ns    106647592 ns            6 RowInvRate=1066.48ns
BM_MatchProcessConfig/100000/50/100/20/10  104019354 ns    104015834 ns            6 RowInvRate=1040.16ns
BM_MatchProcessConfig/100000/50/150/20/10   75719582 ns     75715601 ns            9 RowInvRate=757.156ns
BM_MatchProcessConfig/100000/50/50/30/10   104794745 ns    104791381 ns            7 RowInvRate=1047.91ns
BM_MatchProcessConfig/100000/50/100/30/10   94469775 ns     94468203 ns            7 RowInvRate=944.682ns
BM_MatchProcessConfig/100000/50/150/30/10   75569718 ns     75562968 ns            9 RowInvRate=755.63ns
BM_MatchProcessConfig/100000/50/50/40/10   104976138 ns    104974141 ns            6 RowInvRate=1049.74ns
BM_MatchProcessConfig/100000/50/100/40/10   98674113 ns     98665922 ns            6 RowInvRate=986.659ns
BM_MatchProcessConfig/100000/50/150/40/10  123157892 ns    123151699 ns            6 RowInvRate=1.23152us
BM_MatchProcessConfig/100000/50/50/2/20    101811200 ns    101803814 ns            7 RowInvRate=1018.04ns
BM_MatchProcessConfig/100000/50/100/2/20   100738807 ns    100735733 ns            8 RowInvRate=1007.36ns
BM_MatchProcessConfig/100000/50/150/2/20   118951241 ns    118945180 ns            6 RowInvRate=1.18945us
BM_MatchProcessConfig/100000/50/50/3/20    106756231 ns    106753137 ns            8 RowInvRate=1067.53ns
BM_MatchProcessConfig/100000/50/100/3/20   102690360 ns    102685461 ns            7 RowInvRate=1026.85ns
BM_MatchProcessConfig/100000/50/150/3/20   111689532 ns    111681082 ns            6 RowInvRate=1.11681us
BM_MatchProcessConfig/100000/50/50/10/20   108903643 ns    108897891 ns            6 RowInvRate=1088.98ns
BM_MatchProcessConfig/100000/50/100/10/20  107643226 ns    107640232 ns            6 RowInvRate=1076.4ns
BM_MatchProcessConfig/100000/50/150/10/20  125228453 ns    125223091 ns            6 RowInvRate=1.25223us
BM_MatchProcessConfig/100000/50/50/20/20   105816223 ns    105808660 ns            6 RowInvRate=1058.09ns
BM_MatchProcessConfig/100000/50/100/20/20  109132041 ns    109128304 ns            6 RowInvRate=1091.28ns
BM_MatchProcessConfig/100000/50/150/20/20  119183595 ns    119178833 ns            6 RowInvRate=1.19179us
BM_MatchProcessConfig/100000/50/50/30/20   107976358 ns    107972209 ns            6 RowInvRate=1079.72ns
BM_MatchProcessConfig/100000/50/100/30/20  109205789 ns    109202045 ns            6 RowInvRate=1092.02ns
BM_MatchProcessConfig/100000/50/150/30/20  120069706 ns    120062833 ns            6 RowInvRate=1.20063us
BM_MatchProcessConfig/100000/50/50/40/20   108539829 ns    108535619 ns            6 RowInvRate=1085.36ns
BM_MatchProcessConfig/100000/50/100/40/20  115102062 ns    115097968 ns            6 RowInvRate=1.15098us
BM_MatchProcessConfig/100000/50/150/40/20  120326860 ns    120323600 ns            6 RowInvRate=1.20324us
BM_MatchProcessConfig/100000/50/50/2/50     81812873 ns     81809801 ns            8 RowInvRate=818.098ns
BM_MatchProcessConfig/100000/50/100/2/50    78370416 ns     78368481 ns            9 RowInvRate=783.685ns
BM_MatchProcessConfig/100000/50/150/2/50    82884189 ns     82882090 ns            8 RowInvRate=828.821ns
BM_MatchProcessConfig/100000/50/50/3/50     78518912 ns     78514758 ns            9 RowInvRate=785.148ns
BM_MatchProcessConfig/100000/50/100/3/50    76074463 ns     76072208 ns            9 RowInvRate=760.722ns
BM_MatchProcessConfig/100000/50/150/3/50    80408904 ns     80406544 ns            9 RowInvRate=804.065ns
BM_MatchProcessConfig/100000/50/50/10/50   104027095 ns    104022605 ns            6 RowInvRate=1040.23ns
BM_MatchProcessConfig/100000/50/100/10/50  100718865 ns    100714524 ns            7 RowInvRate=1007.15ns
BM_MatchProcessConfig/100000/50/150/10/50  122059483 ns    122054755 ns            6 RowInvRate=1.22055us
BM_MatchProcessConfig/100000/50/50/20/50   131652397 ns    131643614 ns            5 RowInvRate=1.31644us
BM_MatchProcessConfig/100000/50/100/20/50  134710541 ns    134705588 ns            5 RowInvRate=1.34706us
BM_MatchProcessConfig/100000/50/150/20/50  175648925 ns    175641809 ns            4 RowInvRate=1.75642us
BM_MatchProcessConfig/100000/50/50/30/50   163320127 ns    163312516 ns            4 RowInvRate=1.63313us
BM_MatchProcessConfig/100000/50/100/30/50  168207578 ns    168195162 ns            4 RowInvRate=1.68195us
BM_MatchProcessConfig/100000/50/150/30/50  193480667 ns    193468452 ns            4 RowInvRate=1.93468us
BM_MatchProcessConfig/100000/50/50/40/50   157490560 ns    157482046 ns            4 RowInvRate=1.57482us
BM_MatchProcessConfig/100000/50/100/40/50  163786079 ns    163768635 ns            4 RowInvRate=1.63769us
BM_MatchProcessConfig/100000/50/150/40/50  194155677 ns    194149401 ns            4 RowInvRate=1.94149us
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
            transition->activity_names.insert(Slice(activity));
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
