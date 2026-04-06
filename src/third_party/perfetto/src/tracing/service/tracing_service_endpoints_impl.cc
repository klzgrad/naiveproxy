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

#include "src/tracing/service/tracing_service_endpoints_impl.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/metatrace.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/version.h"
#include "perfetto/ext/tracing/core/consumer.h"
#include "perfetto/ext/tracing/core/producer.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/tracing_service_capabilities.h"
#include "perfetto/tracing/core/tracing_service_state.h"
#include "src/tracing/core/shared_memory_arbiter_impl.h"
#include "src/tracing/service/tracing_service_impl.h"
#include "src/tracing/service/tracing_service_structs.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"

namespace perfetto::tracing_service {

namespace {
// Partially encodes a CommitDataRequest in an int32 for the purposes of
// metatracing. Note that it encodes only the bottom 10 bits of the producer id
// (which is technically 16 bits wide).
//
// Format (by bit range):
// [   31 ][         30 ][             29:20 ][            19:10 ][        9:0]
// [unused][has flush id][num chunks to patch][num chunks to move][producer id]
int32_t EncodeCommitDataRequest(ProducerID producer_id,
                                const CommitDataRequest& req_untrusted) {
  uint32_t cmov = static_cast<uint32_t>(req_untrusted.chunks_to_move_size());
  uint32_t cpatch = static_cast<uint32_t>(req_untrusted.chunks_to_patch_size());
  uint32_t has_flush_id = req_untrusted.flush_request_id() != 0;

  uint32_t mask = (1 << 10) - 1;
  uint32_t acc = 0;
  acc |= has_flush_id << 30;
  acc |= (cpatch & mask) << 20;
  acc |= (cmov & mask) << 10;
  acc |= (producer_id & mask);
  return static_cast<int32_t>(acc);
}
}  // namespace

////////////////////////////////////////////////////////////////////////////////
// ConsumerEndpointImpl implementation
////////////////////////////////////////////////////////////////////////////////

ConsumerEndpointImpl::ConsumerEndpointImpl(TracingServiceImpl* service,
                                           base::TaskRunner* task_runner,
                                           Consumer* consumer,
                                           uid_t uid)
    : task_runner_(task_runner),
      service_(service),
      consumer_(consumer),
      uid_(uid),
      weak_ptr_factory_(this) {}

ConsumerEndpointImpl::~ConsumerEndpointImpl() {
  service_->DisconnectConsumer(this);
  consumer_->OnDisconnect();
}

void ConsumerEndpointImpl::NotifyOnTracingDisabled(const std::string& error) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  task_runner_->PostTask([weak_this = weak_ptr_factory_.GetWeakPtr(),
                          error /* deliberate copy */] {
    if (weak_this)
      weak_this->consumer_->OnTracingDisabled(error);
  });
}

void ConsumerEndpointImpl::EnableTracing(const TraceConfig& cfg,
                                         base::ScopedFile fd) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto status = service_->EnableTracing(this, cfg, std::move(fd));
  if (!status.ok())
    NotifyOnTracingDisabled(status.message());
}

void ConsumerEndpointImpl::ChangeTraceConfig(const TraceConfig& cfg) {
  if (!tracing_session_id_) {
    PERFETTO_LOG(
        "Consumer called ChangeTraceConfig() but tracing was "
        "not active");
    return;
  }
  service_->ChangeTraceConfig(this, cfg);
}

void ConsumerEndpointImpl::StartTracing() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!tracing_session_id_) {
    PERFETTO_LOG("Consumer called StartTracing() but tracing was not active");
    return;
  }
  service_->StartTracing(tracing_session_id_);
}

void ConsumerEndpointImpl::DisableTracing() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!tracing_session_id_) {
    PERFETTO_LOG("Consumer called DisableTracing() but tracing was not active");
    return;
  }
  service_->DisableTracing(tracing_session_id_);
}

void ConsumerEndpointImpl::ReadBuffers() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!tracing_session_id_) {
    PERFETTO_LOG("Consumer called ReadBuffers() but tracing was not active");
    consumer_->OnTraceData({}, /* has_more = */ false);
    return;
  }
  if (!service_->ReadBuffersIntoConsumer(tracing_session_id_, this)) {
    consumer_->OnTraceData({}, /* has_more = */ false);
  }
}

