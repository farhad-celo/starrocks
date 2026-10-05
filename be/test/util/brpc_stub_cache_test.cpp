// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "util/brpc_stub_cache.h"

#include <gtest/gtest.h>
#include <testutil/assert.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "runtime/exec_env.h"
#include "util/defer_op.h"
#include "util/failpoint/fail_point.h"

namespace starrocks {

class BrpcStubCacheTest : public testing::Test {
public:
    BrpcStubCacheTest() = default;
    ~BrpcStubCacheTest() override = default;
    void SetUp() override {
        _saved_brpc_max_connections_per_server = config::brpc_max_connections_per_server;
        _saved_brpc_stub_expire_s = config::brpc_stub_expire_s;
        _saved_brpc_connection_type = config::brpc_connection_type;
        config::brpc_max_connections_per_server = 1;
        config::brpc_connection_type = "single";
        _env._pipeline_timer = new pipeline::PipelineTimer();
        ASSERT_OK(_env._pipeline_timer->start());
    }
    void TearDown() override {
        delete _env._pipeline_timer;
        _env._pipeline_timer = nullptr;
        config::brpc_max_connections_per_server = _saved_brpc_max_connections_per_server;
        config::brpc_stub_expire_s = _saved_brpc_stub_expire_s;
        config::brpc_connection_type = _saved_brpc_connection_type;
    }

private:
    ExecEnv _env;
    int32_t _saved_brpc_max_connections_per_server = 0;
    int32_t _saved_brpc_stub_expire_s = 0;
    std::string _saved_brpc_connection_type;
};

TEST_F(BrpcStubCacheTest, normal) {
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;
    auto stub1 = cache.get_stub(address);
    ASSERT_NE(nullptr, stub1);
    address.port = 124;
    auto stub2 = cache.get_stub(address);
    ASSERT_NE(nullptr, stub2);
    ASSERT_NE(stub1, stub2);
    address.port = 123;
    auto stub3 = cache.get_stub(address);
    ASSERT_EQ(stub1, stub3);
}

TEST_F(BrpcStubCacheTest, invalid) {
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "invalid.cm.invalid";
    address.port = 123;
    auto stub1 = cache.get_stub(address);
    ASSERT_EQ(nullptr, stub1);
}

TEST_F(BrpcStubCacheTest, reset) {
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;
    auto stub1 = cache.get_stub(address);
    ASSERT_NE(nullptr, stub1);
    auto istub1 = stub1->stub();

    stub1->reset_channel();
    auto istub2 = stub1->stub();

    ASSERT_NE(istub1, istub2);
}

TEST_F(BrpcStubCacheTest, test_http_stub) {
    HttpBrpcStubCache cache;
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;
    auto stub1 = cache.get_http_stub(address);
    ASSERT_NE(nullptr, *stub1);
    address.port = 124;
    auto stub2 = cache.get_http_stub(address);
    ASSERT_NE(nullptr, *stub2);
    ASSERT_NE(*stub1, *stub2);
    address.port = 123;
    auto stub3 = cache.get_http_stub(address);
    ASSERT_NE(nullptr, *stub3);
    ASSERT_EQ(*stub1, *stub3);

    address.hostname = "invalid.cm.invalid";
    auto stub4 = cache.get_http_stub(address);
    ASSERT_EQ(nullptr, *stub4);
}

TEST_F(BrpcStubCacheTest, test_cleanup) {
    config::brpc_stub_expire_s = 1;
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;
    auto stub1 = cache.get_stub(address);
    ASSERT_NE(nullptr, stub1);
    auto stub2 = cache.get_stub(address);
    ASSERT_EQ(stub2, stub1);

    sleep(2);
    auto stub3 = cache.get_stub(address);
    ASSERT_NE(stub3, stub1);
}

TEST_F(BrpcStubCacheTest, test_http_cleanup) {
    config::brpc_stub_expire_s = 1;
    HttpBrpcStubCache cache;
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;
    auto stub1 = cache.get_http_stub(address);
    ASSERT_NE(nullptr, *stub1);
    auto stub2 = cache.get_http_stub(address);
    ASSERT_EQ(*stub2, *stub1);

    sleep(2);
    auto stub3 = cache.get_http_stub(address);
    ASSERT_NE(*stub3, *stub1);
}

TEST_F(BrpcStubCacheTest, test_destructor_joins_inflight_cleanup_tasks) {
    config::brpc_stub_expire_s = 1;
    auto cache = std::make_unique<BrpcStubCache>(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;
    auto stub = cache->get_stub(address);
    ASSERT_NE(nullptr, stub);

    cache.reset();

    auto cache2 = std::make_unique<BrpcStubCache>(&_env);
    auto fresh_stub = cache2->get_stub(address);
    ASSERT_NE(nullptr, fresh_stub);
}

TEST_F(BrpcStubCacheTest, test_active_access_keeps_stub_alive_across_expire_window) {
    config::brpc_stub_expire_s = 2;
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;
    auto stub1 = cache.get_stub(address);
    ASSERT_NE(nullptr, stub1);

    for (int i = 0; i < 3; ++i) {
        sleep(1);
        auto stub = cache.get_stub(address);
        ASSERT_EQ(stub1, stub) << "stub must not be evicted while being accessed";
    }
}

TEST_F(BrpcStubCacheTest, test_http_active_access_keeps_stub_alive_across_expire_window) {
    config::brpc_stub_expire_s = 2;
    HttpBrpcStubCache cache;
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;
    auto stub1 = cache.get_http_stub(address);
    ASSERT_NE(nullptr, *stub1);

    for (int i = 0; i < 3; ++i) {
        sleep(1);
        auto stub = cache.get_http_stub(address);
        ASSERT_NE(nullptr, *stub);
        ASSERT_EQ(*stub1, *stub) << "http stub must not be evicted while being accessed";
    }
}

TEST_F(BrpcStubCacheTest, acquire_least_loaded_stub) {
    config::brpc_max_connections_per_server = 3;
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;

    auto stub0 = cache.get_stub(address);
    ASSERT_NE(nullptr, stub0);

    auto first_or = cache.acquire_least_loaded_stub(stub0->endpoint());
    ASSERT_OK(first_or.status());
    auto first = std::move(first_or).value();
    ASSERT_EQ(stub0.get(), first.reservation.stub());
    ASSERT_EQ(0, first.reservation.in_flight_before());
    ASSERT_FALSE(first.created_on_contention);
    ASSERT_FALSE(first.selected_at_connection_limit);

    auto second_or = cache.acquire_least_loaded_stub(stub0->endpoint());
    ASSERT_OK(second_or.status());
    auto second = std::move(second_or).value();
    ASSERT_NE(stub0.get(), second.reservation.stub());
    ASSERT_EQ(0, second.reservation.in_flight_before());
    ASSERT_TRUE(second.created_on_contention);
    ASSERT_FALSE(second.selected_at_connection_limit);

    auto third_or = cache.acquire_least_loaded_stub(stub0->endpoint());
    ASSERT_OK(third_or.status());
    auto third = std::move(third_or).value();
    ASSERT_NE(stub0.get(), third.reservation.stub());
    ASSERT_NE(second.reservation.stub(), third.reservation.stub());
    ASSERT_EQ(0, third.reservation.in_flight_before());
    ASSERT_TRUE(third.created_on_contention);
    ASSERT_FALSE(third.selected_at_connection_limit);

    auto least_loaded_or = cache.acquire_least_loaded_stub(stub0->endpoint());
    ASSERT_OK(least_loaded_or.status());
    auto least_loaded = std::move(least_loaded_or).value();
    ASSERT_EQ(stub0.get(), least_loaded.reservation.stub());
    ASSERT_EQ(1, least_loaded.reservation.in_flight_before());
    ASSERT_FALSE(least_loaded.created_on_contention);
    ASSERT_TRUE(least_loaded.selected_at_connection_limit);
}

// Least-loaded selection and get_or_create() round-robin keep independent cursors, so an exchange sink's
// acquire_least_loaded_stub() calls must not perturb the even distribution get_stub() callers rely on.
TEST_F(BrpcStubCacheTest, acquire_least_loaded_does_not_perturb_round_robin) {
    config::brpc_max_connections_per_server = 3;
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;

    // Fill the pool; the round-robin cursor now points at stub0 (the last stub returned).
    auto stub0 = cache.get_stub(address);
    auto stub1 = cache.get_stub(address);
    auto stub2 = cache.get_stub(address);
    ASSERT_NE(nullptr, stub0);
    ASSERT_NE(stub0.get(), stub1.get());
    ASSERT_NE(stub1.get(), stub2.get());
    ASSERT_EQ(stub0.get(), cache.get_stub(address).get());

    // Make stub0 and stub1 busy so least-loaded selection picks the idle stub2, which sits at a
    // different index than the round-robin cursor. A shared cursor would be dragged to stub2 here.
    auto busy0 = stub0->reserve_rpc();
    auto busy1 = stub1->reserve_rpc();
    auto selected_or = cache.acquire_least_loaded_stub(stub0->endpoint());
    ASSERT_OK(selected_or.status());
    auto selected = std::move(selected_or).value();
    ASSERT_EQ(stub2.get(), selected.reservation.stub());

    // Round-robin must resume from where it left off (stub1, then stub2), unaffected by the
    // least-loaded selection above.
    ASSERT_EQ(stub1.get(), cache.get_stub(address).get());
    ASSERT_EQ(stub2.get(), cache.get_stub(address).get());
}

TEST_F(BrpcStubCacheTest, first_stub_is_not_created_on_contention) {
    BrpcStubCache cache(&_env);
    butil::EndPoint endpoint;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1", 123, &endpoint));

    auto pool = cache.get_or_create_pool(endpoint);
    ASSERT_NE(nullptr, pool);
    ASSERT_TRUE(pool->_stubs.empty());

    auto selected_or = pool->acquire_least_loaded(endpoint, 0);
    ASSERT_OK(selected_or.status());
    auto selected = std::move(selected_or).value();
    ASSERT_NE(nullptr, selected.reservation.stub());
    ASSERT_FALSE(selected.created_on_contention);
    ASSERT_FALSE(selected.selected_at_connection_limit);
}

TEST_F(BrpcStubCacheTest, stub_creation_failure_is_not_connection_limit) {
#ifdef FIU_ENABLE
    config::brpc_max_connections_per_server = 2;
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;

    auto stub = cache.get_stub(address);
    ASSERT_NE(nullptr, stub);
    auto busy = stub->reserve_rpc();

    auto* failpoint = failpoint::FailPointRegistry::GetInstance()->get("brpc_stub_cache_create_stub_failed");
    ASSERT_NE(nullptr, failpoint);
    PFailPointTriggerMode mode;
    mode.set_mode(FailPointTriggerModeType::ENABLE);
    failpoint->setMode(mode);
    DeferOp disable_failpoint([&] {
        mode.set_mode(FailPointTriggerModeType::DISABLE);
        failpoint->setMode(mode);
    });

    auto selected_or = cache.acquire_least_loaded_stub(stub->endpoint());
    ASSERT_OK(selected_or.status());
    auto selected = std::move(selected_or).value();
    ASSERT_EQ(stub.get(), selected.reservation.stub());
    ASSERT_FALSE(selected.created_on_contention);
    ASSERT_FALSE(selected.selected_at_connection_limit);
#else
    GTEST_SKIP() << "requires FIU_ENABLE to inject stub creation failure";
#endif
}

TEST_F(BrpcStubCacheTest, acquire_least_loaded_stub_prefers_lower_payload_load) {
    config::brpc_max_connections_per_server = 2;
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;

    auto stub0 = cache.get_stub(address);
    ASSERT_NE(nullptr, stub0);
    const int64_t batch_bytes = std::max<int64_t>(config::max_transmit_batched_bytes, 1);

    auto first_or = cache.acquire_least_loaded_stub(stub0->endpoint(), 2 * batch_bytes);
    ASSERT_OK(first_or.status());
    auto first = std::move(first_or).value();
    ASSERT_EQ(stub0.get(), first.reservation.stub());

    auto second_or = cache.acquire_least_loaded_stub(stub0->endpoint(), batch_bytes);
    ASSERT_OK(second_or.status());
    auto second = std::move(second_or).value();
    ASSERT_NE(stub0.get(), second.reservation.stub());

    auto selected_or = cache.acquire_least_loaded_stub(stub0->endpoint());
    ASSERT_OK(selected_or.status());
    auto selected = std::move(selected_or).value();
    ASSERT_EQ(second.reservation.stub(), selected.reservation.stub());
    ASSERT_EQ(1, selected.reservation.in_flight_before());
    ASSERT_TRUE(selected.selected_at_connection_limit);
}

TEST_F(BrpcStubCacheTest, dynamic_selection_requires_single_connection_type) {
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;
    auto stub = cache.get_stub(address);
    ASSERT_NE(nullptr, stub);

    for (const auto* connection_type : {"pooled", "short"}) {
        config::brpc_connection_type = connection_type;
        auto selection = cache.acquire_least_loaded_stub(stub->endpoint());
        ASSERT_FALSE(selection.ok());
        ASSERT_TRUE(selection.status().is_not_supported());
    }
}

TEST_F(BrpcStubCacheTest, concurrent_acquire_does_not_exceed_limit) {
    config::brpc_max_connections_per_server = 4;
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;

    auto initial_stub = cache.get_stub(address);
    ASSERT_NE(nullptr, initial_stub);
    auto initial_reservation = initial_stub->reserve_rpc();

    std::atomic<bool> succeeded = true;
    std::mutex selections_mutex;
    std::vector<BrpcStubCache::StubSelection> selections;
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([&]() {
            auto result = cache.acquire_least_loaded_stub(initial_stub->endpoint());
            if (!result.ok()) {
                succeeded = false;
                return;
            }
            std::lock_guard lock(selections_mutex);
            selections.emplace_back(std::move(result).value());
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_TRUE(succeeded);
    ASSERT_EQ(16, selections.size());
    size_t created_on_contention = 0;
    std::unordered_set<PInternalService_RecoverableStub*> selected_stubs{initial_stub.get()};
    for (const auto& selection : selections) {
        created_on_contention += selection.created_on_contention;
        selected_stubs.insert(selection.reservation.stub());
    }
    ASSERT_EQ(3, created_on_contention);
    ASSERT_EQ(config::brpc_max_connections_per_server, selected_stubs.size());
}

TEST_F(BrpcStubCacheTest, get_or_create_pool_returns_stable_pool) {
    config::brpc_max_connections_per_server = 2;
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;

    auto stub0 = cache.get_stub(address);
    ASSERT_NE(nullptr, stub0);

    auto pool = cache.get_or_create_pool(stub0->endpoint());
    ASSERT_NE(nullptr, pool);
    // Repeated lookups renew the deadline and return the same pool instance rather than recreating it.
    ASSERT_EQ(pool.get(), cache.get_or_create_pool(stub0->endpoint()).get());

    // Selecting through the cached pool yields the stub already created via get_stub.
    auto selected = pool->acquire_least_loaded(stub0->endpoint(), 0);
    ASSERT_OK(selected.status());
    ASSERT_EQ(stub0.get(), std::move(selected).value().reservation.stub());
}

TEST_F(BrpcStubCacheTest, in_use_pool_survives_expiry_and_stays_shared) {
    config::brpc_stub_expire_s = 1;
    config::brpc_max_connections_per_server = 2;
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;

    auto stub = cache.get_stub(address);
    ASSERT_NE(nullptr, stub);
    const auto endpoint = stub->endpoint();
    auto pool = cache.get_or_create_pool(endpoint);
    ASSERT_NE(nullptr, pool);

    // A caller holds the pool across the expiry window. The cleanup task must not evict a referenced pool, so
    // concurrent callers to the same endpoint keep sharing the same pool (and its load accounting).
    sleep(2);
    auto shared = cache.get_or_create_pool(endpoint);
    ASSERT_EQ(pool.get(), shared.get());

    // The retained pool stays fully usable and routes to the stub created via get_stub.
    auto selected = pool->acquire_least_loaded(endpoint, 0);
    ASSERT_OK(selected.status());
    ASSERT_EQ(stub.get(), std::move(selected).value().reservation.stub());
}

TEST_F(BrpcStubCacheTest, idle_pool_is_evicted_after_expiry) {
    config::brpc_stub_expire_s = 1;
    config::brpc_max_connections_per_server = 2;
    BrpcStubCache cache(&_env);
    TNetworkAddress address;
    address.hostname = "127.0.0.1";
    address.port = 123;

    auto stub = cache.get_stub(address);
    ASSERT_NE(nullptr, stub);
    const auto endpoint = stub->endpoint();

    // Hold no reference to the pool (the stub does not reference its pool). After the idle window the cleanup
    // task evicts the map entry, so a fresh lookup rebuilds an empty pool.
    sleep(2);
    auto fresh = cache.get_or_create_pool(endpoint);
    ASSERT_NE(nullptr, fresh);
    ASSERT_TRUE(fresh->_stubs.empty());
}

} // namespace starrocks
