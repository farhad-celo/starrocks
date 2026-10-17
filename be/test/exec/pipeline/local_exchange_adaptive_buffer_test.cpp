// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <atomic>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "column/chunk.h"
#include "common/config.h"
#include "exec/chunk_buffer_memory_manager.h"
#include "exec/pipeline/exchange/local_exchange.h"
#include "exec/pipeline/exchange/local_exchange_source_operator.h"
#include "exec/pipeline/query_context.h"
#include "gutil/casts.h"
#include "runtime/exec_env.h"
#include "runtime/runtime_state.h"
#include "testutil/assert.h"
#include "testutil/column_test_helper.h"
#include "util/defer_op.h"

namespace starrocks::pipeline {

namespace {
// One growth step from the current limit.
bool grow(ChunkBufferMemoryManager& mgr) {
    return mgr.try_grow_from(mgr.get_current_limit_per_driver());
}

constexpr size_t kMaxPerDriver = 16000;
constexpr size_t kInitialPerDriver = 1000;
constexpr size_t kDop = 2;
constexpr int64_t kNoRowsLimit = std::numeric_limits<int64_t>::max();

ChunkPtr make_chunk(size_t num_rows) {
    auto chunk = std::make_shared<Chunk>();
    chunk->append_column(ColumnTestHelper::build_column<int32_t>(std::vector<int32_t>(num_rows, 1)), 1);
    return chunk;
}
} // namespace

class LocalExchangeAdaptiveBufferTest : public ::testing::Test {
public:
    void SetUp() override {
        _prev_enable = config::enable_local_exchange_adaptive_buffer;
        _prev_initial = config::local_exchange_buffer_initial_mem_limit_per_driver;
        _prev_chunks = config::local_exchange_buffer_min_chunks_per_driver;
        _prev_factor = config::local_exchange_buffer_growth_factor;
        config::enable_local_exchange_adaptive_buffer = true;
        config::local_exchange_buffer_initial_mem_limit_per_driver = kInitialPerDriver;
        config::local_exchange_buffer_min_chunks_per_driver = 2;
        config::local_exchange_buffer_growth_factor = 2.0;
    }

    void TearDown() override {
        config::enable_local_exchange_adaptive_buffer = _prev_enable;
        config::local_exchange_buffer_initial_mem_limit_per_driver = _prev_initial;
        config::local_exchange_buffer_min_chunks_per_driver = _prev_chunks;
        config::local_exchange_buffer_growth_factor = _prev_factor;
        if (_exchanger != nullptr) {
            _exchanger->close(_runtime_state.get());
        }
    }

protected:
    // Builds a memory manager, `kDop` sources and a DirectThroughExchanger on top of them.
    // Chunks from sink sequence `i` always go to source `i % kDop`, so a test controls which source is starved.
    void build_exchange() {
        _memory_manager = std::make_shared<ChunkBufferMemoryManager>(kDop, kMaxPerDriver, kNoRowsLimit, true);

        _query_context = std::make_shared<QueryContext>();
        _query_context->init_mem_tracker(-1, GlobalEnv::GetInstance()->process_mem_tracker());
        TQueryOptions query_options;
        query_options.batch_size = 4096;
        TQueryGlobals query_globals;
        _runtime_state =
                std::make_shared<RuntimeState>(_fragment_id, query_options, query_globals, ExecEnv::GetInstance());
        _runtime_state->set_query_ctx(_query_context.get());
        _runtime_state->init_instance_mem_tracker();

        _source_factory = std::make_unique<LocalExchangeSourceOperatorFactory>(0, 1, _memory_manager);
        _source_factory->set_runtime_state(_runtime_state.get());
        for (size_t i = 0; i < kDop; i++) {
            _sources.emplace_back(_source_factory->create(kDop, i));
        }
        _exchanger = std::make_unique<DirectThroughExchanger>(_memory_manager, _source_factory.get());
        ASSERT_OK(_exchanger->prepare(_runtime_state.get()));
    }