void ConsumerEndpointImpl::FreeBuffers() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!tracing_session_id_) {
    PERFETTO_LOG("Consumer called FreeBuffers() but tracing was not active");
    return;
  }
  service_->FreeBuffers(tracing_session_id_);
  tracing_session_id_ = 0;
}

void ConsumerEndpointImpl::Flush(uint32_t timeout_ms,
                                 FlushCallback callback,
                                 FlushFlags flush_flags) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!tracing_session_id_) {
    PERFETTO_LOG("Consumer called Flush() but tracing was not active");
    return;
  }
  service_->Flush(tracing_session_id_, timeout_ms, callback, flush_flags);
}

void ConsumerEndpointImpl::Detach(const std::string& key) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  bool success = service_->DetachConsumer(this, key);
  auto weak_this = weak_ptr_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this = std::move(weak_this), success] {
    if (weak_this)
      weak_this->consumer_->OnDetach(success);
  });
}

void ConsumerEndpointImpl::Attach(const std::string& key) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  bool success = service_->AttachConsumer(this, key);
  task_runner_->PostTask([weak_this = weak_ptr_factory_.GetWeakPtr(), success] {
    if (!weak_this)
      return;
    Consumer* consumer = weak_this->consumer_;
    TracingSession* session =
        weak_this->service_->GetTracingSession(weak_this->tracing_session_id_);
    if (!session) {
      consumer->OnAttach(false, TraceConfig());
      return;
    }
    consumer->OnAttach(success, session->config);
  });
}

void ConsumerEndpointImpl::GetTraceStats() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  bool success = false;
  TraceStats stats;
  TracingSession* session = service_->GetTracingSession(tracing_session_id_);
  if (session) {
    success = true;
    stats = service_->GetTraceStats(session);
  }
  auto weak_this = weak_ptr_factory_.GetWeakPtr();
  task_runner_->PostTask(
      [weak_this = std::move(weak_this), success, stats = std::move(stats)] {
        if (weak_this)
          weak_this->consumer_->OnTraceStats(success, stats);
      });
}

void ConsumerEndpointImpl::ObserveEvents(uint32_t events_mask) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  observable_events_mask_ = events_mask;
  TracingSession* session = service_->GetTracingSession(tracing_session_id_);
  if (!session)
    return;

  if (observable_events_mask_ & ObservableEvents::TYPE_DATA_SOURCES_INSTANCES) {
    // Issue initial states.
    for (const auto& kv : session->data_source_instances) {
      ProducerEndpointImpl* producer = service_->GetProducer(kv.first);
      PERFETTO_DCHECK(producer);
      OnDataSourceInstanceStateChange(*producer, kv.second);
    }
  }

  // If the ObserveEvents() call happens after data sources have acked already
  // notify immediately.
  if (observable_events_mask_ &
      ObservableEvents::TYPE_ALL_DATA_SOURCES_STARTED) {
    service_->MaybeNotifyAllDataSourcesStarted(session);
  }
}

void ConsumerEndpointImpl::OnDataSourceInstanceStateChange(
    const ProducerEndpointImpl& producer,
    const DataSourceInstance& instance) {
  if (!(observable_events_mask_ &
        ObservableEvents::TYPE_DATA_SOURCES_INSTANCES)) {
    return;
  }

  if (instance.state != DataSourceInstance::CONFIGURED &&
      instance.state != DataSourceInstance::STARTED &&
      instance.state != DataSourceInstance::STOPPED) {
    return;
  }

  auto* observable_events = AddObservableEvents();
  auto* change = observable_events->add_instance_state_changes();
  change->set_producer_name(producer.name_);
  change->set_data_source_name(instance.data_source_name);
  if (instance.state == DataSourceInstance::STARTED) {
    change->set_state(ObservableEvents::DATA_SOURCE_INSTANCE_STATE_STARTED);
  } else {
    change->set_state(ObservableEvents::DATA_SOURCE_INSTANCE_STATE_STOPPED);
  }
}

