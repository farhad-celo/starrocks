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
2025-06-15T10:53:25+00:00
Running ./be/build_Release/src/bench/celonis/output/match_process_bench
Run on (32 X 2759.96 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 15.00, 10.82, 8.76
// Args: Number of rows/ Length of each variant / Number of possible values / Number of nfa states / Percentage to the next states
----------------------------------------------------------------------------------------------------
Benchmark                                          Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------------
BM_MatchProcessConfig/100000/50/50/2/5      77075659 ns     77074527 ns            9 RowInvRate=770.745ns
BM_MatchProcessConfig/100000/50/100/2/5     72015797 ns     72011713 ns           10 RowInvRate=720.117ns
BM_MatchProcessConfig/100000/50/150/2/5     80514658 ns     80511110 ns            9 RowInvRate=805.111ns
BM_MatchProcessConfig/100000/50/50/3/5      75839899 ns     75832892 ns            9 RowInvRate=758.329ns
BM_MatchProcessConfig/100000/50/100/3/5     72176938 ns     72174495 ns           10 RowInvRate=721.745ns
BM_MatchProcessConfig/100000/50/150/3/5     82544065 ns     82537148 ns            8 RowInvRate=825.371ns
BM_MatchProcessConfig/100000/50/50/10/5     77919677 ns     77913165 ns            9 RowInvRate=779.132ns
BM_MatchProcessConfig/100000/50/100/10/5    72575258 ns     72572244 ns           10 RowInvRate=725.722ns
BM_MatchProcessConfig/100000/50/150/10/5    89855619 ns     89850894 ns            8 RowInvRate=898.509ns
BM_MatchProcessConfig/100000/50/50/20/5     75660461 ns     75654225 ns            9 RowInvRate=756.542ns
BM_MatchProcessConfig/100000/50/100/20/5    72711424 ns     72709049 ns           10 RowInvRate=727.09ns
BM_MatchProcessConfig/100000/50/150/20/5    89072147 ns     89069409 ns            8 RowInvRate=890.694ns
BM_MatchProcessConfig/100000/50/50/30/5     76932667 ns     76928757 ns            9 RowInvRate=769.288ns
BM_MatchProcessConfig/100000/50/100/30/5    72484484 ns     72481206 ns           10 RowInvRate=724.812ns
BM_MatchProcessConfig/100000/50/150/30/5    89047698 ns     89043605 ns            8 RowInvRate=890.436ns
BM_MatchProcessConfig/100000/50/50/40/5     76022788 ns     76014587 ns            9 RowInvRate=760.146ns
BM_MatchProcessConfig/100000/50/100/40/5    72531330 ns     72527450 ns           10 RowInvRate=725.274ns
BM_MatchProcessConfig/100000/50/150/40/5    90259393 ns     90216652 ns            8 RowInvRate=902.167ns
BM_MatchProcessConfig/100000/50/50/2/10     75517446 ns     75513903 ns            9 RowInvRate=755.139ns
BM_MatchProcessConfig/100000/50/100/2/10    71335588 ns     71328640 ns           10 RowInvRate=713.286ns
BM_MatchProcessConfig/100000/50/150/2/10    75008982 ns     75006075 ns            9 RowInvRate=750.061ns
BM_MatchProcessConfig/100000/50/50/3/10     75654995 ns     75650556 ns            9 RowInvRate=756.506ns
BM_MatchProcessConfig/100000/50/100/3/10    72494791 ns     72491875 ns           10 RowInvRate=724.919ns
BM_MatchProcessConfig/100000/50/150/3/10    75159953 ns     75156244 ns            9 RowInvRate=751.562ns
BM_MatchProcessConfig/100000/50/50/10/10    75474128 ns     75472072 ns            9 RowInvRate=754.721ns
BM_MatchProcessConfig/100000/50/100/10/10   72551536 ns     72543734 ns           10 RowInvRate=725.437ns
BM_MatchProcessConfig/100000/50/150/10/10   75939616 ns     75937177 ns            9 RowInvRate=759.372ns
BM_MatchProcessConfig/100000/50/50/20/10    75747838 ns     75738876 ns            9 RowInvRate=757.389ns
BM_MatchProcessConfig/100000/50/100/20/10   74028177 ns     74026649 ns            9 RowInvRate=740.266ns
BM_MatchProcessConfig/100000/50/150/20/10   76235736 ns     76234290 ns            9 RowInvRate=762.343ns
BM_MatchProcessConfig/100000/50/50/30/10    76114682 ns     76113450 ns            9 RowInvRate=761.135ns
BM_MatchProcessConfig/100000/50/100/30/10   72761973 ns     72759380 ns           10 RowInvRate=727.594ns
BM_MatchProcessConfig/100000/50/150/30/10   77798242 ns     77793008 ns            9 RowInvRate=777.93ns
BM_MatchProcessConfig/100000/50/50/40/10    79389456 ns     79379664 ns            9 RowInvRate=793.797ns
BM_MatchProcessConfig/100000/50/100/40/10   72746595 ns     72741658 ns           10 RowInvRate=727.417ns
BM_MatchProcessConfig/100000/50/150/40/10  103483614 ns    103475919 ns            8 RowInvRate=1034.76ns
BM_MatchProcessConfig/100000/50/50/2/20     80018239 ns     80017110 ns            9 RowInvRate=800.171ns
BM_MatchProcessConfig/100000/50/100/2/20    76060636 ns     76058616 ns           10 RowInvRate=760.586ns
BM_MatchProcessConfig/100000/50/150/2/20    81231475 ns     81231010 ns            8 RowInvRate=812.31ns
BM_MatchProcessConfig/100000/50/50/3/20     76446936 ns     76431103 ns            9 RowInvRate=764.311ns
BM_MatchProcessConfig/100000/50/100/3/20    74499959 ns     74496470 ns            9 RowInvRate=744.965ns
BM_MatchProcessConfig/100000/50/150/3/20    82335940 ns     82332788 ns            8 RowInvRate=823.328ns
BM_MatchProcessConfig/100000/50/50/10/20    76686486 ns     76683472 ns            9 RowInvRate=766.835ns
BM_MatchProcessConfig/100000/50/100/10/20   73054094 ns     73050555 ns           10 RowInvRate=730.506ns
BM_MatchProcessConfig/100000/50/150/10/20   86055816 ns     86054326 ns            8 RowInvRate=860.543ns
BM_MatchProcessConfig/100000/50/50/20/20    77612710 ns     77611468 ns            9 RowInvRate=776.115ns
BM_MatchProcessConfig/100000/50/100/20/20   73555885 ns     73554863 ns           10 RowInvRate=735.549ns
BM_MatchProcessConfig/100000/50/150/20/20   87195174 ns     87192591 ns            8 RowInvRate=871.926ns
BM_MatchProcessConfig/100000/50/50/30/20    76890191 ns     76888010 ns            9 RowInvRate=768.88ns
BM_MatchProcessConfig/100000/50/100/30/20   74687313 ns     74686035 ns            9 RowInvRate=746.86ns
BM_MatchProcessConfig/100000/50/150/30/20   87879033 ns     87874863 ns            8 RowInvRate=878.749ns
BM_MatchProcessConfig/100000/50/50/40/20    76751960 ns     76750729 ns            9 RowInvRate=767.507ns
BM_MatchProcessConfig/100000/50/100/40/20   74080925 ns     74078374 ns           10 RowInvRate=740.784ns
BM_MatchProcessConfig/100000/50/150/40/20   86556495 ns     86552613 ns            8 RowInvRate=865.526ns
BM_MatchProcessConfig/100000/50/50/2/50     76138668 ns     76134388 ns            9 RowInvRate=761.344ns
BM_MatchProcessConfig/100000/50/100/2/50    72514213 ns     72510947 ns           10 RowInvRate=725.109ns
BM_MatchProcessConfig/100000/50/150/2/50    77653781 ns     77652650 ns            9 RowInvRate=776.527ns
BM_MatchProcessConfig/100000/50/50/3/50     76120335 ns     76121227 ns            9 RowInvRate=761.212ns
BM_MatchProcessConfig/100000/50/100/3/50    73279304 ns     73279033 ns           10 RowInvRate=732.79ns
BM_MatchProcessConfig/100000/50/150/3/50    78827452 ns     78825487 ns            9 RowInvRate=788.255ns
BM_MatchProcessConfig/100000/50/50/10/50    80388425 ns     80385787 ns            9 RowInvRate=803.858ns
BM_MatchProcessConfig/100000/50/100/10/50   79517705 ns     79514260 ns            9 RowInvRate=795.143ns
BM_MatchProcessConfig/100000/50/150/10/50  102949767 ns    102949621 ns            7 RowInvRate=1029.5ns
BM_MatchProcessConfig/100000/50/50/20/50    96060935 ns     96058684 ns            7 RowInvRate=960.587ns
BM_MatchProcessConfig/100000/50/100/20/50   99672677 ns     99668514 ns            7 RowInvRate=996.685ns
BM_MatchProcessConfig/100000/50/150/20/50  141289633 ns    141285621 ns            5 RowInvRate=1.41286us
BM_MatchProcessConfig/100000/50/50/30/50   134073475 ns    134071185 ns            5 RowInvRate=1.34071us
BM_MatchProcessConfig/100000/50/100/30/50  135881336 ns    135881029 ns            5 RowInvRate=1.35881us
BM_MatchProcessConfig/100000/50/150/30/50  157114311 ns    157099353 ns            4 RowInvRate=1.57099us
BM_MatchProcessConfig/100000/50/50/40/50   123782470 ns    123781333 ns            6 RowInvRate=1.23781us
BM_MatchProcessConfig/100000/50/100/40/50  130441338 ns    130441145 ns            5 RowInvRate=1.30441us
BM_MatchProcessConfig/100000/50/150/40/50  159163151 ns    159153595 ns            4 RowInvRate=1.59154us
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
