/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef SRC_TRACING_SERVICE_TRACING_SERVICE_IMPL_H_
#define SRC_TRACING_SERVICE_TRACING_SERVICE_IMPL_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/circular_queue.h"
#include "perfetto/ext/base/uuid.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/base/weak_runner.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/client_identity.h"
#include "perfetto/ext/tracing/core/commit_data_request.h"
#include "perfetto/ext/tracing/core/trace_stats.h"
#include "perfetto/ext/tracing/core/tracing_service.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "src/android_stats/perfetto_atoms.h"
#include "src/tracing/core/id_allocator.h"
#include "src/tracing/service/clock.h"
#include "src/tracing/service/dependencies.h"
#include "src/tracing/service/random.h"
#include "src/tracing/service/tracing_service_endpoints_impl.h"
#include "src/tracing/service/tracing_service_session.h"
#include "src/tracing/service/tracing_service_structs.h"

namespace protozero {
class MessageFilter;
}

namespace perfetto {

namespace protos {
namespace gen {
enum TraceStats_FinalFlushOutcome : int;
}
}  // namespace protos

class Consumer;
class Producer;
class SharedMemory;
class SharedMemoryArbiterImpl;
class TraceBuffer;
class TracePacket;

namespace tracing_service {
// The tracing service business logic.
class TracingServiceImpl : public TracingService {
 public:
  static constexpr size_t kMaxShmSize = 32 * 1024 * 1024ul;
  static constexpr uint8_t kSyncMarker[] = {0x82, 0x47, 0x7a, 0x76, 0xb2, 0x8d,
                                            0x42, 0xba, 0x81, 0xdc, 0x33, 0x32,
                                            0x6d, 0x57, 0xa0, 0x79};
  static constexpr size_t kMaxTracePacketSliceSize =
      128 * 1024 - 512;  // This is ipc::kIPCBufferSize - 512, see assertion in
                         // tracing_integration_test.cc and b/195065199

  // This is a rough threshold to determine how many bytes to read from the
  // buffers on each iteration when writing into a file. Since filtering and
  // compression allocate memory, this effectively limits the amount of memory
  // allocated.
  static constexpr size_t kWriteIntoFileChunkSize = 1024 * 1024ul;

  explicit TracingServiceImpl(std::unique_ptr<SharedMemory::Factory>,
                              base::TaskRunner*,
                              tracing_service::Dependencies,
                              InitOpts = {});
  ~TracingServiceImpl() override;

  // Called by ProducerEndpointImpl.
  void DisconnectProducer(ProducerID);
  void RegisterDataSource(ProducerID, const DataSourceDescriptor&);
  void UpdateDataSource(ProducerID, const DataSourceDescriptor&);
  void UnregisterDataSource(ProducerID, const std::string& name);
  void CopyProducerPageIntoLogBuffer(ProducerID,
                                     const ClientIdentity&,
                                     WriterID,
                                     ChunkID,
                                     BufferID,
                                     uint16_t num_fragments,
                                     uint8_t chunk_flags,
                                     bool chunk_complete,
                                     const uint8_t* src,
                                     size_t size);
  void ApplyChunkPatches(ProducerID,
                         const std::vector<CommitDataRequest::ChunkToPatch>&);
  void NotifyFlushDoneForProducer(ProducerID, FlushRequestID);
  void NotifyDataSourceStarted(ProducerID, DataSourceInstanceID);
  void NotifyDataSourceStopped(ProducerID, DataSourceInstanceID);
  void ActivateTriggers(ProducerID, const std::vector<std::string>& triggers);

  // Called by ConsumerEndpointImpl.
  bool DetachConsumer(ConsumerEndpointImpl*, const std::string& key);
  bool AttachConsumer(ConsumerEndpointImpl*, const std::string& key);
  void DisconnectConsumer(ConsumerEndpointImpl*);
  base::Status EnableTracing(ConsumerEndpointImpl*,
                             const TraceConfig&,
                             base::ScopedFile);
  void ChangeTraceConfig(ConsumerEndpointImpl*, const TraceConfig&);