    LocalExchangeSourceOperator* source(size_t i) const {
        return down_cast<LocalExchangeSourceOperator*>(_sources[i].get());
    }

    // Push chunks until the shared buffer is full. `sink_sequence_of` maps the push index to a sink sequence.
    template <class F>
    void fill_until_full(F&& sink_sequence_of) {
        for (int i = 0; i < 1000 && !_memory_manager->is_full(); i++) {
            ASSERT_OK(_exchanger->accept(make_chunk(50), sink_sequence_of(i)));
        }
        ASSERT_TRUE(_memory_manager->is_full());
    }

    bool _prev_enable{};
    int64_t _prev_initial{};
    int64_t _prev_chunks{};
    double _prev_factor{};

    std::shared_ptr<ChunkBufferMemoryManager> _memory_manager;
    std::unique_ptr<LocalExchangeSourceOperatorFactory> _source_factory;
    std::unique_ptr<DirectThroughExchanger> _exchanger;
    std::vector<OperatorPtr> _sources;
    TUniqueId _fragment_id;
    std::shared_ptr<QueryContext> _query_context;
    std::shared_ptr<RuntimeState> _runtime_state;
};

// ---- ChunkBufferMemoryManager ----

TEST_F(LocalExchangeAdaptiveBufferTest, disabled_uses_max_limit) {
    config::enable_local_exchange_adaptive_buffer = false;
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    ASSERT_EQ(kMaxPerDriver, mgr.get_current_limit_per_driver());
    ASSERT_EQ(kDop * kMaxPerDriver, mgr.max_memory_usage());
    ASSERT_FALSE(mgr.can_grow());
    ASSERT_FALSE(grow(mgr));
    ASSERT_EQ(0, mgr.get_grow_count());
}

TEST_F(LocalExchangeAdaptiveBufferTest, not_adaptive_unless_opted_in) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver);

    ASSERT_EQ(kMaxPerDriver, mgr.get_current_limit_per_driver());
    ASSERT_EQ(kDop * kMaxPerDriver, mgr.max_memory_usage());
    ASSERT_FALSE(mgr.can_grow());
    ASSERT_FALSE(mgr.observe_chunk_memory_usage(3000));
}

TEST_F(LocalExchangeAdaptiveBufferTest, zero_initial_limit_disables) {
    config::local_exchange_buffer_initial_mem_limit_per_driver = 0;
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    ASSERT_EQ(kMaxPerDriver, mgr.get_current_limit_per_driver());
    ASSERT_FALSE(mgr.can_grow());
}

TEST_F(LocalExchangeAdaptiveBufferTest, starts_at_initial_limit) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    ASSERT_EQ(kInitialPerDriver, mgr.get_current_limit_per_driver());
    ASSERT_EQ(kDop * kInitialPerDriver, mgr.max_memory_usage());
    ASSERT_TRUE(mgr.can_grow());
}

TEST_F(LocalExchangeAdaptiveBufferTest, grow_multiplies_and_clamps_at_max) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    size_t expected = kInitialPerDriver;
    size_t grows = 0;
    while (expected < kMaxPerDriver) {
        ASSERT_TRUE(grow(mgr));
        expected = std::min<size_t>(expected * 2, kMaxPerDriver);
        grows++;
        ASSERT_EQ(expected, mgr.get_current_limit_per_driver());
    }
    // 1000 -> 2000 -> 4000 -> 8000 -> 16000
    ASSERT_EQ(4, grows);
    ASSERT_EQ(grows, mgr.get_grow_count());

    // At the ceiling nothing can grow anymore.
    ASSERT_FALSE(mgr.can_grow());
    ASSERT_FALSE(grow(mgr));
    ASSERT_EQ(kMaxPerDriver, mgr.get_current_limit_per_driver());
    ASSERT_EQ(grows, mgr.get_grow_count());
}

TEST_F(LocalExchangeAdaptiveBufferTest, small_growth_factor_is_clamped) {
    config::local_exchange_buffer_growth_factor = 1.0;
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    ASSERT_TRUE(grow(mgr));
    ASSERT_EQ(1500, mgr.get_current_limit_per_driver());
}

