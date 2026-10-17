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

#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

#include "common/config.h"
#include "common/logging.h"

namespace starrocks::pipeline {
// Manage the memory usage for local exchange
class ChunkBufferMemoryManager {
public:
    // `adaptive` opts the exchange in to the adaptive per-driver limit, see enable_local_exchange_adaptive_buffer.
    // It is only safe for exchanges where a full buffer implies that some source has output: whole chunks are
    // queued, or the number of sources doesn't exceed max_input_dop so that a source holding its share flushes its
    // partial rows.
    ChunkBufferMemoryManager(size_t max_input_dop, int64_t per_driver_mem_limit,
                             int64_t max_buffered_rows = std::numeric_limits<int64_t>::max(), bool adaptive = false)
            : _max_input_dop(max_input_dop) {
        _max_buffered_rows = max_buffered_rows;
        if (per_driver_mem_limit > 0) {
            _max_memory_usage_per_driver = per_driver_mem_limit;
        } else {
            LOG(WARNING) << "invalid per_driver_mem_limit";
        }
        size_t res = max_input_dop * _max_memory_usage_per_driver;
        if (res < _max_memory_usage) {
            _max_memory_usage = res;
        }
        // Snapshot the configs, so that changing them doesn't shift the limit of running queries.
        const int64_t initial = config::local_exchange_buffer_initial_mem_limit_per_driver;
        _adaptive = adaptive && config::enable_local_exchange_adaptive_buffer && initial > 0;
        _min_chunks = std::max<int64_t>(1, config::local_exchange_buffer_min_chunks_per_driver);
        _growth_factor = std::max(1.5, config::local_exchange_buffer_growth_factor);
        _limit_per_driver = _adaptive ? std::min<size_t>(initial, _max_memory_usage_per_driver) : 0;
    }

    // The sink reports the size of the chunks it pushes, so that the buffer limit is never smaller than a few chunks.
    // Returns true if the limit was raised.
    bool observe_chunk_memory_usage(size_t bytes) {
        if (!_adaptive || _release_mode.load() || bytes == 0) {
            return false;
        }
        size_t prev = _avg_chunk_memory_usage.load(std::memory_order_relaxed);
        // Follow an increase immediately, so that the floor doesn't lag behind a jump of the chunk size, and decay
        // slowly otherwise. The limit never shrinks anyway. Racy read-modify-write is fine: it is only a heuristic.
        size_t avg = bytes > prev ? bytes : prev - prev / 8 + bytes / 8;
        _avg_chunk_memory_usage.store(avg, std::memory_order_relaxed);
        return _raise_limit(_min_chunks * avg);
    }

    // The per-driver limit currently in effect. It never decreases, except by entering the release memory mode.
    size_t get_current_limit_per_driver() const {
        if (!_adaptive) {
            return _max_memory_usage_per_driver;
        }
        if (_release_mode.load()) {
            return _max_memory_usage / std::max<size_t>(1, _max_input_dop);
        }
        return _limit_per_driver.load();
    }

    bool is_adaptive() const { return _adaptive; }

    bool can_grow() const {
        return _adaptive && !_release_mode.load() && _limit_per_driver.load() < _max_memory_usage_per_driver;
    }

    // Raise the limit by the growth factor only if it is still `observed_limit`, so that many callers that saw the same
    // limit (e.g. all sources flushing at once) result in a single step. Returns true if this call raised the limit.
    bool try_grow_from(size_t observed_limit) {
        if (!can_grow()) {
            return false;
        }
        size_t target = std::min(static_cast<size_t>(observed_limit * _growth_factor), _max_memory_usage_per_driver);
        if (target <= observed_limit || !_limit_per_driver.compare_exchange_strong(observed_limit, target)) {
            return false;
        }
        _grow_count++;
        return true;
    }

    size_t get_grow_count() const { return _grow_count; }

    void update_memory_usage(int64_t memory_usage, int64_t num_rows) {
        bool prev_full = is_full();
        size_t prev_memusage = _memory_usage.fetch_add(memory_usage);
        size_t prev_num_rows = _buffered_num_rows.fetch_add(num_rows);
        bool is_full =
                prev_memusage + memory_usage >= max_memory_usage() || prev_num_rows + num_rows > _max_buffered_rows;
        bool expect = false;
        bool full_changed = prev_full != is_full;
        if (!full_changed) {
            return;
        }
        _full_events_changed.compare_exchange_strong(expect, full_changed);
    }

    size_t get_memory_limit_per_driver() const { return _max_memory_usage_per_driver; }

    int64_t get_memory_usage() const { return _memory_usage; }

    bool is_full() const { return _memory_usage >= max_memory_usage() || _buffered_num_rows > _max_buffered_rows; }

    // Full because of the memory limit, as opposed to the buffered rows limit.
    bool is_memory_full() const { return _memory_usage >= max_memory_usage(); }

    // Compared with the maximum limit of the exchange (lowered only by the release memory mode), not the adaptive
    // limit: the connector sink writer scaling is calibrated on it, and that exchanger is never adaptive.
    bool is_half_full() const { return _memory_usage * 2 >= _max_memory_usage; }

    size_t get_max_input_dop() const { return _max_input_dop; }

    // Enters the release memory mode: the total limit is pinned to the given value and never grows again.
    void update_max_memory_usage(size_t max_memory_usage) {
        DCHECK(max_memory_usage > 0);
        // Set the mode first, so that a concurrent growth can't raise the limit after the pinned value is written.
        _release_mode = true;
        _max_memory_usage = max_memory_usage;
    }

    bool in_release_mode() const { return _release_mode; }

    // The total limit currently in effect.
    size_t max_memory_usage() const {
        if (!_adaptive || _release_mode.load()) {
            return _max_memory_usage;
        }
        return std::min<size_t>(_max_memory_usage, _max_input_dop * get_current_limit_per_driver());
    }

    void clear() {
        _memory_usage = 0;
        _buffered_num_rows = 0;
    }

    bool full_events_changed() {
        if (!_full_events_changed.load(std::memory_order_acquire)) {
            return false;
        }
        bool val = true;
        return _full_events_changed.compare_exchange_strong(val, false);
    }

private:
    // Raise the limit to `target` (clamped to the maximum). Returns true if the limit was raised by this call.
    bool _raise_limit(size_t target) {
        target = std::min(target, _max_memory_usage_per_driver);
        size_t prev = _limit_per_driver.load();
        while (prev < target) {
            if (_limit_per_driver.compare_exchange_weak(prev, target)) {
                return true;
            }
        }
        return false;
    }

    std::atomic<size_t> _max_memory_usage{128UL * 1024 * 1024 * 1024}; // 128GB
    size_t _max_memory_usage_per_driver = 128 * 1024 * 1024UL;         // 128MB
    size_t _max_buffered_rows{};
    std::atomic<int64_t> _memory_usage{};
    std::atomic<int64_t> _buffered_num_rows{};
    size_t _max_input_dop;
    std::atomic<bool> _full_events_changed{};

    // Adaptive limit, see local_exchange_buffer_initial_mem_limit_per_driver.
    bool _adaptive = false;
    int64_t _min_chunks = 1;
    double _growth_factor = 2.0;
    // Only ever raised.
    std::atomic<size_t> _limit_per_driver{0};
    std::atomic<size_t> _avg_chunk_memory_usage{0};
    std::atomic<size_t> _grow_count{0};
    // Sticky: once set, the limit is pinned and never grows.
    std::atomic<bool> _release_mode{false};
};
} // namespace starrocks::pipeline