  void StartTracing(TracingSessionID);
  void DisableTracing(TracingSessionID,
                      bool disable_immediately = false,
                      const std::string& error = {});
  void Flush(TracingSessionID tsid,
             uint32_t timeout_ms,
             ConsumerEndpoint::FlushCallback,
             FlushFlags);
  void FlushAndDisableTracing(TracingSessionID);
  base::Status FlushAndCloneSession(ConsumerEndpointImpl*,
                                    ConsumerEndpoint::CloneSessionArgs);

  // Starts reading the internal tracing buffers from the tracing session `tsid`
  // and sends them to `*consumer` (which must be != nullptr).
  //
  // Only reads a limited amount of data in one call. If there's more data,
  // immediately schedules itself on a PostTask.
  //
  // Returns false in case of error.
  bool ReadBuffersIntoConsumer(TracingSessionID tsid,
                               ConsumerEndpointImpl* consumer);

  // Reads all the tracing buffers from the tracing session `tsid` and writes
  // them into the associated file.
  // If `async_flush_buffers_before_read` is `true` this function becomes
  // asynchronous: immediately posts a `Flush` task and returns. Reads the
  // buffers when the flush is done, inside the `FlushCallback`.
  //
  // Reads all the data in the buffers (or until the file is full) before
  // returning.
  //
  // If the tracing session write_period_ms is 0, the file is full or there has
  // been an error, flushes the file and closes it. Otherwise, schedules itself
  // to be executed after write_period_ms.
  //
  // Returns false in case of error.
  bool ReadBuffersIntoFile(TracingSessionID tsid,
                           bool async_flush_buffers_before_read);

  void FreeBuffers(TracingSessionID tsid, const std::string& error = {});

  // Service implementation.
  std::unique_ptr<TracingService::ProducerEndpoint> ConnectProducer(
      Producer*,
      const ClientIdentity& client_identity,
      const std::string& producer_name,
      size_t shared_memory_size_hint_bytes = 0,
      bool in_process = false,
      ProducerSMBScrapingMode smb_scraping_mode =
          ProducerSMBScrapingMode::kDefault,
      size_t shared_memory_page_size_hint_bytes = 0,
      std::unique_ptr<SharedMemory> shm = nullptr,
      const std::string& sdk_version = {},
      const std::string& machine_name = {}) override;

  std::unique_ptr<TracingService::ConsumerEndpoint> ConnectConsumer(
      Consumer*,
      uid_t) override;

  std::unique_ptr<TracingService::RelayEndpoint> ConnectRelayClient(
      RelayClientID) override;

  void DisconnectRelayClient(RelayClientID);

  // Set whether SMB scraping should be enabled by default or not. Producers can
  // override this setting for their own SMBs.
  void SetSMBScrapingEnabled(bool enabled) override {
    smb_scraping_enabled_ = enabled;
  }

  // Exposed mainly for testing.
  size_t num_producers() const { return producers_.size(); }
  ProducerEndpointImpl* GetProducer(ProducerID) const;

 private:
  friend class ProducerEndpointImpl;
  friend class ConsumerEndpointImpl;

  TracingServiceImpl(const TracingServiceImpl&) = delete;
  TracingServiceImpl& operator=(const TracingServiceImpl&) = delete;

  bool IsInitiatorPrivileged(const TracingSession&);

  DataSourceInstance* SetupDataSource(const TraceConfig::DataSource&,
                                      const TraceConfig::ProducerConfig&,
                                      const RegisteredDataSource&,
                                      TracingSession*);

  void MaybeSetUpProtoVm(const DataSourceConfig&,
                         const RegisteredDataSource&,
                         BufferID);

  // Returns the next available ProducerID that is not in |producers_|.
  ProducerID GetNextProducerID();

  // Returns a pointer to the |tracing_sessions_| entry or nullptr if the
  // session doesn't exists.
  TracingSession* GetTracingSession(TracingSessionID);

  // Returns a pointer to the |tracing_sessions_| entry with
  // |unique_session_name| in the config (or nullptr if the
  // session doesn't exists). CLONED_READ_ONLY sessions are ignored.
  TracingSession* GetTracingSessionByUniqueName(
      const std::string& unique_session_name);

  // Returns a pointer to the tracing session that has the highest
  // TraceConfig.bugreport_score, if any, or nullptr.
  TracingSession* FindTracingSessionWithMaxBugreportScore();

