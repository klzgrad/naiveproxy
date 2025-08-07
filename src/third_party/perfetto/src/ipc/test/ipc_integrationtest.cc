/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/ext/ipc/client.h"
#include "perfetto/ext/ipc/host.h"
#include "src/base/test/test_task_runner.h"
#include "src/ipc/test/test_socket.h"
#include "test/gtest_and_gmock.h"

#include "src/ipc/test/greeter_service.gen.h"
#include "src/ipc/test/greeter_service.ipc.h"

namespace ipc_test {
namespace {

using ::perfetto::ipc::AsyncResult;
using ::perfetto::ipc::Client;
using ::perfetto::ipc::Deferred;
using ::perfetto::ipc::Host;
using ::perfetto::ipc::Service;
using ::perfetto::ipc::ServiceProxy;
using ::testing::_;
using ::testing::Invoke;

using namespace ::ipc_test::gen;

::perfetto::ipc::TestSocket kTestSocket{"ipc_integrationtest"};

class MockEventListener : public ServiceProxy::EventListener {
 public:
  MOCK_METHOD(void, OnConnect, (), (override));
  MOCK_METHOD(void, OnDisconnect, (), (override));
};

class MockGreeterService : public ::ipc_test::gen::Greeter {
 public:
  MOCK_METHOD(void,
              OnSayHello,
              (const GreeterRequestMsg&, DeferredGreeterReplyMsg*));
  void SayHello(const GreeterRequestMsg& request,
                DeferredGreeterReplyMsg reply) override {
    OnSayHello(request, &reply);
  }

  MOCK_METHOD(void,
              OnWaveGoodbye,
              (const GreeterRequestMsg&, DeferredGreeterReplyMsg*));
  void WaveGoodbye(const GreeterRequestMsg& request,
                   DeferredGreeterReplyMsg reply) override {
    OnWaveGoodbye(request, &reply);
  }
};

class IPCIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override { kTestSocket.Destroy(); }
  void TearDown() override { kTestSocket.Destroy(); }

  perfetto::base::TestTaskRunner task_runner_;
  MockEventListener svc_proxy_events_;
};

TEST_F(IPCIntegrationTest, SayHelloWaveGoodbye) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)
  std::unique_ptr<Host> host = Host::CreateInstance_Fuchsia(&task_runner_);
#else
  std::unique_ptr<Host> host =
      Host::CreateInstance(kTestSocket.name(), &task_runner_);
#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)
  ASSERT_TRUE(host);

  MockGreeterService* svc = new MockGreeterService();
  ASSERT_TRUE(host->ExposeService(std::unique_ptr<Service>(svc)));

  auto on_connect = task_runner_.CreateCheckpoint("on_connect");
  EXPECT_CALL(svc_proxy_events_, OnConnect()).WillOnce(Invoke(on_connect));

#if PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)
  auto socket_pair = perfetto::base::UnixSocketRaw::CreatePairPosix(
      perfetto::base::SockFamily::kUnix, perfetto::base::SockType::kStream);

  std::unique_ptr<Client> cli = Client::CreateInstance(
      Client::ConnArgs(
          perfetto::base::ScopedSocketHandle(socket_pair.first.ReleaseFd())),
      &task_runner_);
  host->AdoptConnectedSocket_Fuchsia(
      perfetto::base::ScopedSocketHandle(socket_pair.second.ReleaseFd()),
      [](int) { return false; });
#else
  std::unique_ptr<Client> cli = Client::CreateInstance(
      {kTestSocket.name(), /*retry=*/false}, &task_runner_);
#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)
  std::unique_ptr<GreeterProxy> svc_proxy(new GreeterProxy(&svc_proxy_events_));
  cli->BindService(svc_proxy->GetWeakPtr());
  task_runner_.RunUntilCheckpoint("on_connect");

  {
    GreeterRequestMsg req;
    req.set_name("Mr Bojangles");
    auto on_reply = task_runner_.CreateCheckpoint("on_hello_reply");
    Deferred<GreeterReplyMsg> deferred_reply(
        [on_reply](AsyncResult<GreeterReplyMsg> reply) {
          ASSERT_TRUE(reply.success());
          ASSERT_FALSE(reply.has_more());
          ASSERT_EQ("Hello Mr Bojangles", reply->message());
          on_reply();
        });

    EXPECT_CALL(*svc, OnSayHello(_, _))
        .WillOnce(Invoke([](const GreeterRequestMsg& host_req,
                            Deferred<GreeterReplyMsg>* host_reply) {
          auto reply = AsyncResult<GreeterReplyMsg>::Create();
          reply->set_message("Hello " + host_req.name());
          host_reply->Resolve(std::move(reply));
        }));
    svc_proxy->SayHello(req, std::move(deferred_reply));
    task_runner_.RunUntilCheckpoint("on_hello_reply");
  }

  {
    GreeterRequestMsg req;
    req.set_name("Mrs Bojangles");
    auto on_reply = task_runner_.CreateCheckpoint("on_goodbye_reply");
    Deferred<GreeterReplyMsg> deferred_reply(
        [on_reply](AsyncResult<GreeterReplyMsg> reply) {
          ASSERT_TRUE(reply.success());
          ASSERT_FALSE(reply.has_more());
          ASSERT_EQ("Goodbye Mrs Bojangles", reply->message());
          on_reply();
        });

    EXPECT_CALL(*svc, OnWaveGoodbye(_, _))
        .WillOnce(Invoke([](const GreeterRequestMsg& host_req,
                            Deferred<GreeterReplyMsg>* host_reply) {
          auto reply = AsyncResult<GreeterReplyMsg>::Create();
          reply->set_message("Goodbye " + host_req.name());
          host_reply->Resolve(std::move(reply));
        }));
    svc_proxy->WaveGoodbye(req, std::move(deferred_reply));
    task_runner_.RunUntilCheckpoint("on_goodbye_reply");
  }
}

}  // namespace
}  // namespace ipc_test
