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

#include "util/internal_service_recoverable_stub.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <string>
#include <utility>

#include "util/ref_count_closure.h"

using namespace starrocks;

namespace {

class HoldingRpcChannel : public google::protobuf::RpcChannel {
public:
    void CallMethod(const google::protobuf::MethodDescriptor*, google::protobuf::RpcController*,
                    const google::protobuf::Message*, google::protobuf::Message*,
                    google::protobuf::Closure* done) override {
        _done = done;
    }

    void complete() {
        auto* done = std::exchange(_done, nullptr);
        done->Run();
    }

private:
    google::protobuf::Closure* _done = nullptr;
};

class InspectingRpcChannel : public google::protobuf::RpcChannel {
public:
    InspectingRpcChannel(PInternalService_RecoverableStub* stub, int64_t expected_payload_bytes)
            : _stub(stub), _expected_payload_bytes(expected_payload_bytes) {}

    void CallMethod(const google::protobuf::MethodDescriptor*, google::protobuf::RpcController*,
                    const google::protobuf::Message*, google::protobuf::Message*,
                    google::protobuf::Closure* done) override {
        EXPECT_EQ(nullptr, done);
        EXPECT_EQ(1, _stub->num_in_flight_rpcs());
        EXPECT_EQ(_expected_payload_bytes, _stub->num_in_flight_payload_bytes());
    }

private:
    PInternalService_RecoverableStub* _stub;
    int64_t _expected_payload_bytes;
};

class CheckLoadReleasedClosure : public google::protobuf::Closure {
public:
    explicit CheckLoadReleasedClosure(PInternalService_RecoverableStub* stub) : _stub(stub) {}

    void Run() override {
        EXPECT_EQ(0, _stub->num_in_flight_rpcs());
        EXPECT_EQ(0, _stub->num_in_flight_payload_bytes());
        called = true;
    }

    bool called = false;

private:
    PInternalService_RecoverableStub* _stub;
};

} // namespace

class PInternalService_RecoverableStubTest : public testing::Test {
public:
    PInternalService_RecoverableStubTest() = default;
    ~PInternalService_RecoverableStubTest() override = default;

    template <typename SendRpc>
    void check_payload_accounting(int64_t request_bytes, SendRpc send_rpc) {
        butil::EndPoint point;
        ASSERT_EQ(0, butil::str2endpoint("127.0.0.1", 18000, &point));
        HoldingRpcChannel channel;
        auto stub = std::make_shared<PInternalService_RecoverableStub>(point);
        stub->_stub = std::make_shared<PInternalService_Stub>(&channel);

        for (bool fail : {false, true}) {
            brpc::Controller cntl;
            const std::string attachment(4096, 'a');
            cntl.request_attachment().append(attachment);
            CheckLoadReleasedClosure done(stub.get());
            send_rpc(stub.get(), &cntl, &done);
            EXPECT_EQ(1, stub->num_in_flight_rpcs());
            EXPECT_EQ(request_bytes + attachment.size(), stub->num_in_flight_payload_bytes());
            EXPECT_FALSE(done.called);

            if (fail) {
                cntl.SetFailed(EIO, "injected failure");
            }
            channel.complete();
            EXPECT_TRUE(done.called);
            EXPECT_EQ(0, stub->num_in_flight_rpcs());
            EXPECT_EQ(0, stub->num_in_flight_payload_bytes());
        }
    }
};

TEST_F(PInternalService_RecoverableStubTest, tablet_write_payload_accounting) {
    PTabletWriterAddChunkRequest request;
    request.mutable_chunk()->set_data(std::string(8192, 'b'));
    PTabletWriterAddBatchResult response;
    check_payload_accounting(request.ByteSizeLong(), [&](auto* stub, auto* cntl, auto* done) {
        stub->tablet_writer_add_chunk(cntl, &request, &response, done);
    });

    PTabletWriterAddChunksRequest requests;
    *requests.add_requests() = request;
    check_payload_accounting(requests.ByteSizeLong(), [&](auto* stub, auto* cntl, auto* done) {
        stub->tablet_writer_add_chunks(cntl, &requests, &response, done);
    });
}

TEST_F(PInternalService_RecoverableStubTest, transmit_chunk_payload_accounting) {
    PTransmitChunkParams request;
    request.add_chunks()->set_data(std::string(8192, 'b'));
    PTransmitChunkResult response;
    check_payload_accounting(request.ByteSizeLong(), [&](auto* stub, auto* cntl, auto* done) {
        stub->transmit_chunk(cntl, &request, &response, done);
    });

    // The pipeline path already reserves the complete payload before calling the stub.
    check_payload_accounting(request.ByteSizeLong(), [&](auto* stub, auto* cntl, auto* done) {
        auto reservation = stub->reserve_rpc(request.ByteSizeLong() + cntl->request_attachment().size());
        stub->transmit_chunk(std::move(reservation), cntl, &request, &response, done);
    });
}

TEST_F(PInternalService_RecoverableStubTest, http_attachment_payload_accounting) {
    PTransmitChunkResult transmit_response;
    check_payload_accounting(0, [&](auto* stub, auto* cntl, auto* done) {
        stub->transmit_chunk_via_http(cntl, nullptr, &transmit_response, done);
    });

    PTabletWriterAddBatchResult write_response;
    check_payload_accounting(0, [&](auto* stub, auto* cntl, auto* done) {
        stub->tablet_writer_add_chunk_via_http(cntl, nullptr, &write_response, done);
    });
    check_payload_accounting(0, [&](auto* stub, auto* cntl, auto* done) {
        stub->tablet_writer_add_chunks_via_http(cntl, nullptr, &write_response, done);
    });
}