void ConsumerEndpointImpl::OnAllDataSourcesStarted() {
  if (!(observable_events_mask_ &
        ObservableEvents::TYPE_ALL_DATA_SOURCES_STARTED)) {
    return;
  }
  auto* observable_events = AddObservableEvents();
  observable_events->set_all_data_sources_started(true);
}

void ConsumerEndpointImpl::NotifyCloneSnapshotTrigger(
    const TriggerInfo& trigger) {
  if (!(observable_events_mask_ & ObservableEvents::TYPE_CLONE_TRIGGER_HIT)) {
    return;
  }
  auto* observable_events = AddObservableEvents();
  auto* clone_trig = observable_events->mutable_clone_trigger_hit();
  clone_trig->set_tracing_session_id(static_cast<int64_t>(tracing_session_id_));
  clone_trig->set_trigger_name(trigger.trigger_name);
  clone_trig->set_producer_name(trigger.producer_name);
  clone_trig->set_producer_uid(static_cast<uint32_t>(trigger.producer_uid));
  clone_trig->set_boot_time_ns(trigger.boot_time_ns);
  clone_trig->set_trigger_delay_ms(trigger.trigger_delay_ms);
}

ObservableEvents* ConsumerEndpointImpl::AddObservableEvents() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!observable_events_) {
    observable_events_.reset(new ObservableEvents());
    task_runner_->PostTask([weak_this = weak_ptr_factory_.GetWeakPtr()] {
      if (!weak_this)
        return;

      // Move into a temporary to allow reentrancy in OnObservableEvents.
      auto observable_events = std::move(weak_this->observable_events_);
      weak_this->consumer_->OnObservableEvents(*observable_events);
    });
  }
  return observable_events_.get();
}

void ConsumerEndpointImpl::QueryServiceState(
    QueryServiceStateArgs args,
    QueryServiceStateCallback callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingServiceState svc_state;

  const auto& sessions = service_->tracing_sessions_;
  svc_state.set_tracing_service_version(base::GetVersionString());
  svc_state.set_num_sessions(static_cast<int>(sessions.size()));

  int num_started = 0;
  for (const auto& kv : sessions)
    num_started += kv.second.state == TracingSession::State::STARTED ? 1 : 0;
  svc_state.set_num_sessions_started(num_started);

  for (const auto& kv : service_->producers_) {
    if (args.sessions_only)
      break;
    auto* producer = svc_state.add_producers();
    producer->set_id(static_cast<int>(kv.first));
    producer->set_name(kv.second->name_);
    producer->set_sdk_version(kv.second->sdk_version_);
    producer->set_uid(static_cast<int32_t>(kv.second->uid()));
    producer->set_pid(static_cast<int32_t>(kv.second->pid()));
    producer->set_frozen(kv.second->IsAndroidProcessFrozen());
  }

  for (const auto& kv : service_->data_sources_) {
    if (args.sessions_only)
      break;
    const auto& registered_data_source = kv.second;
    auto* data_source = svc_state.add_data_sources();
    *data_source->mutable_ds_descriptor() = registered_data_source.descriptor;
    data_source->set_producer_id(
        static_cast<int>(registered_data_source.producer_id));
  }

  svc_state.set_supports_tracing_sessions(true);
  for (const auto& kv : service_->tracing_sessions_) {
    const TracingSession& s = kv.second;
    if (!s.IsCloneAllowed(uid_))
      continue;
    auto* session = svc_state.add_tracing_sessions();
    session->set_id(s.id);
    session->set_consumer_uid(static_cast<int>(s.consumer_uid));
    session->set_duration_ms(s.config.duration_ms());
    session->set_num_data_sources(
        static_cast<uint32_t>(s.data_source_instances.size()));
    session->set_unique_session_name(s.config.unique_session_name());
    if (s.config.has_bugreport_score())
      session->set_bugreport_score(s.config.bugreport_score());
    if (s.config.has_bugreport_filename())
      session->set_bugreport_filename(s.config.bugreport_filename());
    for (const auto& snap_kv : s.initial_clock_snapshot) {
      if (snap_kv.clock_id == protos::pbzero::BUILTIN_CLOCK_REALTIME)
        session->set_start_realtime_ns(static_cast<int64_t>(snap_kv.timestamp));
    }
    for (const auto& buf : s.config.buffers())
      session->add_buffer_size_kb(buf.size_kb());

    switch (s.state) {
      case TracingSession::State::DISABLED:
        session->set_state("DISABLED");
        break;
      case TracingSession::State::CONFIGURED:
        session->set_state("CONFIGURED");
        break;
      case TracingSession::State::STARTED:
        session->set_is_started(true);
        session->set_state("STARTED");
        break;
      case TracingSession::State::DISABLING_WAITING_STOP_ACKS:
        session->set_state("STOP_WAIT");
        break;
      case TracingSession::State::CLONED_READ_ONLY:
        session->set_state("CLONED_READ_ONLY");
        break;
    }
  }
  callback(/*success=*/true, svc_state);
}