  // Returns a pointer to the |tracing_sessions_| entry, matching the given
  // uid and detach key, or nullptr if no such session exists.
  TracingSession* GetDetachedSession(uid_t, const std::string& key);

  // Update the memory guard rail by using the latest information from the
  // shared memory and trace buffers.
  void UpdateMemoryGuardrail();

  uint32_t DelayToNextWritePeriodMs(const TracingSession&);
  void StartDataSourceInstance(ProducerEndpointImpl*,
                               TracingSession*,
                               DataSourceInstance*);
  void StopDataSourceInstance(ProducerEndpointImpl*,
                              TracingSession*,
                              DataSourceInstance*,
                              bool disable_immediately);
  void PeriodicSnapshotTask(TracingSessionID);
  void MaybeSnapshotClocksIntoRingBuffer(TracingSession*);
  bool SnapshotClocks(TracingSession::ClockSnapshotData*);
  // Records a lifecycle event of type |field_id| with the current timestamp.
  void SnapshotLifecycleEvent(TracingSession*,
                              uint32_t field_id,
                              bool snapshot_clocks);
  // Deletes all the lifecycle events of type |field_id| and records just one,
  // that happened at time |boot_time_ns|.
  void SetSingleLifecycleEvent(TracingSession*,
                               uint32_t field_id,
                               int64_t boot_time_ns);
  void EmitClockSnapshot(TracingSession*,
                         TracingSession::ClockSnapshotData,
                         std::vector<TracePacket>*);
  void EmitSyncMarker(std::vector<TracePacket>*);
  void EmitStats(TracingSession*, std::vector<TracePacket>*);
  TraceStats GetTraceStats(TracingSession*);
  void EmitLifecycleEvents(TracingSession*, std::vector<TracePacket>*);
  void EmitUuid(TracingSession*, std::vector<TracePacket>*);
  void MaybeEmitTraceConfig(TracingSession*, std::vector<TracePacket>*);
  void EmitSystemInfo(std::vector<TracePacket>*);
  void EmitTraceProvenance(TracingSession*, std::vector<TracePacket>*);
  void MaybeEmitRemoteSystemInfo(std::vector<TracePacket>*);
  void MaybeEmitCloneTrigger(TracingSession*, std::vector<TracePacket>*);
  void MaybeEmitReceivedTriggers(TracingSession*, std::vector<TracePacket>*);
  void MaybeEmitRemoteClockSync(TracingSession*, std::vector<TracePacket>*);
  void MaybeEmitProtoVmInstances(TracingSession*, std::vector<TracePacket>*);
  void EmitExtensionDescriptors(TracingSession*, std::vector<TracePacket>*);
  void MaybeNotifyAllDataSourcesStarted(TracingSession*);
  void OnFlushTimeout(TracingSessionID, FlushRequestID, FlushFlags);
  void OnDisableTracingTimeout(TracingSessionID);
  void OnAllDataSourceStartedTimeout(TracingSessionID);
  void DisableTracingNotifyConsumerAndFlushFile(TracingSession*,
                                                const std::string& error = {});
  void PeriodicFlushTask(TracingSessionID, bool post_next_only);
  void CompleteFlush(TracingSessionID tsid,
                     ConsumerEndpoint::FlushCallback callback,
                     bool success);
  void ScrapeSharedMemoryBuffers(TracingSession*, ProducerEndpointImpl*);
  void PeriodicClearIncrementalStateTask(TracingSessionID, bool post_next_only);
  TraceBuffer* GetBufferByID(BufferID);
  void FlushDataSourceInstances(
      TracingSession*,
      uint32_t timeout_ms,
      const std::map<ProducerID, std::vector<DataSourceInstanceID>>&,
      ConsumerEndpoint::FlushCallback,
      FlushFlags);
  std::map<ProducerID, std::vector<DataSourceInstanceID>>
  GetFlushableDataSourceInstancesForBuffers(TracingSession*,
                                            const std::set<BufferID>&);
  bool DoCloneBuffers(const TracingSession&,
                      const std::set<BufferID>&,
                      PendingClone*);
  base::Status FinishCloneSession(ConsumerEndpointImpl*,
                                  TracingSessionID,
                                  std::vector<std::unique_ptr<TraceBuffer>>,
                                  std::vector<int64_t> buf_cloned_timestamps,
                                  bool skip_filter,
                                  bool final_flush_outcome,
                                  std::optional<TriggerInfo> clone_trigger,
                                  base::Uuid*,
                                  int64_t clone_started_timestamp_ns,
                                  base::ScopedFile output_file_fd);
  void OnFlushDoneForClone(TracingSessionID src_tsid,
                           PendingCloneID clone_id,
                           const std::set<BufferID>& buf_ids,
                           bool final_flush_outcome);

