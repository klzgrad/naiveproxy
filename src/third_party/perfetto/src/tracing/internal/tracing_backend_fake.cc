/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "perfetto/tracing/internal/tracing_backend_fake.h"

#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/tracing/core/consumer.h"
#include "perfetto/ext/tracing/core/producer.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/ext/tracing/core/tracing_service.h"

namespace perfetto {
namespace internal {

namespace {

class UnsupportedProducerEndpoint : public ProducerEndpoint {
 public:
  UnsupportedProducerEndpoint(Producer* producer, base::TaskRunner* task_runner)
      : producer_(producer), task_runner_(task_runner) {
    // The SDK will attempt to reconnect the producer, so instead we allow it
    // to connect successfully, but never start any sessions.
    auto weak_ptr = weak_ptr_factory_.GetWeakPtr();
    task_runner_->PostTask([weak_ptr] {
      if (weak_ptr && weak_ptr->connected_)
        weak_ptr->producer_->OnConnect();
    });
  }
  ~UnsupportedProducerEndpoint() override { Disconnect(); }

  void Disconnect() override {
    if (!connected_)
      return;
    connected_ = false;
    producer_->OnDisconnect();
  }

  void RegisterDataSource(const DataSourceDescriptor&) override {}
  void UpdateDataSource(const DataSourceDescriptor&) override {}
  void UnregisterDataSource(const std::string& /*name*/) override {}

  void RegisterTraceWriter(uint32_t /*writer_id*/,
                           uint32_t /*target_buffer*/) override {}
  void UnregisterTraceWriter(uint32_t /*writer_id*/) override {}

  void CommitData(const CommitDataRequest&,
                  CommitDataCallback callback) override {
    if (connected_) {
      callback();
    }
  }

  SharedMemory* shared_memory() const override { return nullptr; }
  size_t shared_buffer_page_size_kb() const override { return 0; }

  std::unique_ptr<TraceWriter> CreateTraceWriter(
      BufferID /*target_buffer*/,
      BufferExhaustedPolicy) override {
    return nullptr;
  }

  SharedMemoryArbiter* MaybeSharedMemoryArbiter() override { return nullptr; }
  bool IsShmemProvidedByProducer() const override { return false; }

  void NotifyFlushComplete(FlushRequestID) override {}
  void NotifyDataSourceStarted(DataSourceInstanceID) override {}
  void NotifyDataSourceStopped(DataSourceInstanceID) override {}
  void ActivateTriggers(const std::vector<std::string>&) override {}

  void Sync(std::function<void()> callback) override {
    if (connected_) {
      callback();
    }
  }

 private:
  Producer* const producer_;
  base::TaskRunner* const task_runner_;
  bool connected_ = true;
  base::WeakPtrFactory<UnsupportedProducerEndpoint> weak_ptr_factory_{
      this};  // Keep last.
};

class UnsupportedConsumerEndpoint : public ConsumerEndpoint {
 public:
  UnsupportedConsumerEndpoint(Consumer* consumer, base::TaskRunner* task_runner)
      : consumer_(consumer), task_runner_(task_runner) {
    // The SDK will not to reconnect the consumer, so we just disconnect it
    // immediately, which will cancel the tracing session.
    auto weak_this = weak_ptr_factory_.GetWeakPtr();
    task_runner_->PostTask([weak_this] {
      if (weak_this)
        weak_this->consumer_->OnDisconnect();
    });
  }
  ~UnsupportedConsumerEndpoint() override = default;

  void EnableTracing(const TraceConfig&, base::ScopedFile) override {}
  void ChangeTraceConfig(const TraceConfig&) override {}

  void StartTracing() override {}
  void DisableTracing() override {}

  void Flush(uint32_t /*timeout_ms*/,
             FlushCallback callback,
             FlushFlags) override {
    callback(/*success=*/false);
  }

  void ReadBuffers() override {}
  void FreeBuffers() override {}

  void Detach(const std::string& /*key*/) override {}
  void Attach(const std::string& /*key*/) override {}

  void GetTraceStats() override {}
  void ObserveEvents(uint32_t /*events_mask*/) override {}
  void QueryServiceState(QueryServiceStateArgs,
                         QueryServiceStateCallback) override {}
  void QueryCapabilities(QueryCapabilitiesCallback) override {}

  void SaveTraceForBugreport(SaveTraceForBugreportCallback) override {}
  void CloneSession(CloneSessionArgs) override {}

 private:
  Consumer* const consumer_;
  base::TaskRunner* const task_runner_;
  base::WeakPtrFactory<UnsupportedConsumerEndpoint> weak_ptr_factory_{
      this};  // Keep last.
};

}  // namespace

// static
TracingBackend* TracingBackendFake::GetInstance() {
  static auto* instance = new TracingBackendFake();
  return instance;
}

TracingBackendFake::TracingBackendFake() = default;

std::unique_ptr<ProducerEndpoint> TracingBackendFake::ConnectProducer(
    const ConnectProducerArgs& args) {
  return std::unique_ptr<ProducerEndpoint>(
      new UnsupportedProducerEndpoint(args.producer, args.task_runner));
}

std::unique_ptr<ConsumerEndpoint> TracingBackendFake::ConnectConsumer(
    const ConnectConsumerArgs& args) {
  return std::unique_ptr<ConsumerEndpoint>(
      new UnsupportedConsumerEndpoint(args.consumer, args.task_runner));
}

}  // namespace internal
}  // namespace perfetto