TEST_F(LocalExchangeAdaptiveBufferTest, configs_are_snapshotted_at_construction) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    config::local_exchange_buffer_min_chunks_per_driver = 1000000;
    config::local_exchange_buffer_growth_factor = 8.0;
    ASSERT_EQ(kInitialPerDriver, mgr.get_current_limit_per_driver());
    mgr.observe_chunk_memory_usage(100);
    ASSERT_EQ(kInitialPerDriver, mgr.get_current_limit_per_driver());
    ASSERT_TRUE(grow(mgr));
    ASSERT_EQ(2 * kInitialPerDriver, mgr.get_current_limit_per_driver());
}

TEST_F(LocalExchangeAdaptiveBufferTest, limit_never_shrinks) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    ASSERT_TRUE(mgr.observe_chunk_memory_usage(3000));
    ASSERT_EQ(6000, mgr.get_current_limit_per_driver());

    // Narrower chunks lower the average, but not the limit.
    for (int i = 0; i < 200; i++) {
        ASSERT_FALSE(mgr.observe_chunk_memory_usage(10));
    }
    ASSERT_EQ(6000, mgr.get_current_limit_per_driver());
}

TEST_F(LocalExchangeAdaptiveBufferTest, concurrent_grow_counts_only_winners) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    std::vector<std::thread> threads;
    std::atomic<size_t> raised{0};
    for (int i = 0; i < 8; i++) {
        threads.emplace_back([&] {
            while (mgr.can_grow()) {
                if (grow(mgr)) {
                    raised++;
                }
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    ASSERT_EQ(kMaxPerDriver, mgr.get_current_limit_per_driver());
    ASSERT_EQ(raised.load(), mgr.get_grow_count());
}

TEST_F(LocalExchangeAdaptiveBufferTest, chunk_floor_raises_limit) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    // 2 chunks of 3000 bytes need 6000 bytes, more than the 1000 initial limit.
    ASSERT_TRUE(mgr.observe_chunk_memory_usage(3000));
    ASSERT_EQ(6000, mgr.get_current_limit_per_driver());
    ASSERT_EQ(kDop * 6000, mgr.max_memory_usage());

    // The floor is clamped to the ceiling.
    ChunkBufferMemoryManager huge(kDop, kMaxPerDriver, kNoRowsLimit, true);
    huge.observe_chunk_memory_usage(kMaxPerDriver * 10);
    ASSERT_EQ(kMaxPerDriver, huge.get_current_limit_per_driver());
    ASSERT_FALSE(huge.can_grow());
}

TEST_F(LocalExchangeAdaptiveBufferTest, chunk_floor_follows_a_jump_immediately) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    for (int i = 0; i < 10; i++) {
        mgr.observe_chunk_memory_usage(100);
    }
    ASSERT_EQ(kInitialPerDriver, mgr.get_current_limit_per_driver());
    // A single wider chunk raises the floor at once, without waiting for the average to catch up.
    ASSERT_TRUE(mgr.observe_chunk_memory_usage(3000));
    ASSERT_EQ(6000, mgr.get_current_limit_per_driver());
}

TEST_F(LocalExchangeAdaptiveBufferTest, try_grow_from_steps_once_for_concurrent_callers) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    size_t observed = mgr.get_current_limit_per_driver();
    ASSERT_TRUE(mgr.try_grow_from(observed));
    // Everybody else saw the same limit, so they don't step again.
    ASSERT_FALSE(mgr.try_grow_from(observed));
    ASSERT_FALSE(mgr.try_grow_from(observed));
    ASSERT_EQ(2 * kInitialPerDriver, mgr.get_current_limit_per_driver());
    ASSERT_EQ(1, mgr.get_grow_count());
}

TEST_F(LocalExchangeAdaptiveBufferTest, half_full_uses_the_fixed_maximum) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    // Well above half of the adaptive limit, far below half of the maximum.
    mgr.update_memory_usage(kDop * kInitialPerDriver, 10);
    ASSERT_TRUE(mgr.is_full());
    ASSERT_FALSE(mgr.is_half_full());
    mgr.update_memory_usage(kDop * kMaxPerDriver / 2, 10);
    ASSERT_TRUE(mgr.is_half_full());
}

