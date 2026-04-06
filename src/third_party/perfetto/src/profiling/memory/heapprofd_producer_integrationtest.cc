/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/profiling/memory/heapprofd_producer.h"

#include "perfetto/base/proc_utils.h"
#include "perfetto/ext/base/thread_task_runner.h"
#include "perfetto/ext/tracing/ipc/consumer_ipc_client.h"
#include "perfetto/ext/tracing/ipc/service_ipc_host.h"
#include "protos/perfetto/common/data_source_descriptor.gen.h"
#include "protos/perfetto/config/trace_config.gen.h"
#include "src/base/test/test_task_runner.h"
#include "src/base/test/tmp_dir_tree.h"
#include "src/profiling/memory/client.h"
#include "src/profiling/memory/unhooked_allocator.h"
#include "src/tracing/test/mock_consumer.h"
#include "test/gtest_and_gmock.h"

namespace perfetto {
namespace profiling {
namespace {

using ::testing::NiceMock;
using ::testing::NotNull;

class TracingServiceThread {
 public:
  TracingServiceThread(const std::string& producer_socket,
                       const std::string& consumer_socket)
      : runner_(base::ThreadTaskRunner::CreateAndStart("perfetto.svc")),
        producer_socket_(producer_socket),
        consumer_socket_(consumer_socket) {
    runner_.PostTaskAndWaitForTesting([this]() {
      svc_ = ServiceIPCHost::CreateInstance(&runner_);
      bool res =
          svc_->Start(producer_socket_.c_str(), consumer_socket_.c_str());
      if (!res) {
        PERFETTO_FATAL("Failed to start service listening on %s and %s",
                       producer_socket_.c_str(), consumer_socket_.c_str());
      }
    });
  }

  ~TracingServiceThread() {
    runner_.PostTaskAndWaitForTesting([this]() { svc_.reset(); });
  }

  const std::string& producer_socket() const { return producer_socket_; }
  const std::string& consumer_socket() const { return consumer_socket_; }

 private:
  base::ThreadTaskRunner runner_;

  std::string producer_socket_;
  std::string consumer_socket_;
  std::unique_ptr<ServiceIPCHost> svc_;
};

class HeapprofdThread {
 public:
  HeapprofdThread(const std::string& producer_socket,
                  const std::string& heapprofd_socket)
      : runner_(base::ThreadTaskRunner::CreateAndStart("heapprofd.svc")),
        producer_socket_(producer_socket),
        heapprofd_socket_(heapprofd_socket) {
    runner_.PostTaskAndWaitForTesting([this]() {
      heapprofd_.reset(new HeapprofdProducer(HeapprofdMode::kCentral, &runner_,
                                             /* exit_when_done= */ false));

      heapprofd_->ConnectWithRetries(producer_socket_.c_str());
      listen_sock_ = base::UnixSocket::Listen(
          heapprofd_socket_.c_str(), &heapprofd_->socket_delegate(), &runner_,
          base::SockFamily::kUnix, base::SockType::kStream);
      EXPECT_THAT(listen_sock_, NotNull());
    });
  }

  void Sync() {
    runner_.PostTaskAndWaitForTesting([]() {});
  }

  ~HeapprofdThread() {
    runner_.PostTaskAndWaitForTesting([this]() {
      listen_sock_.reset();
      heapprofd_.reset();
    });
  }

  const std::string& producer_socket() const { return producer_socket_; }
  const std::string& heapprofd_socket() const { return heapprofd_socket_; }

 private:
  base::ThreadTaskRunner runner_;

  std::string producer_socket_;
  std::string heapprofd_socket_;
  std::unique_ptr<HeapprofdProducer> heapprofd_;
  std::unique_ptr<base::UnixSocket> listen_sock_;
};

class TraceConsumer {
 public:
  explicit TraceConsumer(base::TestTaskRunner* runner, std::string socket)
      : socket_(std::move(socket)), consumer_(runner) {
    consumer_.Connect(
        ConsumerIPCClient::Connect(socket_.c_str(), &consumer_, runner));
  }

  NiceMock<MockConsumer>& consumer() { return consumer_; }