void ConsumerEndpointImpl::QueryCapabilities(
    QueryCapabilitiesCallback callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingServiceCapabilities caps;
  caps.set_has_query_capabilities(true);
  caps.set_has_trace_config_output_path(true);
  caps.set_has_clone_session(true);
  caps.add_observable_events(ObservableEvents::TYPE_DATA_SOURCES_INSTANCES);
  caps.add_observable_events(ObservableEvents::TYPE_ALL_DATA_SOURCES_STARTED);
  caps.add_observable_events(ObservableEvents::TYPE_CLONE_TRIGGER_HIT);
  static_assert(
      ObservableEvents::Type_MAX == ObservableEvents::TYPE_CLONE_TRIGGER_HIT,
      "");
  callback(caps);
}

void ConsumerEndpointImpl::SaveTraceForBugreport(
    SaveTraceForBugreportCallback consumer_callback) {
  consumer_callback(false,
                    "SaveTraceForBugreport is deprecated. Use "
                    "CloneSession(kBugreportSessionId) instead.");
}

void ConsumerEndpointImpl::CloneSession(CloneSessionArgs args) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  // FlushAndCloneSession will call OnSessionCloned after the async flush.
  base::Status result = service_->FlushAndCloneSession(this, std::move(args));

  if (!result.ok()) {
    consumer_->OnSessionCloned({false, result.message(), {}, false});
  }
}

////////////////////////////////////////////////////////////////////////////////
// ProducerEndpointImpl implementation
////////////////////////////////////////////////////////////////////////////////

ProducerEndpointImpl::ProducerEndpointImpl(
    ProducerID id,
    const ClientIdentity& client_identity,
    TracingServiceImpl* service,
    base::TaskRunner* task_runner,
    Producer* producer,
    const std::string& producer_name,
    const std::string& machine_name,
    const std::string& sdk_version,
    bool in_process,
    bool smb_scraping_enabled)
    : id_(id),
      client_identity_(client_identity),
      service_(service),
      producer_(producer),
      name_(producer_name),
      machine_name_(machine_name),
      sdk_version_(sdk_version),
      in_process_(in_process),
      smb_scraping_enabled_(smb_scraping_enabled),
      weak_runner_(task_runner) {}

ProducerEndpointImpl::~ProducerEndpointImpl() {
  service_->DisconnectProducer(id_);
  producer_->OnDisconnect();
}

void ProducerEndpointImpl::Disconnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  // Disconnection is only supported via destroying the ProducerEndpoint.
  PERFETTO_FATAL("Not supported");
}

void ProducerEndpointImpl::RegisterDataSource(
    const DataSourceDescriptor& desc) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_->RegisterDataSource(id_, desc);
}

void ProducerEndpointImpl::UpdateDataSource(const DataSourceDescriptor& desc) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_->UpdateDataSource(id_, desc);
}

void ProducerEndpointImpl::UnregisterDataSource(const std::string& name) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_->UnregisterDataSource(id_, name);
}

void ProducerEndpointImpl::RegisterTraceWriter(uint32_t writer_id,
                                               uint32_t target_buffer) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  writers_[static_cast<WriterID>(writer_id)] =
      static_cast<BufferID>(target_buffer);
}

void ProducerEndpointImpl::UnregisterTraceWriter(uint32_t writer_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  writers_.erase(static_cast<WriterID>(writer_id));
}