TEST_F(LocalExchangeAdaptiveBufferTest, observe_ignored_when_disabled) {
    config::enable_local_exchange_adaptive_buffer = false;
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    mgr.observe_chunk_memory_usage(3000);
    ASSERT_EQ(kMaxPerDriver, mgr.get_current_limit_per_driver());
}

TEST_F(LocalExchangeAdaptiveBufferTest, is_full_follows_grown_limit) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    mgr.update_memory_usage(kDop * kInitialPerDriver, 10);
    ASSERT_TRUE(mgr.is_full());
    ASSERT_TRUE(grow(mgr));
    ASSERT_FALSE(mgr.is_full());
}

TEST_F(LocalExchangeAdaptiveBufferTest, release_mode_pins_limit_and_blocks_growth) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    mgr.update_max_memory_usage(500);
    ASSERT_TRUE(mgr.in_release_mode());
    ASSERT_EQ(500, mgr.max_memory_usage());
    // The reported per-driver limit is the pinned one, not the adaptive one.
    ASSERT_EQ(500 / kDop, mgr.get_current_limit_per_driver());
    ASSERT_FALSE(mgr.can_grow());
    ASSERT_FALSE(grow(mgr));

    // Neither a larger chunk floor nor a growth attempt may undo the pinned limit.
    mgr.observe_chunk_memory_usage(1000000);
    ASSERT_EQ(500, mgr.max_memory_usage());
    mgr.update_memory_usage(500, 1);
    ASSERT_TRUE(mgr.is_full());
}

// ---- LocalExchanger::maybe_grow_buffer ----

TEST_F(LocalExchangeAdaptiveBufferTest, grows_when_full_and_a_source_is_starved) {
    build_exchange();

    // Everything goes to source 0, source 1 stays empty.
    fill_until_full([](int) { return 0; });
    ASSERT_TRUE(source(0)->has_output());
    ASSERT_TRUE(source(1)->is_starved());

    size_t limit_before = _exchanger->get_current_limit_per_driver();
    _exchanger->maybe_grow_buffer();

    ASSERT_EQ(limit_before * 2, _exchanger->get_current_limit_per_driver());
    ASSERT_EQ(1, _exchanger->get_buffer_grow_count());
    ASSERT_TRUE(_exchanger->need_input());
}

TEST_F(LocalExchangeAdaptiveBufferTest, does_not_grow_when_all_sources_have_output) {
    build_exchange();

    // Alternate sinks, so both sources hold data.
    fill_until_full([](int i) { return i % 2; });
    ASSERT_FALSE(source(0)->is_starved());
    ASSERT_FALSE(source(1)->is_starved());

    size_t limit_before = _exchanger->get_current_limit_per_driver();
    _exchanger->maybe_grow_buffer();

    ASSERT_EQ(limit_before, _exchanger->get_current_limit_per_driver());
    ASSERT_EQ(0, _exchanger->get_buffer_grow_count());
    ASSERT_FALSE(_exchanger->need_input());
}

TEST_F(LocalExchangeAdaptiveBufferTest, does_not_grow_when_buffer_not_full) {
    build_exchange();

    ASSERT_OK(_exchanger->accept(make_chunk(1), 0));
    ASSERT_FALSE(_memory_manager->is_full());
    ASSERT_TRUE(source(1)->is_starved());

    _exchanger->maybe_grow_buffer();
    ASSERT_EQ(0, _exchanger->get_buffer_grow_count());
    ASSERT_EQ(kInitialPerDriver, _exchanger->get_current_limit_per_driver());
}

TEST_F(LocalExchangeAdaptiveBufferTest, does_not_grow_when_full_because_of_rows_limit) {
    // Rows limit of 100, far below the memory limit.
    _memory_manager = std::make_shared<ChunkBufferMemoryManager>(kDop, kMaxPerDriver, 100, true);
    _memory_manager->update_memory_usage(10, 1000);
    ASSERT_TRUE(_memory_manager->is_full());
    ASSERT_FALSE(_memory_manager->is_memory_full());
}