 private:
  // consumer_ refers to socket_.
  const std::string socket_;
  NiceMock<MockConsumer> consumer_;
};

TraceConfig MakeTraceConfig() {
  TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(10 * 1024);

  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("android.heapprofd");
  ds_config->set_target_buffer(0);

  protos::gen::HeapprofdConfig heapprofd_config;
  heapprofd_config.set_sampling_interval_bytes(1);
  heapprofd_config.add_pid(static_cast<uint64_t>(base::GetProcessId()));
  heapprofd_config.set_all_heaps(true);
  heapprofd_config.set_no_startup(true);
  heapprofd_config.set_no_running(true);
  ds_config->set_heapprofd_config_raw(heapprofd_config.SerializeAsString());
  return trace_config;
}

bool WaitFor(std::function<bool()> predicate, long long timeout_ms = 40000) {
  long long deadline_ms = base::GetWallTimeMs().count() + timeout_ms;
  while (base::GetWallTimeMs().count() < deadline_ms) {
    if (predicate())
      return true;
    base::SleepMicroseconds(100 * 1000);  // 0.1 s.
  }
  return false;
}

bool WaitForDsRegistered(MockConsumer* mock_consumer,
                         const std::string& ds_name) {
  return WaitFor([mock_consumer, &ds_name]() {
    auto dss = mock_consumer->QueryServiceState().data_sources();
    return std::any_of(dss.begin(), dss.end(),
                       [&](const TracingServiceState::DataSource& ds) {
                         return ds.ds_descriptor().name() == ds_name;
                       });
  });
}

class HeapprofdProducerIntegrationTest : public testing::Test {
 protected:
  static constexpr char kProducerSock[] = "producer.sock";
  static constexpr char kConsumerSock[] = "consumer.sock";
  static constexpr char kHeapprofdSock[] = "heapprofd.sock";

  std::string ProducerSockPath() const {
    return tmpdir_.AbsolutePath(kProducerSock);
  }

  std::string ConsumerSockPath() const {
    return tmpdir_.AbsolutePath(kConsumerSock);
  }

  std::string HeapprofdSockPath() const {
    return tmpdir_.AbsolutePath(kHeapprofdSock);
  }

  void SetUp() override {
    tmpdir_.TrackFile(kProducerSock);
    tmpdir_.TrackFile(kConsumerSock);
    StartTracingService();

    tmpdir_.TrackFile(kHeapprofdSock);
    heapprofd_service_.emplace(ProducerSockPath(), HeapprofdSockPath());
  }

  void StartTracingService() {
    tracing_service_.emplace(ProducerSockPath(), ConsumerSockPath());
  }

  // Waits for the heapprofd data source to be registered and starts a trace
  // with it.
  std::unique_ptr<TraceConsumer> StartHeapprofdTrace(TraceConfig cfg) {
    auto trace_consumer =
        std::make_unique<TraceConsumer>(&task_runner_, ConsumerSockPath());

    if (WaitForDsRegistered(&trace_consumer->consumer(), "android.heapprofd") ==
        false) {
      ADD_FAILURE();
      return nullptr;
    }

    trace_consumer->consumer().ObserveEvents(
        ObservableEvents::TYPE_ALL_DATA_SOURCES_STARTED);
    trace_consumer->consumer().EnableTracing(cfg);
    trace_consumer->consumer().WaitForObservableEvents();

    return trace_consumer;
  }

  std::shared_ptr<Client> CreateHeapprofdClient() const {
    std::optional<base::UnixSocketRaw> client_sock =
        perfetto::profiling::Client::ConnectToHeapprofd(HeapprofdSockPath());
    if (!client_sock.has_value()) {
      return nullptr;
    }

    return perfetto::profiling::Client::CreateAndHandshake(
        std::move(client_sock.value()),
        UnhookedAllocator<perfetto::profiling::Client>(malloc, free));
  }

  base::TmpDirTree tmpdir_;
  base::TestTaskRunner task_runner_;
  std::optional<TracingServiceThread> tracing_service_;
  std::optional<HeapprofdThread> heapprofd_service_;
};

TEST_F(HeapprofdProducerIntegrationTest, Restart) {
  std::unique_ptr<TraceConsumer> consumer =
      StartHeapprofdTrace(MakeTraceConfig());
  ASSERT_THAT(consumer, NotNull());

  std::shared_ptr<Client> client = CreateHeapprofdClient();
  ASSERT_THAT(client, NotNull());

  // Shutdown tracing service. This should cause HeapprofdProducer::Restart() to
  // be executed on the heapprofd thread.
  tracing_service_.reset();
  // Wait for the effects of the tracing service disconnect to propagate to the
  // heapprofd thread.
  heapprofd_service_->Sync();

  consumer->consumer().ForceDisconnect();
  consumer.reset();

  task_runner_.RunUntilIdle();

  // Start tracing service again. Heapprofd should reconnect.
  ASSERT_EQ(remove(ProducerSockPath().c_str()), 0);
  ASSERT_EQ(remove(ConsumerSockPath().c_str()), 0);
  StartTracingService();

  consumer = StartHeapprofdTrace(MakeTraceConfig());
  ASSERT_THAT(consumer, NotNull());

  consumer->consumer().ForceDisconnect();
  consumer.reset();
}

}  // namespace
}  // namespace profiling
}  // namespace perfetto
