/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACING_SERVICE_TRACING_SERVICE_ENDPOINTS_IMPL_H_
#define SRC_TRACING_SERVICE_TRACING_SERVICE_ENDPOINTS_IMPL_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "perfetto/ext/base/circular_queue.h"
#include "perfetto/ext/base/clock_snapshots.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/thread_checker.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/base/weak_runner.h"
#include "perfetto/ext/tracing/core/client_identity.h"
#include "perfetto/ext/tracing/core/observable_events.h"
#include "perfetto/ext/tracing/core/shared_memory_abi.h"
#include "perfetto/ext/tracing/core/tracing_service.h"
#include "perfetto/tracing/core/forward_decls.h"

// This header contains the declarations for the 3 abtract classes
// (ProducerEndpointImpl, ConsumerEndpointImpl, RelayEndpointImpl).
// These classes are the concrete implementation backed by TracingServiceImpl
// and override the base interfaces defined at the API level (other
// implementations exists in the various IPC layers to stub-out the calls).

namespace perfetto {

class SharedMemoryArbiterImpl;

namespace tracing_service {

class TracingServiceImpl;
struct DataSourceInstance;
struct TracingSession;
struct TriggerInfo;

// The implementation behind the service endpoint exposed to each producer.
class ProducerEndpointImpl : public TracingService::ProducerEndpoint {
 public:
  ProducerEndpointImpl(ProducerID,
                       const ClientIdentity& client_identity,
                       TracingServiceImpl*,
                       base::TaskRunner*,
                       Producer*,
                       const std::string& producer_name,
                       const std::string& machine_name,
                       const std::string& sdk_version,
                       bool in_process,
                       bool smb_scraping_enabled);
  ~ProducerEndpointImpl() override;

  // TracingService::ProducerEndpoint implementation.
  void Disconnect() override;
  void RegisterDataSource(const DataSourceDescriptor&) override;
  void UpdateDataSource(const DataSourceDescriptor&) override;
  void UnregisterDataSource(const std::string& name) override;
  void RegisterTraceWriter(uint32_t writer_id, uint32_t target_buffer) override;
  void UnregisterTraceWriter(uint32_t writer_id) override;
  void CommitData(const CommitDataRequest&, CommitDataCallback) override;
  void SetupSharedMemory(std::unique_ptr<SharedMemory>,
                         size_t page_size_bytes,
                         bool provided_by_producer,
                         SharedMemoryABI::ShmemMode shmem_mode);
  std::unique_ptr<TraceWriter> CreateTraceWriter(
      BufferID,
      BufferExhaustedPolicy) override;
  SharedMemoryArbiter* MaybeSharedMemoryArbiter() override;
  bool IsShmemProvidedByProducer() const override;
  void NotifyFlushComplete(FlushRequestID) override;
  void NotifyDataSourceStarted(DataSourceInstanceID) override;
  void NotifyDataSourceStopped(DataSourceInstanceID) override;
  SharedMemory* shared_memory() const override;
  size_t shared_buffer_page_size_kb() const override;
  void ActivateTriggers(const std::vector<std::string>&) override;
  void Sync(std::function<void()> callback) override;

  void OnTracingSetup();
  void SetupDataSource(DataSourceInstanceID, const DataSourceConfig&);
  void StartDataSource(DataSourceInstanceID, const DataSourceConfig&);
  void StopDataSource(DataSourceInstanceID);
  void Flush(FlushRequestID,
             const std::vector<DataSourceInstanceID>&,
             FlushFlags);
  void OnFreeBuffers(const std::vector<BufferID>& target_buffers);
  void ClearIncrementalState(const std::vector<DataSourceInstanceID>&);

  bool is_allowed_target_buffer(BufferID buffer_id) const {
    return allowed_target_buffers_.count(buffer_id);
  }

  std::optional<BufferID> buffer_id_for_writer(WriterID writer_id) const {
    const auto it = writers_.find(writer_id);
    if (it != writers_.end())
      return it->second;
    return std::nullopt;
  }

  bool IsShmemEmulated() { return shmem_abi_.use_shmem_emulation(); }

  bool IsAndroidProcessFrozen();
  uid_t uid() const { return client_identity_.uid(); }
  pid_t pid() const { return client_identity_.pid(); }
  const ClientIdentity& client_identity() const { return client_identity_; }

 private:
  friend class TracingServiceImpl;
  friend class ConsumerEndpointImpl;
  ProducerEndpointImpl(const ProducerEndpointImpl&) = delete;
  ProducerEndpointImpl& operator=(const ProducerEndpointImpl&) = delete;

  ProducerID const id_;
  ClientIdentity const client_identity_;
  TracingServiceImpl* const service_;
  Producer* producer_;
  std::unique_ptr<SharedMemory> shared_memory_;
  size_t shared_buffer_page_size_kb_ = 0;
  SharedMemoryABI shmem_abi_;
  size_t shmem_size_hint_bytes_ = 0;
  size_t shmem_page_size_hint_bytes_ = 0;
  bool is_shmem_provided_by_producer_ = false;
  const std::string name_;
  const std::string machine_name_;
  std::string sdk_version_;
  bool in_process_;
  bool smb_scraping_enabled_;

  // Set of the global target_buffer IDs that the producer is configured to
  // write into in any active tracing session.
  std::set<BufferID> allowed_target_buffers_;