void ProducerEndpointImpl::CommitData(const CommitDataRequest& req_untrusted,
                                      CommitDataCallback callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  if (metatrace::IsEnabled(metatrace::TAG_TRACE_SERVICE)) {
    PERFETTO_METATRACE_COUNTER(TAG_TRACE_SERVICE, TRACE_SERVICE_COMMIT_DATA,
                               EncodeCommitDataRequest(id_, req_untrusted));
  }

  if (!shared_memory_) {
    PERFETTO_DLOG(
        "Attempted to commit data before the shared memory was allocated.");
    return;
  }
  PERFETTO_DCHECK(shmem_abi_.is_valid());
  for (const auto& entry : req_untrusted.chunks_to_move()) {
    const uint32_t page_idx = entry.page();
    if (page_idx >= shmem_abi_.num_pages())
      continue;  // A buggy or malicious producer.

    SharedMemoryABI::Chunk chunk;
    bool commit_data_over_ipc = entry.has_data();
    bool chunk_complete = true;
    if (PERFETTO_UNLIKELY(commit_data_over_ipc)) {
      // Chunk data is passed over the wire. Create a chunk using the serialized
      // protobuf message.
      const std::string& data = entry.data();
      if (data.size() > SharedMemoryABI::Chunk::kMaxSize) {
        PERFETTO_DFATAL("IPC data commit too large: %zu", data.size());
        continue;  // A malicious or buggy producer
      }
      // |data| is not altered, but we need to const_cast becasue Chunk data
      // members are non-const.
      chunk = SharedMemoryABI::MakeChunkFromSerializedData(
          reinterpret_cast<uint8_t*>(const_cast<char*>(data.data())),
          static_cast<uint16_t>(entry.data().size()),
          static_cast<uint8_t>(entry.chunk()));
      chunk_complete = !entry.chunk_incomplete();
    } else
      chunk = shmem_abi_.TryAcquireChunkForReading(page_idx, entry.chunk());
    if (!chunk.is_valid()) {
      PERFETTO_DLOG("Asked to move chunk %u:%u, but it's not complete",
                    entry.page(), entry.chunk());
      continue;
    }

    // TryAcquireChunkForReading() has load-acquire semantics. Once acquired,
    // the ABI contract expects the producer to not touch the chunk anymore
    // (until the service marks that as free). This is why all the reads below
    // are just memory_order_relaxed. Also, the code here assumes that all this
    // data can be malicious and just gives up if anything is malformed.
    BufferID buffer_id = static_cast<BufferID>(entry.target_buffer());
    const SharedMemoryABI::ChunkHeader& chunk_header = *chunk.header();
    WriterID writer_id = chunk_header.writer_id.load(std::memory_order_relaxed);
    ChunkID chunk_id = chunk_header.chunk_id.load(std::memory_order_relaxed);
    auto packets = chunk_header.packets.load(std::memory_order_relaxed);
    uint16_t num_fragments = packets.count;
    uint8_t chunk_flags = packets.flags;

    service_->CopyProducerPageIntoLogBuffer(
        id_, client_identity_, writer_id, chunk_id, buffer_id, num_fragments,
        chunk_flags, chunk_complete, chunk.payload_begin(),
        chunk.payload_size());

    if (!commit_data_over_ipc) {
      // This one has release-store semantics.
      shmem_abi_.ReleaseChunkAsFree(std::move(chunk));
    }
  }  // for(chunks_to_move)

  service_->ApplyChunkPatches(id_, req_untrusted.chunks_to_patch());

  if (req_untrusted.flush_request_id()) {
    service_->NotifyFlushDoneForProducer(id_, req_untrusted.flush_request_id());
  }

  // Keep this invocation last. ProducerIPCService::CommitData() relies on this
  // callback being invoked within the same callstack and not posted. If this
  // changes, the code there needs to be changed accordingly.
  if (callback)
    callback();
}

