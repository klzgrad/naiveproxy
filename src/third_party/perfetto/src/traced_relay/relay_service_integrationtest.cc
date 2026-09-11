/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <memory>
#include <string>
#include <vector>

#include "perfetto/ext/base/unix_socket.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "src/traced_relay/relay_service.h"

#include "src/base/test/test_task_runner.h"
#include "test/gtest_and_gmock.h"
#include "test/test_helper.h"

#include "protos/perfetto/common/trace_stats.gen.h"
#include "protos/perfetto/config/test_config.gen.h"
#include "protos/perfetto/config/trace_config.gen.h"
#include "protos/perfetto/trace/remote_clock_sync.gen.h"
#include "protos/perfetto/trace/test_event.gen.h"

namespace perfetto {
namespace {

struct TestParams {
  std::string id;
  std::string tcp_sock_name;
  std::string unix_sock_name;
  std::string producer_name;

  std::unique_ptr<RelayService> relay_service;
  std::unique_ptr<base::UnixSocket> server_socket;
  std::unique_ptr<FakeProducerThread> producer_thread;
};

TEST(TracedRelayIntegrationTest, BasicCase) {
  base::TestTaskRunner task_runner;

  std::string sock_name;
  {
    // Set up a server UnixSocket to find an unused TCP port.
    base::UnixSocket::EventListener event_listener;
    auto srv = base::UnixSocket::Listen("127.0.0.1:0", &event_listener,
                                        &task_runner, base::SockFamily::kInet,
                                        base::SockType::kStream);
    ASSERT_TRUE(srv->is_listening());
    sock_name = srv->GetSockAddr();
    // Shut down |srv| here to free the port. It's unlikely that the port will
    // be taken by another process so quickly before we reach the code below.
  }

  TestHelper helper(&task_runner, TestHelper::Mode::kStartDaemons,
                    sock_name.c_str(), /*enable_relay_endpoint=*/true);
  ASSERT_EQ(helper.num_producers(), 1u);
  helper.StartServiceIfRequired();

  auto relay_service = std::make_unique<RelayService>(&task_runner);
  // Don't let RelayClient interfere with the testing of relayed producers.
  relay_service->SetRelayClientDisabledForTesting(true);

  relay_service->Start("@traced_relay", sock_name.c_str());

  auto producer_connected =
      task_runner.CreateCheckpoint("perfetto.FakeProducer.connected");
  auto noop = []() {};
  auto connected = [&]() { task_runner.PostTask(producer_connected); };

  // We won't use the built-in fake producer and will start our own.
  auto producer_thread = std::make_unique<FakeProducerThread>(
      "@traced_relay", connected, noop, noop, "perfetto.FakeProducer");
  producer_thread->Connect();
  task_runner.RunUntilCheckpoint("perfetto.FakeProducer.connected");

  helper.ConnectConsumer();
  helper.WaitForConsumerConnect();

  TraceConfig trace_config;
  trace_config.set_trace_all_machines(true);
  trace_config.add_buffers()->set_size_kb(1024);
  trace_config.set_duration_ms(200);

  static constexpr uint32_t kMsgSize = 1024;
  static constexpr uint32_t kRandomSeed = 42;
  // Enable the producer.
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("perfetto.FakeProducer");
  ds_config->set_target_buffer(0);
  ds_config->mutable_for_testing()->set_seed(kRandomSeed);
  ds_config->mutable_for_testing()->set_message_count(12);
  ds_config->mutable_for_testing()->set_message_size(kMsgSize);
  ds_config->mutable_for_testing()->set_send_batch_on_register(true);

  helper.StartTracing(trace_config);
  helper.WaitForTracingDisabled();

  helper.ReadData();
  helper.WaitForReadData();

  const auto& packets = helper.trace();
  ASSERT_EQ(packets.size(), 12u);

  // The producer is connected from this process. The relay service will inject
  // the SetPeerIdentity message using the pid and euid of the current process.
  auto pid = static_cast<int32_t>(getpid());
  auto uid = static_cast<int32_t>(geteuid());

  std::minstd_rand0 rnd_engine(kRandomSeed);
  for (const auto& packet : packets) {
    ASSERT_TRUE(packet.has_for_testing());
    ASSERT_EQ(packet.trusted_pid(), pid);
    ASSERT_EQ(packet.trusted_uid(), uid);
    ASSERT_EQ(packet.for_testing().seq_value(), rnd_engine());
    // The tracing service should emit non-default machine ID in trace packets.
    ASSERT_NE(packet.machine_id(), 0u);
  }
}

TEST(TracedRelayIntegrationTest, MachineID_MultiRelayService) {
  base::TestTaskRunner task_runner;
  std::vector<TestParams> test_params(2);

  base::UnixSocket::EventListener event_listener;
  for (size_t i = 0; i < test_params.size(); i++) {
    auto& param = test_params[i];
    param.id = std::to_string(i + 1);
    param.server_socket = base::UnixSocket::Listen(
        "127.0.0.1:0", &event_listener, &task_runner, base::SockFamily::kInet,
        base::SockType::kStream);
    ASSERT_TRUE(param.server_socket->is_listening());
    param.tcp_sock_name = param.server_socket->GetSockAddr();
    param.relay_service = std::make_unique<RelayService>(&task_runner);
    param.relay_service->SetMachineIdHintForTesting("test-machine-id-" +
                                                    param.id);
    param.unix_sock_name = std::string("@traced_relay_") + param.id;
    param.producer_name = std::string("perfetto.FakeProducer.") + param.id;
  }
  for (auto& param : test_params) {
    // Shut down listening sockets to free the port. It's unlikely that the port
    // will be taken by another process so quickly before we reach the code
    // below.
    param.server_socket = nullptr;
  }
  auto relay_sock_name =
      test_params[0].tcp_sock_name + "," + test_params[1].tcp_sock_name;

  for (auto& param : test_params) {
    param.relay_service->Start(param.unix_sock_name.c_str(),
                               param.tcp_sock_name.c_str());
    // Don't let RelayClient interfere with the testing of relayed producers.
    param.relay_service->SetRelayClientDisabledForTesting(true);
  }

  TestHelper helper(&task_runner, TestHelper::Mode::kStartDaemons,
                    relay_sock_name.c_str(), /*enable_relay_endpoint=*/true);
  ASSERT_EQ(helper.num_producers(), 2u);
  helper.StartServiceIfRequired();

  for (auto& param : test_params) {
    auto checkpoint_name = "perfetto.FakeProducer.connected." + param.id;
    auto producer_connected = task_runner.CreateCheckpoint(checkpoint_name);
    auto noop = []() {};
    auto connected = std::bind(
        [&](std::function<void()> checkpoint) {
          task_runner.PostTask(checkpoint);
        },
        producer_connected);
    // We won't use the built-in fake producer and will start our own.
    param.producer_thread = std::make_unique<FakeProducerThread>(
        param.unix_sock_name, connected, noop, noop, param.producer_name);
    param.producer_thread->Connect();
    task_runner.RunUntilCheckpoint(checkpoint_name);
  }

  helper.ConnectConsumer();
  helper.WaitForConsumerConnect();

  TraceConfig trace_config;
  trace_config.set_trace_all_machines(true);
  trace_config.add_buffers()->set_size_kb(1024);
  trace_config.set_duration_ms(200);

  static constexpr uint32_t kMsgSize = 1024;
  static constexpr uint32_t kRandomSeed = 42;

  // Enable the 1st producer.
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("perfetto.FakeProducer.1");
  ds_config->set_target_buffer(0);
  ds_config->mutable_for_testing()->set_message_count(12);
  ds_config->mutable_for_testing()->set_message_size(kMsgSize);
  ds_config->mutable_for_testing()->set_send_batch_on_register(true);
  // Enable the 2nd producer.
  ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("perfetto.FakeProducer.2");
  ds_config->set_target_buffer(0);
  ds_config->mutable_for_testing()->set_message_count(24);
  ds_config->mutable_for_testing()->set_message_size(kMsgSize);
  ds_config->mutable_for_testing()->set_send_batch_on_register(true);

  helper.StartTracing(trace_config);
  helper.WaitForTracingDisabled();

  helper.ReadData();
  helper.WaitForReadData();

  const auto& packets = helper.trace();
  ASSERT_EQ(packets.size(), 36u);

  // The producer is connected from this process. The relay service will inject
  // the SetPeerIdentity message using the pid and euid of the current process.
  auto pid = static_cast<int32_t>(getpid());
  auto uid = static_cast<int32_t>(geteuid());

  std::minstd_rand0 rnd_engine(kRandomSeed);
  std::map<uint32_t, size_t> packets_counts;  // machine ID => count.

  for (const auto& packet : packets) {
    ASSERT_TRUE(packet.has_for_testing());
    ASSERT_EQ(packet.trusted_pid(), pid);
    ASSERT_EQ(packet.trusted_uid(), uid);
    packets_counts[packet.machine_id()]++;
  }

  // Fake producer (1, 2) either gets machine ID (1, 2), or (2, 1), depending on
  // which on is seen by the tracing service first.
  ASSERT_EQ(packets_counts.size(), 2u);
  auto count_1 = packets_counts.begin()->second;
  auto count_2 = packets_counts.rbegin()->second;
  ASSERT_TRUE(count_1 == 12u || count_1 == 24u);
  ASSERT_EQ(count_1 + count_2, 36u);

  for (auto& param : test_params) {
    param.producer_thread = nullptr;
    param.relay_service = nullptr;
  }
}

TEST(TracedRelayIntegrationTest, RelayClient) {
  base::TestTaskRunner task_runner;

  std::string sock_name;
  {
    // Set up a server UnixSocket to find an unused TCP port.
    base::UnixSocket::EventListener event_listener;
    auto srv = base::UnixSocket::Listen("127.0.0.1:0", &event_listener,
                                        &task_runner, base::SockFamily::kInet,
                                        base::SockType::kStream);
    ASSERT_TRUE(srv->is_listening());
    sock_name = srv->GetSockAddr();
    // Shut down |srv| here to free the port. It's unlikely that the port will
    // be taken by another process so quickly before we reach the code below.
  }

  TestHelper helper(&task_runner, TestHelper::Mode::kStartDaemons,
                    sock_name.c_str(), /*enable_relay_endpoint=*/true);
  ASSERT_EQ(helper.num_producers(), 1u);
  helper.StartServiceIfRequired();

  auto relay_service = std::make_unique<RelayService>(&task_runner);
  // This also starts the RelayClient.
  relay_service->Start("@traced_relay", sock_name.c_str());
  ASSERT_TRUE(!!relay_service->relay_client_for_testing());

  auto producer_connected =
      task_runner.CreateCheckpoint("perfetto.FakeProducer.connected");
  auto noop = []() {};
  auto connected = [&]() { task_runner.PostTask(producer_connected); };

  // We won't use the built-in fake producer and will start our own.
  auto producer_thread = std::make_unique<FakeProducerThread>(
      "@traced_relay", connected, noop, noop, "perfetto.FakeProducer");
  producer_thread->Connect();
  task_runner.RunUntilCheckpoint("perfetto.FakeProducer.connected");

  helper.ConnectConsumer();
  helper.WaitForConsumerConnect();

  while (!relay_service->relay_client_for_testing()
              ->clock_synced_with_service_for_testing())
    task_runner.RunUntilIdle();

  TraceConfig trace_config;
  trace_config.set_trace_all_machines(true);
  trace_config.add_buffers()->set_size_kb(1024);
  trace_config.set_duration_ms(200);

  // Enable the producer.
  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("perfetto.FakeProducer");
  ds_config->set_target_buffer(0);

  helper.StartTracing(trace_config);
  helper.WaitForTracingDisabled();

  helper.ReadData();
  helper.WaitForReadData();

  const auto& packets = helper.full_trace();

  std::map<uint32_t, size_t> system_info_counts;  // machine ID => count.
  bool clock_sync_packet_seen = false;
  for (auto& packet : packets) {
    if (packet.has_system_info()) {
      system_info_counts[packet.machine_id()]++;
    } else if (packet.has_remote_clock_sync()) {
      clock_sync_packet_seen = true;

      auto& synced_clocks = packet.remote_clock_sync().synced_clocks();
      ASSERT_FALSE(synced_clocks.empty());
      for (auto& clock_offset : synced_clocks) {
        ASSERT_TRUE(clock_offset.has_client_clocks());
        ASSERT_TRUE(clock_offset.has_host_clocks());
      }
    }
  }
  ASSERT_EQ(system_info_counts.size(), 2u);
  ASSERT_EQ(system_info_counts.begin()->second, 1u);
  ASSERT_EQ(system_info_counts.rbegin()->second, 1u);
  ASSERT_TRUE(clock_sync_packet_seen);
}

// Drives the shmem-emulation commit path with enough volume that the
// producer's emulated SMB page indices exceed the (smaller) service-side SMB.
// Pre-#6051 this caused ~75% of chunks to be silently rejected.
TEST(TracedRelayIntegrationTest, HighVolumeProducer) {
  base::TestTaskRunner task_runner;

  std::string sock_name;
  {
    base::UnixSocket::EventListener event_listener;
    auto srv = base::UnixSocket::Listen("127.0.0.1:0", &event_listener,
                                        &task_runner, base::SockFamily::kInet,
                                        base::SockType::kStream);
    ASSERT_TRUE(srv->is_listening());
    sock_name = srv->GetSockAddr();
  }

  TestHelper helper(&task_runner, TestHelper::Mode::kStartDaemons,
                    sock_name.c_str(), /*enable_relay_endpoint=*/true);
  ASSERT_EQ(helper.num_producers(), 1u);
  helper.StartServiceIfRequired();

  auto producer_connected =
      task_runner.CreateCheckpoint("perfetto.FakeProducer.connected");
  auto producer_enabled =
      task_runner.CreateCheckpoint("perfetto.FakeProducer.enabled");
  auto noop = []() {};
  auto connected = [&]() { task_runner.PostTask(producer_connected); };
  auto on_enabled = [&]() { task_runner.PostTask(producer_enabled); };

  auto producer_thread = std::make_unique<FakeProducerThread>(
      sock_name, connected, noop, on_enabled, "perfetto.FakeProducer");
  producer_thread->runner()->PostTaskAndWaitForTesting([&] {
    producer_thread->producer()->set_buffer_exhausted_policy(
        BufferExhaustedPolicy::kDrop);
  });
  producer_thread->Connect();
  task_runner.RunUntilCheckpoint("perfetto.FakeProducer.connected");

  helper.ConnectConsumer();
  helper.WaitForConsumerConnect();

  // Sized to comfortably exceed the service-side SMB's 64-page count
  // (1000 * 1 KiB packets ≈ 250 chunks at default 4 KiB chunk size) while
  // still finishing well under the test timeout on slow debug builds.
  constexpr uint32_t kMessagesPerBatch = 100;
  constexpr uint32_t kIterations = 10;
  constexpr uint32_t kMessageSize = 1024;
  constexpr uint32_t kSeed = 42;

  TraceConfig trace_config;
  trace_config.set_trace_all_machines(true);
  auto* buf_cfg = trace_config.add_buffers();
  buf_cfg->set_size_kb(8 * 1024);
  buf_cfg->set_experimental_mode(TraceConfig::BufferConfig::TRACE_BUFFER_V2);

  auto* ds_config = trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("perfetto.FakeProducer");
  ds_config->set_target_buffer(0);
  ds_config->mutable_for_testing()->set_seed(kSeed);
  ds_config->mutable_for_testing()->set_message_count(kMessagesPerBatch);
  ds_config->mutable_for_testing()->set_message_size(kMessageSize);
  ds_config->mutable_for_testing()->set_send_batch_on_register(true);

  helper.StartTracing(trace_config);
  task_runner.RunUntilCheckpoint("perfetto.FakeProducer.enabled");

  for (uint32_t i = 1; i < kIterations; i++) {
    auto cp = "batch.done." + std::to_string(i);
    auto done = task_runner.CreateCheckpoint(cp);
    producer_thread->producer()->ProduceEventBatch(
        [&task_runner, done] { task_runner.PostTask(done); });
    task_runner.RunUntilCheckpoint(cp);
  }

  auto sync_done = task_runner.CreateCheckpoint("perfetto.FakeProducer.sync");
  producer_thread->producer()->Sync(
      [&task_runner, sync_done] { task_runner.PostTask(sync_done); });
  task_runner.RunUntilCheckpoint("perfetto.FakeProducer.sync");

  helper.FlushAndWait(5000);
  helper.DisableTracing();
  helper.WaitForTracingDisabled();
  helper.ReadData();
  helper.WaitForReadData();

  size_t packets_received = 0;
  for (const auto& packet : helper.trace()) {
    if (packet.has_for_testing())
      packets_received++;
  }
  EXPECT_EQ(packets_received, kIterations * kMessagesPerBatch);

  uint64_t trace_writer_packet_loss = 0;
  for (const auto& packet : helper.full_trace()) {
    if (!packet.has_trace_stats())
      continue;
    for (const auto& buf : packet.trace_stats().buffer_stats())
      trace_writer_packet_loss += buf.trace_writer_packet_loss();
  }
  EXPECT_EQ(trace_writer_packet_loss, 0u);
}

}  // namespace
}  // namespace perfetto