  // Maps registered TraceWriter IDs to their target buffers as registered by
  // the producer. Note that producers aren't required to register their
  // writers, so we may see commits of chunks with WriterIDs that aren't
  // contained in this map. However, if a producer does register a writer, the
  // service will prevent the writer from writing into any other buffer than
  // the one associated with it here. The BufferIDs stored in this map are
  // untrusted, so need to be verified against |allowed_target_buffers_|
  // before use.
  std::map<WriterID, BufferID> writers_;

  // This is used only in in-process configurations.
  // SharedMemoryArbiterImpl methods themselves are thread-safe.
  std::unique_ptr<SharedMemoryArbiterImpl> inproc_shmem_arbiter_;

  PERFETTO_THREAD_CHECKER(thread_checker_)
  base::WeakRunner weak_runner_;
};

// The implementation behind the service endpoint exposed to each consumer.
class ConsumerEndpointImpl : public TracingService::ConsumerEndpoint {
 public:
  ConsumerEndpointImpl(TracingServiceImpl*,
                       base::TaskRunner*,
                       Consumer*,
                       uid_t uid);
  ~ConsumerEndpointImpl() override;

  void NotifyOnTracingDisabled(const std::string& error);

  // TracingService::ConsumerEndpoint implementation.
  void EnableTracing(const TraceConfig&, base::ScopedFile) override;
  void ChangeTraceConfig(const TraceConfig& cfg) override;
  void StartTracing() override;
  void DisableTracing() override;
  void ReadBuffers() override;
  void FreeBuffers() override;
  void Flush(uint32_t timeout_ms, FlushCallback, FlushFlags) override;
  void Detach(const std::string& key) override;
  void Attach(const std::string& key) override;
  void GetTraceStats() override;
  void ObserveEvents(uint32_t enabled_event_types) override;
  void QueryServiceState(QueryServiceStateArgs,
                         QueryServiceStateCallback) override;
  void QueryCapabilities(QueryCapabilitiesCallback) override;
  void SaveTraceForBugreport(SaveTraceForBugreportCallback) override;
  void CloneSession(CloneSessionArgs) override;

  // Will queue a task to notify the consumer about the state change.
  void OnDataSourceInstanceStateChange(const ProducerEndpointImpl&,
                                       const DataSourceInstance&);
  void OnAllDataSourcesStarted();

  base::WeakPtr<ConsumerEndpointImpl> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  friend class TracingServiceImpl;
  friend struct TracingSession;
  ConsumerEndpointImpl(const ConsumerEndpointImpl&) = delete;
  ConsumerEndpointImpl& operator=(const ConsumerEndpointImpl&) = delete;

  void NotifyCloneSnapshotTrigger(const TriggerInfo& trigger_name);

  // Returns a pointer to an ObservableEvents object that the caller can fill
  // and schedules a task to send the ObservableEvents to the consumer.
  ObservableEvents* AddObservableEvents();

  base::TaskRunner* const task_runner_;
  TracingServiceImpl* const service_;
  Consumer* const consumer_;
  uid_t const uid_;
  TracingSessionID tracing_session_id_ = 0;

  // Whether the consumer is interested in DataSourceInstance state change
  // events.
  uint32_t observable_events_mask_ = 0;

  // ObservableEvents that will be sent to the consumer. If set, a task to
  // flush the events to the consumer has been queued.
  std::unique_ptr<ObservableEvents> observable_events_;

  PERFETTO_THREAD_CHECKER(thread_checker_)
  base::WeakPtrFactory<ConsumerEndpointImpl> weak_ptr_factory_;  // Keep last.
};

class RelayEndpointImpl : public TracingService::RelayEndpoint {
 public:
  using SyncMode = RelayEndpoint::SyncMode;
  using RelayClientID = TracingService::RelayClientID;

  struct SyncedClockSnapshots {
    SyncedClockSnapshots(SyncMode _sync_mode,
                         base::ClockSnapshotVector _client_clocks,
                         base::ClockSnapshotVector _host_clocks)
        : sync_mode(_sync_mode),
          client_clocks(std::move(_client_clocks)),
          host_clocks(std::move(_host_clocks)) {}
    SyncMode sync_mode;
    base::ClockSnapshotVector client_clocks;
    base::ClockSnapshotVector host_clocks;
  };

  explicit RelayEndpointImpl(RelayClientID relay_client_id,
                             TracingServiceImpl* service);
  ~RelayEndpointImpl() override;

  void CacheSystemInfo(std::vector<uint8_t> serialized_system_info) override {
    serialized_system_info_ = serialized_system_info;
  }

  void SyncClocks(SyncMode sync_mode,
                  base::ClockSnapshotVector client_clocks,
                  base::ClockSnapshotVector host_clocks) override;
  void Disconnect() override;

  MachineID machine_id() const { return relay_client_id_.first; }

  base::CircularQueue<SyncedClockSnapshots>& synced_clocks() {
    return synced_clocks_;
  }

  std::vector<uint8_t>& serialized_system_info() {
    return serialized_system_info_;
  }

 private:
  friend class TracingServiceImpl;
  RelayEndpointImpl(const RelayEndpointImpl&) = delete;
  RelayEndpointImpl& operator=(const RelayEndpointImpl&) = delete;

  RelayClientID relay_client_id_;
  TracingServiceImpl* const service_;
  std::vector<uint8_t> serialized_system_info_;
  base::CircularQueue<SyncedClockSnapshots> synced_clocks_;

  PERFETTO_THREAD_CHECKER(thread_checker_)
};

}  // namespace tracing_service
}  // namespace perfetto
#endif  // SRC_TRACING_SERVICE_TRACING_SERVICE_ENDPOINTS_IMPL_H_