void ProducerEndpointImpl::SetupSharedMemory(
    std::unique_ptr<SharedMemory> shared_memory,
    size_t page_size_bytes,
    bool provided_by_producer,
    SharedMemoryABI::ShmemMode shmem_mode) {
  PERFETTO_DCHECK(!shared_memory_ && !shmem_abi_.is_valid());
  PERFETTO_DCHECK(page_size_bytes % 1024 == 0);

  shared_memory_ = std::move(shared_memory);
  shared_buffer_page_size_kb_ = page_size_bytes / 1024;
  is_shmem_provided_by_producer_ = provided_by_producer;

  shmem_abi_.Initialize(reinterpret_cast<uint8_t*>(shared_memory_->start()),
                        shared_memory_->size(),
                        shared_buffer_page_size_kb() * 1024, shmem_mode);
  if (in_process_) {
    inproc_shmem_arbiter_.reset(new SharedMemoryArbiterImpl(
        shared_memory_->start(), shared_memory_->size(),
        SharedMemoryABI::ShmemMode::kDefault,
        shared_buffer_page_size_kb_ * 1024, this, weak_runner_.task_runner()));
    inproc_shmem_arbiter_->SetDirectSMBPatchingSupportedByService();
  }

  OnTracingSetup();
  service_->UpdateMemoryGuardrail();
}

SharedMemory* ProducerEndpointImpl::shared_memory() const {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  return shared_memory_.get();
}

size_t ProducerEndpointImpl::shared_buffer_page_size_kb() const {
  return shared_buffer_page_size_kb_;
}

void ProducerEndpointImpl::ActivateTriggers(
    const std::vector<std::string>& triggers) {
  service_->ActivateTriggers(id_, triggers);
}

void ProducerEndpointImpl::StopDataSource(DataSourceInstanceID ds_inst_id) {
  // TODO(primiano): When we'll support tearing down the SMB, at this point we
  // should send the Producer a TearDownTracing if all its data sources have
  // been disabled (see b/77532839 and aosp/655179 PS1).
  PERFETTO_DCHECK_THREAD(thread_checker_);
  weak_runner_.PostTask(
      [this, ds_inst_id] { producer_->StopDataSource(ds_inst_id); });
}

SharedMemoryArbiter* ProducerEndpointImpl::MaybeSharedMemoryArbiter() {
  if (!inproc_shmem_arbiter_) {
    PERFETTO_FATAL(
        "The in-process SharedMemoryArbiter can only be used when "
        "CreateProducer has been called with in_process=true and after tracing "
        "has started.");
  }

  PERFETTO_DCHECK(in_process_);
  return inproc_shmem_arbiter_.get();
}

bool ProducerEndpointImpl::IsShmemProvidedByProducer() const {
  return is_shmem_provided_by_producer_;
}

// Can be called on any thread.
std::unique_ptr<TraceWriter> ProducerEndpointImpl::CreateTraceWriter(
    BufferID buf_id,
    BufferExhaustedPolicy buffer_exhausted_policy) {
  PERFETTO_DCHECK(MaybeSharedMemoryArbiter());
  return MaybeSharedMemoryArbiter()->CreateTraceWriter(buf_id,
                                                       buffer_exhausted_policy);
}

void ProducerEndpointImpl::NotifyFlushComplete(FlushRequestID id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DCHECK(MaybeSharedMemoryArbiter());
  return MaybeSharedMemoryArbiter()->NotifyFlushComplete(id);
}

void ProducerEndpointImpl::OnTracingSetup() {
  weak_runner_.PostTask([this] { producer_->OnTracingSetup(); });
}

void ProducerEndpointImpl::Flush(
    FlushRequestID flush_request_id,
    const std::vector<DataSourceInstanceID>& data_sources,
    FlushFlags flush_flags) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  weak_runner_.PostTask([this, flush_request_id, data_sources, flush_flags] {
    producer_->Flush(flush_request_id, data_sources.data(), data_sources.size(),
                     flush_flags);
  });
}

void ProducerEndpointImpl::SetupDataSource(DataSourceInstanceID ds_id,
                                           const DataSourceConfig& config) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  allowed_target_buffers_.insert(static_cast<BufferID>(config.target_buffer()));
  weak_runner_.PostTask([this, ds_id, config] {
    producer_->SetupDataSource(ds_id, std::move(config));
  });
}

void ProducerEndpointImpl::StartDataSource(DataSourceInstanceID ds_id,
                                           const DataSourceConfig& config) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  weak_runner_.PostTask([this, ds_id, config] {
    producer_->StartDataSource(ds_id, std::move(config));
  });
}