TEST_F(PInternalService_RecoverableStubTest, fetch_datacache_payload_accounting) {
    butil::EndPoint point;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1", 18000, &point));
    auto stub = std::make_shared<PInternalService_RecoverableStub>(point);
    constexpr int64_t kResponseBytes = 256 * 1024;
    InspectingRpcChannel channel(stub.get(), kResponseBytes);
    stub->_stub = std::make_shared<PInternalService_Stub>(&channel);

    PFetchDataCacheRequest request;
    request.set_size(kResponseBytes);
    PFetchDataCacheResponse response;
    brpc::Controller cntl;
    stub->fetch_datacache(&cntl, &request, &response, nullptr);

    EXPECT_EQ(0, stub->num_in_flight_rpcs());
    EXPECT_EQ(0, stub->num_in_flight_payload_bytes());
}

TEST_F(PInternalService_RecoverableStubTest, execute_command) {
    std::shared_ptr<starrocks::PInternalService_RecoverableStub> stub;

    butil::EndPoint point;
    auto res = butil::str2endpoint("127.0.0.1", 8000, &point);
    ASSERT_EQ(res, 0);

    stub = std::make_shared<starrocks::PInternalService_RecoverableStub>(point);

    auto st = stub->reset_channel();
    ASSERT_TRUE(st.ok());

    auto* closure = new starrocks::RefCountClosure<starrocks::ExecuteCommandResultPB>();
    ExecuteCommandRequestPB request;
    stub->execute_command(&closure->cntl, &request, &closure->result, closure);
}

TEST_F(PInternalService_RecoverableStubTest, tablet_writer_add_chunks_via_http) {
    std::shared_ptr<starrocks::PInternalService_RecoverableStub> stub;

    butil::EndPoint point;
    auto res = butil::str2endpoint("127.0.0.1", 8000, &point);
    ASSERT_EQ(res, 0);

    stub = std::make_shared<starrocks::PInternalService_RecoverableStub>(point);

    auto st = stub->reset_channel();
    ASSERT_TRUE(st.ok());

    auto* closure = new starrocks::RefCountClosure<starrocks::PTabletWriterAddBatchResult>();
    stub->tablet_writer_add_chunks_via_http(&closure->cntl, nullptr, &closure->result, closure);

    auto* closure1 = new starrocks::RefCountClosure<starrocks::PTabletWriterAddBatchResult>();
    stub->tablet_writer_add_chunk_via_http(&closure1->cntl, nullptr, &closure1->result, closure1);

    auto* closure2 = new starrocks::RefCountClosure<starrocks::PTabletWriterAddBatchResult>();
    stub->tablet_writer_add_chunk(&closure2->cntl, nullptr, &closure2->result, closure2);
}

TEST_F(PInternalService_RecoverableStubTest, test_load_diagnose) {
    std::shared_ptr<starrocks::PInternalService_RecoverableStub> stub;

    butil::EndPoint point;
    auto res = butil::str2endpoint("127.0.0.1", 8000, &point);
    ASSERT_EQ(res, 0);

    stub = std::make_shared<starrocks::PInternalService_RecoverableStub>(point);

    auto st = stub->reset_channel();
    ASSERT_TRUE(st.ok());

    PLoadDiagnoseRequest request;
    auto* closure = new starrocks::RefCountClosure<starrocks::PLoadDiagnoseResult>();
    stub->load_diagnose(&closure->cntl, &request, &closure->result, closure);
}

TEST_F(PInternalService_RecoverableStubTest, test_get_load_replica_status) {
    std::shared_ptr<starrocks::PInternalService_RecoverableStub> stub;

    butil::EndPoint point;
    auto res = butil::str2endpoint("127.0.0.1", 8000, &point);
    ASSERT_EQ(res, 0);

    stub = std::make_shared<starrocks::PInternalService_RecoverableStub>(point);

    auto st = stub->reset_channel();
    ASSERT_TRUE(st.ok());

    PLoadReplicaStatusRequest request;
    auto* closure = new starrocks::RefCountClosure<starrocks::PLoadReplicaStatusResult>();
    stub->get_load_replica_status(&closure->cntl, &request, &closure->result, closure);
}

TEST_F(PInternalService_RecoverableStubTest, rpc_in_flight_guard) {
    butil::EndPoint point;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1", 18000, &point));
    auto stub = std::make_shared<starrocks::PInternalService_RecoverableStub>(point);

    ASSERT_EQ(0, stub->num_in_flight_rpcs());
    ASSERT_EQ(0, stub->num_in_flight_payload_bytes());
    auto first = stub->reserve_rpc(1024);
    ASSERT_EQ(0, first.in_flight_before());
    ASSERT_EQ(1, stub->num_in_flight_rpcs());
    ASSERT_EQ(1024, stub->num_in_flight_payload_bytes());

    auto second = stub->reserve_rpc(512);
    ASSERT_EQ(1, second.in_flight_before());
    ASSERT_EQ(2, stub->num_in_flight_rpcs());
    ASSERT_EQ(1536, stub->num_in_flight_payload_bytes());

    auto moved = std::move(second);
    ASSERT_EQ(nullptr, second.stub());
    ASSERT_EQ(stub.get(), moved.stub());
    ASSERT_EQ(2, stub->num_in_flight_rpcs());

    moved.reset();
    ASSERT_EQ(1, stub->num_in_flight_rpcs());
    ASSERT_EQ(1024, stub->num_in_flight_payload_bytes());
    first.reset();
    ASSERT_EQ(0, stub->num_in_flight_rpcs());
    ASSERT_EQ(0, stub->num_in_flight_payload_bytes());
}