  // Returns true if `*tracing_session` is waiting for a trigger that hasn't
  // happened.
  static bool IsWaitingForTrigger(TracingSession* tracing_session);

  // Reads the buffers from `*tracing_session` and returns them (along with some
  // metadata packets).
  //
  // The function stops when the cumulative size of the return packets exceeds
  // `threshold` (so it's not a strict upper bound) and sets `*has_more` to
  // true, or when there are no more packets (and sets `*has_more` to false).
  std::vector<TracePacket> ReadBuffers(TracingSession* tracing_session,
                                       size_t threshold,
                                       bool* has_more);

  // If `*tracing_session` has a filter, applies it to `*packets`. Doesn't
  // change the number of `*packets`, only their content.
  void MaybeFilterPackets(TracingSession* tracing_session,
                          std::vector<TracePacket>* packets);

  // If `*tracing_session` has compression enabled, compress `*packets`.
  void MaybeCompressPackets(TracingSession* tracing_session,
                            std::vector<TracePacket>* packets);

  // If `*tracing_session` is configured to write into a file, writes `packets`
  // into the file.
  //
  // Returns true if the file should be closed (because it's full or there has
  // been an error), false otherwise.
  bool WriteIntoFile(TracingSession* tracing_session,
                     std::vector<TracePacket> packets);
  void OnStartTriggersTimeout(TracingSessionID tsid);
  void MaybeLogUploadEvent(const TraceConfig&,
                           const base::Uuid&,
                           PerfettoStatsdAtom atom,
                           const std::string& trigger_name = "");
  void MaybeLogTriggerEvent(const TraceConfig&,
                            const base::Uuid&,
                            PerfettoTriggerAtom atom,
                            const std::string& trigger_name);
  size_t PurgeExpiredAndCountTriggerInWindow(int64_t now_ns,
                                             uint64_t trigger_name_hash);
  void StopOnDurationMsExpiry(TracingSessionID);

  std::unique_ptr<tracing_service::Clock> clock_;
  std::unique_ptr<tracing_service::Random> random_;
  const InitOpts init_opts_;
  std::unique_ptr<SharedMemory::Factory> shm_factory_;
  ProducerID last_producer_id_ = 0;
  DataSourceInstanceID last_data_source_instance_id_ = 0;
  TracingSessionID last_tracing_session_id_ = 0;
  FlushRequestID last_flush_request_id_ = 0;
  uid_t uid_ = 0;

  // Buffer IDs are global across all consumers (because a Producer can produce
  // data for more than one trace session, hence more than one consumer).
  IdAllocator<BufferID> buffer_ids_;

  std::multimap<std::string /*name*/, RegisteredDataSource> data_sources_;
  std::map<ProducerID, ProducerEndpointImpl*> producers_;
  std::map<RelayClientID, RelayEndpointImpl*> relay_clients_;
  std::map<TracingSessionID, TracingSession> tracing_sessions_;
  std::map<BufferID, std::unique_ptr<TraceBuffer>> buffers_;
  std::map<std::string, int64_t> session_to_last_trace_s_;

  // Contains timestamps of triggers.
  // The queue is sorted by timestamp and invocations older than 24 hours are
  // purged when a trigger happens.
  base::CircularQueue<TriggerHistory> trigger_history_;

  bool smb_scraping_enabled_ = false;
  bool lockdown_mode_ = false;

  uint8_t sync_marker_packet_[32];  // Lazily initialized.
  size_t sync_marker_packet_size_ = 0;

  // Stats.
  uint64_t chunks_discarded_ = 0;
  uint64_t patches_discarded_ = 0;

  PERFETTO_THREAD_CHECKER(thread_checker_)

  base::WeakRunner weak_runner_;
};

}  // namespace tracing_service
}  // namespace perfetto

#endif  // SRC_TRACING_SERVICE_TRACING_SERVICE_IMPL_H_