void ProducerEndpointImpl::NotifyDataSourceStarted(
    DataSourceInstanceID data_source_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_->NotifyDataSourceStarted(id_, data_source_id);
}

void ProducerEndpointImpl::NotifyDataSourceStopped(
    DataSourceInstanceID data_source_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_->NotifyDataSourceStopped(id_, data_source_id);
}

void ProducerEndpointImpl::OnFreeBuffers(
    const std::vector<BufferID>& target_buffers) {
  if (allowed_target_buffers_.empty())
    return;
  for (BufferID buffer : target_buffers)
    allowed_target_buffers_.erase(buffer);
}

void ProducerEndpointImpl::ClearIncrementalState(
    const std::vector<DataSourceInstanceID>& data_sources) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  weak_runner_.PostTask([this, data_sources] {
    base::StringView producer_name(name_);
    producer_->ClearIncrementalState(data_sources.data(), data_sources.size());
  });
}

void ProducerEndpointImpl::Sync(std::function<void()> callback) {
  weak_runner_.task_runner()->PostTask(callback);
}

bool ProducerEndpointImpl::IsAndroidProcessFrozen() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  if (in_process_ || uid() == base::kInvalidUid || pid() == base::kInvalidPid)
    return false;

  // As per aosp/3406861, there are three possible mount points for the cgroup.
  // Look at all of them.
  // - Historically everything was in /uid_xxx/pid_yyy (and still is if
  //   PRODUCT_CGROUP_V2_SYS_APP_ISOLATION_ENABLED = false)
  // - cgroup isolation introduces /apps /system subdirectories.
  base::StackString<255> path_v1(
      "/sys/fs/cgroup/uid_%" PRIu32 "/pid_%" PRIu32 "/cgroup.freeze",
      static_cast<uint32_t>(uid()), static_cast<uint32_t>(pid()));
  base::StackString<255> path_v2_app(
      "/sys/fs/cgroup/apps/uid_%" PRIu32 "/pid_%" PRIu32 "/cgroup.freeze",
      static_cast<uint32_t>(uid()), static_cast<uint32_t>(pid()));
  base::StackString<255> path_v2_system(
      "/sys/fs/cgroup/system/uid_%" PRIu32 "/pid_%" PRIu32 "/cgroup.freeze",
      static_cast<uint32_t>(uid()), static_cast<uint32_t>(pid()));
  const char* paths[] = {path_v1.c_str(), path_v2_app.c_str(),
                         path_v2_system.c_str()};

  for (const char* path : paths) {
    char frozen = '0';
    auto fd = base::OpenFile(path, O_RDONLY);
    ssize_t rsize = 0;
    if (fd) {
      rsize = base::Read(*fd, &frozen, sizeof(frozen));
      if (rsize > 0) {
        return frozen == '1';
      }
    }
  }
  PERFETTO_DLOG("Failed to read cgroup.freeze from [%s, %s, %s]",
                path_v1.c_str(), path_v2_app.c_str(), path_v2_system.c_str());

#endif
  return false;
}

////////////////////////////////////////////////////////////////////////////////
// RelayEndpointImpl implementation
////////////////////////////////////////////////////////////////////////////////
RelayEndpointImpl::RelayEndpointImpl(RelayClientID relay_client_id,
                                     TracingServiceImpl* service)
    : relay_client_id_(relay_client_id),
      service_(service),
      serialized_system_info_({}) {}
RelayEndpointImpl::~RelayEndpointImpl() = default;

void RelayEndpointImpl::SyncClocks(SyncMode sync_mode,
                                   base::ClockSnapshotVector client_clocks,
                                   base::ClockSnapshotVector host_clocks) {
  // We keep only the most recent 5 clock sync snapshots.
  static constexpr size_t kNumSyncClocks = 5;
  if (synced_clocks_.size() >= kNumSyncClocks)
    synced_clocks_.pop_front();

  synced_clocks_.emplace_back(sync_mode, std::move(client_clocks),
                              std::move(host_clocks));
}

void RelayEndpointImpl::Disconnect() {
  service_->DisconnectRelayClient(relay_client_id_);
}
}  // namespace perfetto::tracing_service