TEST_F(LocalExchangeAdaptiveBufferTest, source_pull_grows_buffer_when_starved) {
    build_exchange();

    // The sink is blocked by the full buffer, and the only way to unblock it is a source-side trigger.
    fill_until_full([](int) { return 0; });
    ASSERT_TRUE(source(1)->is_starved());
    size_t limit_before = _exchanger->get_current_limit_per_driver();

    while (source(0)->has_output() && _memory_manager->is_memory_full()) {
        ASSERT_OK(source(0)->pull_chunk(_runtime_state.get()).status());
    }
    // Pulling from source 0 either freed enough room or grew the limit, so the sink can make progress.
    ASSERT_TRUE(_exchanger->need_input());
    ASSERT_GE(_exchanger->get_current_limit_per_driver(), limit_before);
}

TEST_F(LocalExchangeAdaptiveBufferTest, does_not_grow_in_release_mode) {
    build_exchange();

    // Pin the total limit as the memory-pressure mode does, then fill it with a starved source around.
    _memory_manager->update_max_memory_usage(1500);
    fill_until_full([](int) { return 0; });
    ASSERT_TRUE(source(1)->is_starved());

    _exchanger->maybe_grow_buffer();
    ASSERT_EQ(0, _exchanger->get_buffer_grow_count());
    ASSERT_FALSE(_exchanger->need_input());
}

TEST_F(LocalExchangeAdaptiveBufferTest, stops_growing_at_the_ceiling) {
    build_exchange();

    // Keep source 1 starved and keep filling, growing each time, until the ceiling is reached.
    for (int round = 0; round < 20 && _memory_manager->can_grow(); round++) {
        fill_until_full([](int) { return 0; });
        _exchanger->maybe_grow_buffer();
    }
    ASSERT_EQ(kMaxPerDriver, _exchanger->get_current_limit_per_driver());
    ASSERT_FALSE(_memory_manager->can_grow());

    size_t grows = _exchanger->get_buffer_grow_count();
    _exchanger->maybe_grow_buffer();
    ASSERT_EQ(grows, _exchanger->get_buffer_grow_count());
}

TEST_F(LocalExchangeAdaptiveBufferTest, finished_source_is_not_starved) {
    build_exchange();

    ASSERT_TRUE(source(1)->is_starved());
    ASSERT_OK(source(1)->set_finishing(_runtime_state.get()));
    ASSERT_FALSE(source(1)->is_starved());
}

// ---- LocalExchangeSourceOperator flush threshold ----

TEST_F(LocalExchangeAdaptiveBufferTest, release_mode_ignores_chunk_samples) {
    ChunkBufferMemoryManager mgr(kDop, kMaxPerDriver, kNoRowsLimit, true);

    mgr.update_max_memory_usage(500);
    // Nothing reads the adaptive limit anymore, so a sample must not report a raise (and wake the sinks).
    ASSERT_FALSE(mgr.observe_chunk_memory_usage(3000));
}

TEST_F(LocalExchangeAdaptiveBufferTest, is_adaptive_follows_opt_in) {
    ChunkBufferMemoryManager adaptive(kDop, kMaxPerDriver, kNoRowsLimit, true);
    ChunkBufferMemoryManager fixed(kDop, kMaxPerDriver);
    ASSERT_TRUE(adaptive.is_adaptive());
    ASSERT_FALSE(fixed.is_adaptive());
}

TEST_F(LocalExchangeAdaptiveBufferTest, drained_source_does_not_grow_when_it_is_not_starved) {
    build_exchange();

    // Both sources hold data and the buffer is full: pulling from source 0 leaves it with output, so the hint
    // must not cause a growth step.
    fill_until_full([](int i) { return i % 2; });
    size_t limit_before = _exchanger->get_current_limit_per_driver();
    _exchanger->maybe_grow_buffer(source(0));
    ASSERT_EQ(limit_before, _exchanger->get_current_limit_per_driver());
    ASSERT_EQ(0, _exchanger->get_buffer_grow_count());
}

