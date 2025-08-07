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

#ifndef SRC_TRACING_TEST_MOCK_CONSUMER_H_
#define SRC_TRACING_TEST_MOCK_CONSUMER_H_

#include <memory>
#include <string_view>

#include "perfetto/ext/tracing/core/consumer.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/ext/tracing/core/tracing_service.h"
#include "perfetto/tracing/core/tracing_service_state.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/trace_packet.gen.h"

namespace perfetto {

namespace base {
class TestTaskRunner;
}

class MockConsumer : public Consumer {
 public:
  class FlushRequest {
   public:
    FlushRequest(std::function<bool(void)> wait_func) : wait_func_(wait_func) {}
    bool WaitForReply() { return wait_func_(); }

   private:
    std::function<bool(void)> wait_func_;
  };

  explicit MockConsumer(base::TestTaskRunner*);
  ~MockConsumer() override;

  void Connect(std::unique_ptr<TracingService::ConsumerEndpoint>);
  void Connect(TracingService* svc, uid_t = 0);
  void ForceDisconnect();
  void EnableTracing(const TraceConfig&, base::ScopedFile = base::ScopedFile());
  void StartTracing();
  void Detach(std::string key);
  void Attach(std::string key);
  void ChangeTraceConfig(const TraceConfig&);
  void DisableTracing();
  void FreeBuffers();
  void WaitForTracingDisabled(uint32_t timeout_ms = 3000) {
    return WaitForTracingDisabledWithError(testing::_, timeout_ms);
  }
  void WaitForTracingDisabledWithError(
      const testing::Matcher<const std::string&>& error_matcher,
      uint32_t timeout_ms = 3000);
  FlushRequest Flush(
      uint32_t timeout_ms = 10000,
      FlushFlags = FlushFlags(FlushFlags::Initiator::kConsumerSdk,
                              FlushFlags::Reason::kExplicit));
  std::vector<protos::gen::TracePacket> ReadBuffers();
  void GetTraceStats();
  TraceStats WaitForTraceStats(bool success);
  TracingServiceState QueryServiceState();
  void ObserveEvents(uint32_t enabled_event_types);
  ObservableEvents WaitForObservableEvents();
  void CloneSession(TracingSessionID);

  TracingService::ConsumerEndpoint* endpoint() {
    return service_endpoint_.get();
  }

  // Consumer implementation.
  MOCK_METHOD(void, OnConnect, (), (override));
  MOCK_METHOD(void, OnDisconnect, (), (override));
  MOCK_METHOD(void,
              OnTracingDisabled,
              (const std::string& /*error*/),
              (override));
  MOCK_METHOD(void,
              OnTraceData,
              (std::vector<TracePacket>* /*packets*/, bool /*has_more*/));
  MOCK_METHOD(void, OnDetach, (bool), (override));
  MOCK_METHOD(void, OnAttach, (bool, const TraceConfig&), (override));
  MOCK_METHOD(void, OnTraceStats, (bool, const TraceStats&), (override));
  MOCK_METHOD(void, OnObservableEvents, (const ObservableEvents&), (override));
  MOCK_METHOD(void, OnSessionCloned, (const OnSessionClonedArgs&), (override));

  // gtest doesn't support move-only types. This wrapper is here jut to pass
  // a pointer to the vector (rather than the vector itself) to the mock method.
  void OnTraceData(std::vector<TracePacket> packets, bool has_more) override {
    OnTraceData(&packets, has_more);
  }

 private:
  base::TestTaskRunner* const task_runner_;
  std::unique_ptr<TracingService::ConsumerEndpoint> service_endpoint_;
};

}  // namespace perfetto

#endif  // SRC_TRACING_TEST_MOCK_CONSUMER_H_