TEST_F(LocalExchangeAdaptiveBufferTest, starved_hint_grows_once) {
    build_exchange();

    fill_until_full([](int) { return 0; });
    size_t limit_before = _exchanger->get_current_limit_per_driver();
    _exchanger->maybe_grow_buffer(source(1));
    ASSERT_EQ(2 * limit_before, _exchanger->get_current_limit_per_driver());
    ASSERT_EQ(1, _exchanger->get_buffer_grow_count());
}

TEST_F(LocalExchangeAdaptiveBufferTest, source_flush_threshold_follows_current_limit) {
    build_exchange();

    // A partial shuffle chunk (fewer rows than chunk_size) is only released once the local buffer is almost full,
    // i.e. at 80% of the current per-driver limit.
    auto indexes = std::make_shared<std::vector<uint32_t>>(10, 0);
    size_t usage = kInitialPerDriver * 0.9;
    ASSERT_OK(source(0)->add_chunk(make_chunk(10), indexes, 0, 10, usage));
    ASSERT_TRUE(source(0)->has_output());

    // After growth the same buffered bytes are well below 80% of the new limit, so keep accumulating rows.
    ASSERT_TRUE(grow(*_memory_manager));
    ASSERT_FALSE(source(0)->has_output());
}

TEST_F(LocalExchangeAdaptiveBufferTest, source_flush_of_underfilled_chunk_grows_buffer) {
    build_exchange();

    // The partial chunk is flushed only because it exceeds 80% of the small limit, and the buffer is not full,
    // so the only trigger left is the flush itself.
    auto indexes = std::make_shared<std::vector<uint32_t>>(10, 0);
    ASSERT_OK(source(0)->add_chunk(make_chunk(10), indexes, 0, 10, kInitialPerDriver));
    ASSERT_FALSE(_memory_manager->is_memory_full());
    ASSERT_TRUE(source(0)->has_output());

    ASSERT_OK(source(0)->pull_chunk(_runtime_state.get()).status());
    ASSERT_EQ(2 * kInitialPerDriver, _exchanger->get_current_limit_per_driver());
    ASSERT_EQ(1, _exchanger->get_buffer_grow_count());
}

TEST_F(LocalExchangeAdaptiveBufferTest, source_flush_of_full_chunk_does_not_grow_buffer) {
    build_exchange();

    // chunk_size rows are enough to assemble a full chunk, so the limit is not the problem.
    auto rows = _runtime_state->chunk_size();
    auto indexes = std::make_shared<std::vector<uint32_t>>(rows, 0);
    ASSERT_OK(source(0)->add_chunk(make_chunk(rows), indexes, 0, rows, 10));
    ASSERT_TRUE(source(0)->has_output());

    ASSERT_OK(source(0)->pull_chunk(_runtime_state.get()).status());
    ASSERT_EQ(0, _exchanger->get_buffer_grow_count());
}

TEST_F(LocalExchangeAdaptiveBufferTest, source_flush_threshold_is_fixed_in_release_mode) {
    build_exchange();

    // 2000 bytes of a partial chunk exceed 80% of the 1000 byte initial limit, so it is flushed.
    auto indexes = std::make_shared<std::vector<uint32_t>>(10, 0);
    ASSERT_OK(source(0)->add_chunk(make_chunk(10), indexes, 0, 10, 2000));
    ASSERT_TRUE(source(0)->has_output());

    // Release mode switches the source to its fixed 1MB threshold and pins the manager: 2000 bytes are no longer
    // enough to flush a partial chunk, and the limit can't be grown back.
    source(0)->enter_release_memory_mode();
    ASSERT_TRUE(_memory_manager->in_release_mode());
    ASSERT_FALSE(_memory_manager->can_grow());
    ASSERT_FALSE(source(0)->has_output());
}

} // namespace starrocks::pipeline
