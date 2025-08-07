/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/tracing/internal/tracing_muxer_impl.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/thread_checker.h"
#include "perfetto/ext/base/waitable_event.h"
#include "perfetto/ext/tracing/core/shared_memory_arbiter.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/ext/tracing/core/trace_stats.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/ext/tracing/core/tracing_service.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/tracing_service_state.h"
#include "perfetto/tracing/data_source.h"
#include "perfetto/tracing/internal/data_source_internal.h"
#include "perfetto/tracing/internal/interceptor_trace_writer.h"
#include "perfetto/tracing/internal/tracing_backend_fake.h"
#include "perfetto/tracing/trace_writer_base.h"
#include "perfetto/tracing/tracing.h"
#include "perfetto/tracing/tracing_backend.h"
#include "src/tracing/core/null_trace_writer.h"
#include "src/tracing/internal/tracing_muxer_fake.h"

#include "protos/perfetto/config/interceptor_config.gen.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <io.h>  // For dup()
#else
#include <unistd.h>  // For dup()
#endif

namespace perfetto {
namespace internal {

namespace {

using RegisteredDataSource = TracingMuxerImpl::RegisteredDataSource;

// A task runner which prevents calls to DataSource::Trace() while an operation
// is in progress. Used to guard against unexpected re-entrancy where the
// user-provided task runner implementation tries to enter a trace point under
// the hood.
class NonReentrantTaskRunner : public base::TaskRunner {
 public:
  NonReentrantTaskRunner(TracingMuxer* muxer,
                         std::unique_ptr<base::TaskRunner> task_runner)
      : muxer_(muxer), task_runner_(std::move(task_runner)) {}

  // base::TaskRunner implementation.
  void PostTask(std::function<void()> task) override {
    CallWithGuard([&] { task_runner_->PostTask(std::move(task)); });
  }

  void PostDelayedTask(std::function<void()> task, uint32_t delay_ms) override {
    CallWithGuard(
        [&] { task_runner_->PostDelayedTask(std::move(task), delay_ms); });
  }

  void AddFileDescriptorWatch(base::PlatformHandle fd,
                              std::function<void()> callback) override {
    CallWithGuard(
        [&] { task_runner_->AddFileDescriptorWatch(fd, std::move(callback)); });
  }

  void RemoveFileDescriptorWatch(base::PlatformHandle fd) override {
    CallWithGuard([&] { task_runner_->RemoveFileDescriptorWatch(fd); });
  }

  bool RunsTasksOnCurrentThread() const override {
    bool result;
    CallWithGuard([&] { result = task_runner_->RunsTasksOnCurrentThread(); });
    return result;
  }

 private:
  template <typename T>
  void CallWithGuard(T lambda) const {
    auto* root_tls = muxer_->GetOrCreateTracingTLS();
    if (PERFETTO_UNLIKELY(root_tls->is_in_trace_point)) {
      lambda();
      return;
    }
    ScopedReentrancyAnnotator scoped_annotator(*root_tls);
    lambda();
  }

  TracingMuxer* const muxer_;
  std::unique_ptr<base::TaskRunner> task_runner_;
};

class StopArgsImpl : public DataSourceBase::StopArgs {
 public:
  std::function<void()> HandleStopAsynchronously() const override {
    auto closure = std::move(async_stop_closure);
    async_stop_closure = std::function<void()>();
    return closure;
  }

  mutable std::function<void()> async_stop_closure;
};

class FlushArgsImpl : public DataSourceBase::FlushArgs {
 public:
  std::function<void()> HandleFlushAsynchronously() const override {
    auto closure = std::move(async_flush_closure);
    async_flush_closure = std::function<void()>();
    return closure;
  }

  mutable std::function<void()> async_flush_closure;
};

// Holds an earlier TracingMuxerImpl instance after ResetForTesting() is called.
static TracingMuxerImpl* g_prev_instance{};

template <typename RegisteredBackend>
struct CompareBackendByType {
  static int BackendTypePriority(BackendType type) {
    switch (type) {
      case kSystemBackend:
        return 0;
      case kInProcessBackend:
        return 1;
      case kCustomBackend:
        return 2;
      // The UnspecifiedBackend has the highest priority so that
      // TracingBackendFake is the last one on the backend lists.
      case kUnspecifiedBackend:
        break;
    }
    return 3;
  }
  bool operator()(BackendType type, const RegisteredBackend& b) {
    return BackendTypePriority(type) < BackendTypePriority(b.type);
  }
};

BufferExhaustedPolicy ComputeBufferExhaustedPolicy(
    const DataSourceConfig& cfg,
    const DataSourceParams& params) {
  if (!params.buffer_exhausted_policy_configurable) {
    return params.default_buffer_exhausted_policy;
  }

  switch (cfg.buffer_exhausted_policy()) {
    case DataSourceConfig::BUFFER_EXHAUSTED_UNSPECIFIED:
      return params.default_buffer_exhausted_policy;
    case DataSourceConfig::BUFFER_EXHAUSTED_DROP:
      return BufferExhaustedPolicy::kDrop;
    case DataSourceConfig::BUFFER_EXHAUSTED_STALL_THEN_ABORT:
      return BufferExhaustedPolicy::kStall;
    case DataSourceConfig::BUFFER_EXHAUSTED_STALL_THEN_DROP:
      return BufferExhaustedPolicy::kStallThenDrop;
  }

  return params.default_buffer_exhausted_policy;
}

}  // namespace

// ----- Begin of TracingMuxerImpl::ProducerImpl
TracingMuxerImpl::ProducerImpl::ProducerImpl(
    TracingMuxerImpl* muxer,
    TracingBackendId backend_id,
    uint32_t shmem_batch_commits_duration_ms,
    bool shmem_direct_patching_enabled)
    : muxer_(muxer),
      backend_id_(backend_id),
      shmem_batch_commits_duration_ms_(shmem_batch_commits_duration_ms),
      shmem_direct_patching_enabled_(shmem_direct_patching_enabled) {}

TracingMuxerImpl::ProducerImpl::~ProducerImpl() {
  muxer_ = nullptr;
}

void TracingMuxerImpl::ProducerImpl::Initialize(
    std::unique_ptr<ProducerEndpoint> endpoint) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DCHECK(!connected_);
  connection_id_.fetch_add(1, std::memory_order_relaxed);
  is_producer_provided_smb_ = endpoint->shared_memory();
  last_startup_target_buffer_reservation_ = 0;

  // Adopt the endpoint into a shared pointer so that we can safely share it
  // across threads that create trace writers. The custom deleter function
  // ensures that the endpoint is always destroyed on the muxer's thread. (Note
  // that |task_runner| is assumed to outlive tracing sessions on all threads.)
  auto* task_runner = muxer_->task_runner_.get();
  auto deleter = [task_runner](ProducerEndpoint* e) {
    if (task_runner->RunsTasksOnCurrentThread()) {
      delete e;
      return;
    }
    task_runner->PostTask([e] { delete e; });
  };
  std::shared_ptr<ProducerEndpoint> service(endpoint.release(), deleter);
  // This atomic store is needed because another thread might be concurrently
  // creating a trace writer using the previous (disconnected) |service_|. See
  // CreateTraceWriter().
  std::atomic_store(&service_, std::move(service));
  // Don't try to use the service here since it may not have connected yet. See
  // OnConnect().
}

void TracingMuxerImpl::ProducerImpl::OnConnect() {
  PERFETTO_DLOG("Producer connected");
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DCHECK(!connected_);
  if (is_producer_provided_smb_ && !service_->IsShmemProvidedByProducer()) {
    PERFETTO_ELOG(
        "The service likely doesn't support producer-provided SMBs. Preventing "
        "future attempts to use producer-provided SMB again with this "
        "backend.");
    producer_provided_smb_failed_ = true;
    // Will call OnDisconnect() and cause a reconnect without producer-provided
    // SMB.
    service_->Disconnect();
    return;
  }
  connected_ = true;
  muxer_->UpdateDataSourcesOnAllBackends();
  SendOnConnectTriggers();
}

void TracingMuxerImpl::ProducerImpl::OnDisconnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  // If we're being destroyed, bail out.
  if (!muxer_)
    return;
  connected_ = false;
  // Active data sources for this producer will be stopped by
  // DestroyStoppedTraceWritersForCurrentThread() since the reconnected producer
  // will have a different connection id (even before it has finished
  // connecting).
  registered_data_sources_.reset();
  DisposeConnection();

  // Try reconnecting the producer.
  muxer_->OnProducerDisconnected(this);
}

void TracingMuxerImpl::ProducerImpl::DisposeConnection() {
  // Keep the old service around as a dead connection in case it has active
  // trace writers. If any tracing sessions were created, we can't clear
  // |service_| here because other threads may be concurrently creating new
  // trace writers. Any reconnection attempt will atomically swap the new
  // service in place of the old one.
  if (did_setup_tracing_ || did_setup_startup_tracing_) {
    dead_services_.push_back(service_);
  } else {
    service_.reset();
  }
}

void TracingMuxerImpl::ProducerImpl::OnTracingSetup() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  did_setup_tracing_ = true;
  service_->MaybeSharedMemoryArbiter()->SetBatchCommitsDuration(
      shmem_batch_commits_duration_ms_);
  if (shmem_direct_patching_enabled_) {
    service_->MaybeSharedMemoryArbiter()->EnableDirectSMBPatching();
  }
}

void TracingMuxerImpl::ProducerImpl::OnStartupTracingSetup() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  did_setup_startup_tracing_ = true;
}

void TracingMuxerImpl::ProducerImpl::SetupDataSource(
    DataSourceInstanceID id,
    const DataSourceConfig& cfg) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!muxer_)
    return;
  muxer_->SetupDataSource(
      backend_id_, connection_id_.load(std::memory_order_relaxed), id, cfg);
}

void TracingMuxerImpl::ProducerImpl::StartDataSource(DataSourceInstanceID id,
                                                     const DataSourceConfig&) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!muxer_)
    return;
  muxer_->StartDataSource(backend_id_, id);
  service_->NotifyDataSourceStarted(id);
}

void TracingMuxerImpl::ProducerImpl::StopDataSource(DataSourceInstanceID id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!muxer_)
    return;
  muxer_->StopDataSource_AsyncBegin(backend_id_, id);
}

void TracingMuxerImpl::ProducerImpl::Flush(
    FlushRequestID flush_id,
    const DataSourceInstanceID* instances,
    size_t instance_count,
    FlushFlags flush_flags) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  bool all_handled = true;
  if (muxer_) {
    for (size_t i = 0; i < instance_count; i++) {
      DataSourceInstanceID ds_id = instances[i];
      bool handled = muxer_->FlushDataSource_AsyncBegin(backend_id_, ds_id,
                                                        flush_id, flush_flags);
      if (!handled) {
        pending_flushes_[flush_id].insert(ds_id);
        all_handled = false;
      }
    }
  }

  if (all_handled) {
    service_->NotifyFlushComplete(flush_id);
  }
}

void TracingMuxerImpl::ProducerImpl::ClearIncrementalState(
    const DataSourceInstanceID* instances,
    size_t instance_count) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!muxer_)
    return;
  for (size_t inst_idx = 0; inst_idx < instance_count; inst_idx++) {
    muxer_->ClearDataSourceIncrementalState(backend_id_, instances[inst_idx]);
  }
}

bool TracingMuxerImpl::ProducerImpl::SweepDeadServices() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto is_unused = [](const std::shared_ptr<ProducerEndpoint>& endpoint) {
    auto* arbiter = endpoint->MaybeSharedMemoryArbiter();
    return !arbiter || arbiter->TryShutdown();
  };
  for (auto it = dead_services_.begin(); it != dead_services_.end();) {
    auto next_it = it;
    next_it++;
    if (is_unused(*it)) {
      dead_services_.erase(it);
    }
    it = next_it;
  }
  return dead_services_.empty();
}

void TracingMuxerImpl::ProducerImpl::SendOnConnectTriggers() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  base::TimeMillis now = base::GetWallTimeMs();
  std::vector<std::string> triggers;
  while (!on_connect_triggers_.empty()) {
    // Skip if we passed TTL.
    if (on_connect_triggers_.front().second > now) {
      triggers.push_back(std::move(on_connect_triggers_.front().first));
    }
    on_connect_triggers_.pop_front();
  }
  if (!triggers.empty()) {
    service_->ActivateTriggers(triggers);
  }
}

void TracingMuxerImpl::ProducerImpl::NotifyFlushForDataSourceDone(
    DataSourceInstanceID ds_id,
    FlushRequestID flush_id) {
  if (!connected_) {
    return;
  }

  {
    auto it = pending_flushes_.find(flush_id);
    if (it == pending_flushes_.end()) {
      return;
    }
    std::set<DataSourceInstanceID>& ds_ids = it->second;
    ds_ids.erase(ds_id);
  }

  std::optional<DataSourceInstanceID> biggest_flush_id;
  for (auto it = pending_flushes_.begin(); it != pending_flushes_.end();) {
    if (it->second.empty()) {
      biggest_flush_id = it->first;
      it = pending_flushes_.erase(it);
    } else {
      break;
    }
  }

  if (biggest_flush_id) {
    service_->NotifyFlushComplete(*biggest_flush_id);
  }
}

// ----- End of TracingMuxerImpl::ProducerImpl methods.

// ----- Begin of TracingMuxerImpl::ConsumerImpl
TracingMuxerImpl::ConsumerImpl::ConsumerImpl(TracingMuxerImpl* muxer,
                                             BackendType backend_type,
                                             TracingSessionGlobalID session_id)
    : muxer_(muxer), backend_type_(backend_type), session_id_(session_id) {}

TracingMuxerImpl::ConsumerImpl::~ConsumerImpl() {
  muxer_ = nullptr;
}

void TracingMuxerImpl::ConsumerImpl::Initialize(
    std::unique_ptr<ConsumerEndpoint> endpoint) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_ = std::move(endpoint);
  // Don't try to use the service here since it may not have connected yet. See
  // OnConnect().
}

void TracingMuxerImpl::ConsumerImpl::OnConnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DCHECK(!connected_);
  connected_ = true;

  // Observe data source instance events so we get notified when tracing starts.
  service_->ObserveEvents(ObservableEvents::TYPE_DATA_SOURCES_INSTANCES |
                          ObservableEvents::TYPE_ALL_DATA_SOURCES_STARTED);

  // If the API client configured and started tracing before we connected,
  // tell the backend about it now.
  if (trace_config_)
    muxer_->SetupTracingSession(session_id_, trace_config_);
  if (start_pending_)
    muxer_->StartTracingSession(session_id_);
  if (get_trace_stats_pending_) {
    auto callback = std::move(get_trace_stats_callback_);
    get_trace_stats_callback_ = nullptr;
    muxer_->GetTraceStats(session_id_, std::move(callback));
  }
  if (query_service_state_callback_) {
    auto callback = std::move(query_service_state_callback_);
    query_service_state_callback_ = nullptr;
    muxer_->QueryServiceState(session_id_, std::move(callback));
  }
  if (session_to_clone_) {
    service_->CloneSession(*session_to_clone_);
    session_to_clone_ = std::nullopt;
  }

  if (stop_pending_)
    muxer_->StopTracingSession(session_id_);
}

void TracingMuxerImpl::ConsumerImpl::OnDisconnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  // If we're being destroyed, bail out.
  if (!muxer_)
    return;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  if (!connected_ && backend_type_ == kSystemBackend) {
    PERFETTO_ELOG(
        "Unable to connect to the system tracing service as a consumer. On "
        "Android, use the \"perfetto\" command line tool instead to start "
        "system-wide tracing sessions");
  }
#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)

  // Notify the client about disconnection.
  NotifyError(TracingError{TracingError::kDisconnected, "Peer disconnected"});

  // Make sure the client doesn't hang in a blocking start/stop because of the
  // disconnection.
  NotifyStartComplete();
  NotifyStopComplete();

  // It shouldn't be necessary to call StopTracingSession. If we get this call
  // it means that the service did shutdown before us, so there is no point
  // trying it to ask it to stop the session. We should just remember to cleanup
  // the consumer vector.
  connected_ = false;

  // Notify the muxer that it is safe to destroy |this|. This is needed because
  // the ConsumerEndpoint stored in |service_| requires that |this| be safe to
  // access until OnDisconnect() is called.
  muxer_->OnConsumerDisconnected(this);
}

void TracingMuxerImpl::ConsumerImpl::Disconnect() {
  // This is weird and deserves a comment.
  //
  // When we called the ConnectConsumer method on the service it returns
  // us a ConsumerEndpoint which we stored in |service_|, however this
  // ConsumerEndpoint holds a pointer to the ConsumerImpl pointed to by
  // |this|. Part of the API contract to TracingService::ConnectConsumer is that
  // the ConsumerImpl pointer has to be valid until the
  // ConsumerImpl::OnDisconnect method is called. Therefore we reset the
  // ConsumerEndpoint |service_|. Eventually this will call
  // ConsumerImpl::OnDisconnect and we will inform the muxer it is safe to
  // call the destructor of |this|.
  service_.reset();
}

void TracingMuxerImpl::ConsumerImpl::OnTracingDisabled(
    const std::string& error) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DCHECK(!stopped_);
  stopped_ = true;

  if (!error.empty())
    NotifyError(TracingError{TracingError::kTracingFailed, error});

  // If we're still waiting for the start event, fire it now. This may happen if
  // there are no active data sources in the session.
  NotifyStartComplete();
  NotifyStopComplete();
}

void TracingMuxerImpl::ConsumerImpl::NotifyStartComplete() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (start_complete_callback_) {
    muxer_->task_runner_->PostTask(std::move(start_complete_callback_));
    start_complete_callback_ = nullptr;
  }
  if (blocking_start_complete_callback_) {
    muxer_->task_runner_->PostTask(
        std::move(blocking_start_complete_callback_));
    blocking_start_complete_callback_ = nullptr;
  }
}

void TracingMuxerImpl::ConsumerImpl::NotifyError(const TracingError& error) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (error_callback_) {
    muxer_->task_runner_->PostTask(
        std::bind(std::move(error_callback_), error));
  }
}

void TracingMuxerImpl::ConsumerImpl::NotifyStopComplete() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (stop_complete_callback_) {
    muxer_->task_runner_->PostTask(std::move(stop_complete_callback_));
    stop_complete_callback_ = nullptr;
  }
  if (blocking_stop_complete_callback_) {
    muxer_->task_runner_->PostTask(std::move(blocking_stop_complete_callback_));
    blocking_stop_complete_callback_ = nullptr;
  }
}

void TracingMuxerImpl::ConsumerImpl::OnTraceData(
    std::vector<TracePacket> packets,
    bool has_more) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!read_trace_callback_)
    return;

  size_t capacity = 0;
  for (const auto& packet : packets) {
    // 16 is an over-estimation of the proto preamble size
    capacity += packet.size() + 16;
  }

  // The shared_ptr is to avoid making a copy of the buffer when PostTask-ing.
  std::shared_ptr<std::vector<char>> buf(new std::vector<char>());
  buf->reserve(capacity);
  for (auto& packet : packets) {
    char* start;
    size_t size;
    std::tie(start, size) = packet.GetProtoPreamble();
    buf->insert(buf->end(), start, start + size);
    for (auto& slice : packet.slices()) {
      const auto* slice_data = reinterpret_cast<const char*>(slice.start);
      buf->insert(buf->end(), slice_data, slice_data + slice.size);
    }
  }

  auto callback = read_trace_callback_;
  muxer_->task_runner_->PostTask([callback, buf, has_more] {
    TracingSession::ReadTraceCallbackArgs callback_arg{};
    callback_arg.data = buf->empty() ? nullptr : &(*buf)[0];
    callback_arg.size = buf->size();
    callback_arg.has_more = has_more;
    callback(callback_arg);
  });

  if (!has_more)
    read_trace_callback_ = nullptr;
}

void TracingMuxerImpl::ConsumerImpl::OnObservableEvents(
    const ObservableEvents& events) {
  if (events.instance_state_changes_size()) {
    for (const auto& state_change : events.instance_state_changes()) {
      DataSourceHandle handle{state_change.producer_name(),
                              state_change.data_source_name()};
      data_source_states_[handle] =
          state_change.state() ==
          ObservableEvents::DATA_SOURCE_INSTANCE_STATE_STARTED;
    }
  }

  if (events.instance_state_changes_size() ||
      events.all_data_sources_started()) {
    // Data sources are first reported as being stopped before starting, so once
    // all the data sources we know about have started we can declare tracing
    // begun. In the case where there are no matching data sources for the
    // session, the service will report the all_data_sources_started() event
    // without adding any instances (only since Android S / Perfetto v10.0).
    if (start_complete_callback_ || blocking_start_complete_callback_) {
      bool all_data_sources_started = std::all_of(
          data_source_states_.cbegin(), data_source_states_.cend(),
          [](std::pair<DataSourceHandle, bool> state) { return state.second; });
      if (all_data_sources_started)
        NotifyStartComplete();
    }
  }
}

void TracingMuxerImpl::ConsumerImpl::OnSessionCloned(
    const OnSessionClonedArgs& args) {
  if (!clone_trace_callback_)
    return;
  TracingSession::CloneTraceCallbackArgs callback_arg{};
  callback_arg.success = args.success;
  callback_arg.error = std::move(args.error);
  callback_arg.uuid_msb = args.uuid.msb();
  callback_arg.uuid_lsb = args.uuid.lsb();
  muxer_->task_runner_->PostTask(
      std::bind(std::move(clone_trace_callback_), std::move(callback_arg)));
  clone_trace_callback_ = nullptr;
}

void TracingMuxerImpl::ConsumerImpl::OnTraceStats(
    bool success,
    const TraceStats& trace_stats) {
  if (!get_trace_stats_callback_)
    return;
  TracingSession::GetTraceStatsCallbackArgs callback_arg{};
  callback_arg.success = success;
  callback_arg.trace_stats_data = trace_stats.SerializeAsArray();
  muxer_->task_runner_->PostTask(
      std::bind(std::move(get_trace_stats_callback_), std::move(callback_arg)));
  get_trace_stats_callback_ = nullptr;
}

// The callbacks below are not used.
void TracingMuxerImpl::ConsumerImpl::OnDetach(bool) {}
void TracingMuxerImpl::ConsumerImpl::OnAttach(bool, const TraceConfig&) {}
// ----- End of TracingMuxerImpl::ConsumerImpl

// ----- Begin of TracingMuxerImpl::TracingSessionImpl

// TracingSessionImpl is the RAII object returned to API clients when they
// invoke Tracing::CreateTracingSession. They use it for starting/stopping
// tracing.

TracingMuxerImpl::TracingSessionImpl::TracingSessionImpl(
    TracingMuxerImpl* muxer,
    TracingSessionGlobalID session_id,
    BackendType backend_type)
    : muxer_(muxer), session_id_(session_id), backend_type_(backend_type) {}

// Can be destroyed from any thread.
TracingMuxerImpl::TracingSessionImpl::~TracingSessionImpl() {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask(
      [muxer, session_id] { muxer->DestroyTracingSession(session_id); });
}

// Can be called from any thread.
void TracingMuxerImpl::TracingSessionImpl::Setup(const TraceConfig& cfg,
                                                 int fd) {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  std::shared_ptr<TraceConfig> trace_config(new TraceConfig(cfg));
  if (fd >= 0) {
    base::ignore_result(backend_type_);  // For -Wunused in the amalgamation.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    if (backend_type_ != kInProcessBackend) {
      PERFETTO_FATAL(
          "Passing a file descriptor to TracingSession::Setup() is only "
          "supported with the kInProcessBackend on Windows. Use "
          "TracingSession::ReadTrace() instead");
    }
#endif
    trace_config->set_write_into_file(true);
    fd = dup(fd);
  }
  muxer->task_runner_->PostTask([muxer, session_id, trace_config, fd] {
    muxer->SetupTracingSession(session_id, trace_config, base::ScopedFile(fd));
  });
}

// Can be called from any thread.
void TracingMuxerImpl::TracingSessionImpl::Start() {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask(
      [muxer, session_id] { muxer->StartTracingSession(session_id); });
}

void TracingMuxerImpl::TracingSessionImpl::CloneTrace(CloneTraceArgs args,
                                                      CloneTraceCallback cb) {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask([muxer, session_id, args, cb] {
    muxer->CloneTracingSession(session_id, args, std::move(cb));
  });
}

// Can be called from any thread.
void TracingMuxerImpl::TracingSessionImpl::ChangeTraceConfig(
    const TraceConfig& cfg) {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask([muxer, session_id, cfg] {
    muxer->ChangeTracingSessionConfig(session_id, cfg);
  });
}

// Can be called from any thread except the service thread.
void TracingMuxerImpl::TracingSessionImpl::StartBlocking() {
  PERFETTO_DCHECK(!muxer_->task_runner_->RunsTasksOnCurrentThread());
  auto* muxer = muxer_;
  auto session_id = session_id_;
  base::WaitableEvent tracing_started;
  muxer->task_runner_->PostTask([muxer, session_id, &tracing_started] {
    auto* consumer = muxer->FindConsumer(session_id);
    if (!consumer) {
      // TODO(skyostil): Signal an error to the user.
      tracing_started.Notify();
      return;
    }
    PERFETTO_DCHECK(!consumer->blocking_start_complete_callback_);
    consumer->blocking_start_complete_callback_ = [&] {
      tracing_started.Notify();
    };
    muxer->StartTracingSession(session_id);
  });
  tracing_started.Wait();
}

// Can be called from any thread.
void TracingMuxerImpl::TracingSessionImpl::Flush(
    std::function<void(bool)> user_callback,
    uint32_t timeout_ms) {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask([muxer, session_id, timeout_ms, user_callback] {
    auto* consumer = muxer->FindConsumer(session_id);
    if (!consumer) {
      std::move(user_callback)(false);
      return;
    }
    muxer->FlushTracingSession(session_id, timeout_ms,
                               std::move(user_callback));
  });
}

// Can be called from any thread.
void TracingMuxerImpl::TracingSessionImpl::Stop() {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask(
      [muxer, session_id] { muxer->StopTracingSession(session_id); });
}

// Can be called from any thread except the service thread.
void TracingMuxerImpl::TracingSessionImpl::StopBlocking() {
  PERFETTO_DCHECK(!muxer_->task_runner_->RunsTasksOnCurrentThread());
  auto* muxer = muxer_;
  auto session_id = session_id_;
  base::WaitableEvent tracing_stopped;
  muxer->task_runner_->PostTask([muxer, session_id, &tracing_stopped] {
    auto* consumer = muxer->FindConsumer(session_id);
    if (!consumer) {
      // TODO(skyostil): Signal an error to the user.
      tracing_stopped.Notify();
      return;
    }
    PERFETTO_DCHECK(!consumer->blocking_stop_complete_callback_);
    consumer->blocking_stop_complete_callback_ = [&] {
      tracing_stopped.Notify();
    };
    muxer->StopTracingSession(session_id);
  });
  tracing_stopped.Wait();
}

// Can be called from any thread.
void TracingMuxerImpl::TracingSessionImpl::ReadTrace(ReadTraceCallback cb) {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask([muxer, session_id, cb] {
    muxer->ReadTracingSessionData(session_id, std::move(cb));
  });
}

// Can be called from any thread.
void TracingMuxerImpl::TracingSessionImpl::SetOnStartCallback(
    std::function<void()> cb) {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask([muxer, session_id, cb] {
    auto* consumer = muxer->FindConsumer(session_id);
    if (!consumer)
      return;
    consumer->start_complete_callback_ = cb;
  });
}

// Can be called from any thread
void TracingMuxerImpl::TracingSessionImpl::SetOnErrorCallback(
    std::function<void(TracingError)> cb) {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask([muxer, session_id, cb] {
    auto* consumer = muxer->FindConsumer(session_id);
    if (!consumer) {
      // Notify the client about concurrent disconnection of the session.
      if (cb)
        cb(TracingError{TracingError::kDisconnected, "Peer disconnected"});
      return;
    }
    consumer->error_callback_ = cb;
  });
}

// Can be called from any thread.
void TracingMuxerImpl::TracingSessionImpl::SetOnStopCallback(
    std::function<void()> cb) {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask([muxer, session_id, cb] {
    auto* consumer = muxer->FindConsumer(session_id);
    if (!consumer)
      return;
    consumer->stop_complete_callback_ = cb;
  });
}

// Can be called from any thread.
void TracingMuxerImpl::TracingSessionImpl::GetTraceStats(
    GetTraceStatsCallback cb) {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask([muxer, session_id, cb] {
    muxer->GetTraceStats(session_id, std::move(cb));
  });
}

// Can be called from any thread.
void TracingMuxerImpl::TracingSessionImpl::QueryServiceState(
    QueryServiceStateCallback cb) {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  muxer->task_runner_->PostTask([muxer, session_id, cb] {
    muxer->QueryServiceState(session_id, std::move(cb));
  });
}

// ----- End of TracingMuxerImpl::TracingSessionImpl

// ----- Begin of TracingMuxerImpl::StartupTracingSessionImpl

TracingMuxerImpl::StartupTracingSessionImpl::StartupTracingSessionImpl(
    TracingMuxerImpl* muxer,
    TracingSessionGlobalID session_id,
    BackendType backend_type)
    : muxer_(muxer), session_id_(session_id), backend_type_(backend_type) {}

// Can be destroyed from any thread.
TracingMuxerImpl::StartupTracingSessionImpl::~StartupTracingSessionImpl() =
    default;

void TracingMuxerImpl::StartupTracingSessionImpl::Abort() {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  auto backend_type = backend_type_;
  muxer->task_runner_->PostTask([muxer, session_id, backend_type] {
    muxer->AbortStartupTracingSession(session_id, backend_type);
  });
}

// Must not be called from the SDK's internal thread.
void TracingMuxerImpl::StartupTracingSessionImpl::AbortBlocking() {
  auto* muxer = muxer_;
  auto session_id = session_id_;
  auto backend_type = backend_type_;
  PERFETTO_CHECK(!muxer->task_runner_->RunsTasksOnCurrentThread());
  base::WaitableEvent event;
  muxer->task_runner_->PostTask([muxer, session_id, backend_type, &event] {
    muxer->AbortStartupTracingSession(session_id, backend_type);
    event.Notify();
  });
  event.Wait();
}

// ----- End of TracingMuxerImpl::StartupTracingSessionImpl

// static
TracingMuxer* TracingMuxer::instance_ = TracingMuxerFake::Get();

// This is called by perfetto::Tracing::Initialize().
// Can be called on any thread. Typically, but not necessarily, that will be
// the embedder's main thread.
TracingMuxerImpl::TracingMuxerImpl(const TracingInitArgs& args)
    : TracingMuxer(args.platform ? args.platform
                                 : Platform::GetDefaultPlatform()) {
  PERFETTO_DETACH_FROM_THREAD(thread_checker_);
  instance_ = this;

  // Create the thread where muxer, producers and service will live.
  Platform::CreateTaskRunnerArgs tr_args{/*name_for_debugging=*/"TracingMuxer"};
  task_runner_.reset(new NonReentrantTaskRunner(
      this, platform_->CreateTaskRunner(std::move(tr_args))));

  // Run the initializer on that thread.
  task_runner_->PostTask([this, args] {
    Initialize(args);
    AddBackends(args);
  });
}

void TracingMuxerImpl::Initialize(const TracingInitArgs& args) {
  PERFETTO_DCHECK_THREAD(thread_checker_);  // Rebind the thread checker.

  policy_ = args.tracing_policy;
  supports_multiple_data_source_instances_ =
      args.supports_multiple_data_source_instances;

  // Fallback backend for producer creation for an unsupported backend type.
  PERFETTO_CHECK(producer_backends_.empty());
  AddProducerBackend(internal::TracingBackendFake::GetInstance(),
                     BackendType::kUnspecifiedBackend, args);
  // Fallback backend for consumer creation for an unsupported backend type.
  // This backend simply fails any attempt to start a tracing session.
  PERFETTO_CHECK(consumer_backends_.empty());
  AddConsumerBackend(internal::TracingBackendFake::GetInstance(),
                     BackendType::kUnspecifiedBackend);
}

void TracingMuxerImpl::AddConsumerBackend(TracingConsumerBackend* backend,
                                          BackendType type) {
  if (!backend) {
    // We skip the log in release builds because the *_backend_fake.cc code
    // has already an ELOG before returning a nullptr.
    PERFETTO_DLOG("Consumer backend creation failed, type %d",
                  static_cast<int>(type));
    return;
  }
  // Keep the backends sorted by type.
  auto it =
      std::upper_bound(consumer_backends_.begin(), consumer_backends_.end(),
                       type, CompareBackendByType<RegisteredConsumerBackend>());
  it = consumer_backends_.emplace(it);

  RegisteredConsumerBackend& rb = *it;
  rb.backend = backend;
  rb.type = type;
}

void TracingMuxerImpl::AddProducerBackend(TracingProducerBackend* backend,
                                          BackendType type,
                                          const TracingInitArgs& args) {
  if (!backend) {
    // We skip the log in release builds because the *_backend_fake.cc code
    // has already an ELOG before returning a nullptr.
    PERFETTO_DLOG("Producer backend creation failed, type %d",
                  static_cast<int>(type));
    return;
  }
  TracingBackendId backend_id = producer_backends_.size();
  // Keep the backends sorted by type.
  auto it =
      std::upper_bound(producer_backends_.begin(), producer_backends_.end(),
                       type, CompareBackendByType<RegisteredProducerBackend>());
  it = producer_backends_.emplace(it);

  RegisteredProducerBackend& rb = *it;
  rb.backend = backend;
  rb.id = backend_id;
  rb.type = type;
  rb.producer.reset(new ProducerImpl(this, backend_id,
                                     args.shmem_batch_commits_duration_ms,
                                     args.shmem_direct_patching_enabled));
  rb.producer_conn_args.producer = rb.producer.get();
  rb.producer_conn_args.producer_name = platform_->GetCurrentProcessName();
  rb.producer_conn_args.task_runner = task_runner_.get();
  rb.producer_conn_args.shmem_size_hint_bytes = args.shmem_size_hint_kb * 1024;
  rb.producer_conn_args.shmem_page_size_hint_bytes =
      args.shmem_page_size_hint_kb * 1024;
  rb.producer_conn_args.create_socket_async = args.create_socket_async;
  rb.producer->Initialize(rb.backend->ConnectProducer(rb.producer_conn_args));
}

TracingMuxerImpl::RegisteredProducerBackend*
TracingMuxerImpl::FindProducerBackendById(TracingBackendId id) {
  for (RegisteredProducerBackend& b : producer_backends_) {
    if (b.id == id) {
      return &b;
    }
  }
  return nullptr;
}

TracingMuxerImpl::RegisteredProducerBackend*
TracingMuxerImpl::FindProducerBackendByType(BackendType type) {
  for (RegisteredProducerBackend& b : producer_backends_) {
    if (b.type == type) {
      return &b;
    }
  }
  return nullptr;
}

TracingMuxerImpl::RegisteredConsumerBackend*
TracingMuxerImpl::FindConsumerBackendByType(BackendType type) {
  for (RegisteredConsumerBackend& b : consumer_backends_) {
    if (b.type == type) {
      return &b;
    }
  }
  return nullptr;
}

void TracingMuxerImpl::AddBackends(const TracingInitArgs& args) {
  if (args.backends & kSystemBackend) {
    PERFETTO_CHECK(args.system_producer_backend_factory_);
    if (FindProducerBackendByType(kSystemBackend) == nullptr) {
      AddProducerBackend(args.system_producer_backend_factory_(),
                         kSystemBackend, args);
    }
    if (args.enable_system_consumer) {
      PERFETTO_CHECK(args.system_consumer_backend_factory_);
      if (FindConsumerBackendByType(kSystemBackend) == nullptr) {
        AddConsumerBackend(args.system_consumer_backend_factory_(),
                           kSystemBackend);
      }
    }
  }

  if (args.backends & kInProcessBackend) {
    TracingBackend* b = nullptr;
    if (FindProducerBackendByType(kInProcessBackend) == nullptr) {
      if (!b) {
        PERFETTO_CHECK(args.in_process_backend_factory_);
        b = args.in_process_backend_factory_();
      }
      AddProducerBackend(b, kInProcessBackend, args);
    }
    if (FindConsumerBackendByType(kInProcessBackend) == nullptr) {
      if (!b) {
        PERFETTO_CHECK(args.in_process_backend_factory_);
        b = args.in_process_backend_factory_();
      }
      AddConsumerBackend(b, kInProcessBackend);
    }
  }

  if (args.backends & kCustomBackend) {
    PERFETTO_CHECK(args.custom_backend);
    if (FindProducerBackendByType(kCustomBackend) == nullptr) {
      AddProducerBackend(args.custom_backend, kCustomBackend, args);
    }
    if (FindConsumerBackendByType(kCustomBackend) == nullptr) {
      AddConsumerBackend(args.custom_backend, kCustomBackend);
    }
  }

  if (args.backends & ~(kSystemBackend | kInProcessBackend | kCustomBackend)) {
    PERFETTO_FATAL("Unsupported tracing backend type");
  }
}

// Can be called from any thread (but not concurrently).
bool TracingMuxerImpl::RegisterDataSource(
    const DataSourceDescriptor& descriptor,
    DataSourceFactory factory,
    DataSourceParams params,
    bool no_flush,
    DataSourceStaticState* static_state) {
  // Ignore repeated registrations.
  if (static_state->index != kMaxDataSources)
    return true;

  uint32_t new_index = next_data_source_index_++;
  if (new_index >= kMaxDataSources) {
    PERFETTO_DLOG(
        "RegisterDataSource failed: too many data sources already registered");
    return false;
  }

  // Initialize the static state.
  static_assert(sizeof(static_state->instances[0]) >= sizeof(DataSourceState),
                "instances[] size mismatch");
  for (size_t i = 0; i < static_state->instances.size(); i++)
    new (&static_state->instances[i]) DataSourceState{};

  static_state->index = new_index;

  // Generate a semi-unique id for this data source.
  base::Hasher hash;
  hash.Update(reinterpret_cast<intptr_t>(static_state));
  hash.Update(base::GetWallTimeNs().count());
  static_state->id = hash.digest() ? hash.digest() : 1;

  task_runner_->PostTask(
      [this, descriptor, factory, static_state, params, no_flush] {
        data_sources_.emplace_back();
        RegisteredDataSource& rds = data_sources_.back();
        rds.descriptor = descriptor;
        rds.factory = factory;
        rds.params = params;
        if (!supports_multiple_data_source_instances_) {
          rds.params.supports_multiple_instances = false;
        }
        rds.static_state = static_state;
        rds.no_flush = no_flush;

        UpdateDataSourceOnAllBackends(rds, /*is_changed=*/false);
      });
  return true;
}

// Can be called from any thread (but not concurrently).
void TracingMuxerImpl::UpdateDataSourceDescriptor(
    const DataSourceDescriptor& descriptor,
    const DataSourceStaticState* static_state) {
  task_runner_->PostTask([this, descriptor, static_state] {
    for (auto& rds : data_sources_) {
      if (rds.static_state == static_state) {
        PERFETTO_CHECK(rds.descriptor.name() == descriptor.name());
        rds.descriptor = descriptor;
        rds.descriptor.set_id(static_state->id);
        UpdateDataSourceOnAllBackends(rds, /*is_changed=*/true);
        return;
      }
    }
  });
}

// Can be called from any thread (but not concurrently).
void TracingMuxerImpl::RegisterInterceptor(
    const InterceptorDescriptor& descriptor,
    InterceptorFactory factory,
    InterceptorBase::TLSFactory tls_factory,
    InterceptorBase::TracePacketCallback packet_callback) {
  task_runner_->PostTask([this, descriptor, factory, tls_factory,
                          packet_callback] {
    // Ignore repeated registrations.
    for (const auto& interceptor : interceptors_) {
      if (interceptor.descriptor.name() == descriptor.name()) {
        PERFETTO_DCHECK(interceptor.tls_factory == tls_factory);
        PERFETTO_DCHECK(interceptor.packet_callback == packet_callback);
        return;
      }
    }
    // Only allow certain interceptors for now.
    if (descriptor.name() != "test_interceptor" &&
        descriptor.name() != "console" && descriptor.name() != "etwexport") {
      PERFETTO_ELOG(
          "Interceptors are experimental. If you want to use them, please "
          "get in touch with the project maintainers "
          "(https://perfetto.dev/docs/contributing/"
          "getting-started#community).");
      return;
    }
    interceptors_.emplace_back();
    RegisteredInterceptor& interceptor = interceptors_.back();
    interceptor.descriptor = descriptor;
    interceptor.factory = factory;
    interceptor.tls_factory = tls_factory;
    interceptor.packet_callback = packet_callback;
  });
}

void TracingMuxerImpl::ActivateTriggers(
    const std::vector<std::string>& triggers,
    uint32_t ttl_ms) {
  base::TimeMillis expire_time =
      base::GetWallTimeMs() + base::TimeMillis(ttl_ms);
  task_runner_->PostTask([this, triggers, expire_time] {
    for (RegisteredProducerBackend& backend : producer_backends_) {
      if (backend.producer->connected_) {
        backend.producer->service_->ActivateTriggers(triggers);
      } else {
        for (const std::string& trigger : triggers) {
          backend.producer->on_connect_triggers_.emplace_back(trigger,
                                                              expire_time);
        }
      }
    }
  });
}

// Checks if there is any matching startup tracing data source instance for a
// new SetupDataSource call. If so, moves the data source to this tracing
// session (and its target buffer) and returns true, otherwise returns false.
static bool MaybeAdoptStartupTracingInDataSource(
    TracingBackendId backend_id,
    uint32_t backend_connection_id,
    DataSourceInstanceID instance_id,
    const DataSourceConfig& cfg,
    const std::vector<RegisteredDataSource>& data_sources) {
  for (const auto& rds : data_sources) {
    DataSourceStaticState* static_state = rds.static_state;
    for (uint32_t i = 0; i < kMaxDataSourceInstances; i++) {
      auto* internal_state = static_state->TryGet(i);

      if (internal_state &&
          internal_state->startup_target_buffer_reservation.load(
              std::memory_order_relaxed) &&
          internal_state->data_source_instance_id == 0 &&
          internal_state->backend_id == backend_id &&
          internal_state->backend_connection_id == backend_connection_id &&
          internal_state->config &&
          internal_state->data_source->CanAdoptStartupSession(
              *internal_state->config, cfg)) {
        PERFETTO_DLOG("Setting up data source %" PRIu64
                      " %s by adopting it from a startup tracing session",
                      instance_id, cfg.name().c_str());

        std::lock_guard<std::recursive_mutex> lock(internal_state->lock);
        // Set the associations. The actual takeover happens in
        // StartDataSource().
        internal_state->data_source_instance_id = instance_id;
        internal_state->buffer_id =
            static_cast<internal::BufferId>(cfg.target_buffer());
        internal_state->buffer_exhausted_policy =
            ComputeBufferExhaustedPolicy(cfg, rds.params);
        internal_state->config.reset(new DataSourceConfig(cfg));

        // TODO(eseckler): Should the data source config provided by the service
        // be allowed to specify additional interceptors / additional data
        // source params?

        return true;
      }
    }
  }
  return false;
}

// Called by the service of one of the backends.
void TracingMuxerImpl::SetupDataSource(TracingBackendId backend_id,
                                       uint32_t backend_connection_id,
                                       DataSourceInstanceID instance_id,
                                       const DataSourceConfig& cfg) {
  PERFETTO_DLOG("Setting up data source %" PRIu64 " %s", instance_id,
                cfg.name().c_str());
  PERFETTO_DCHECK_THREAD(thread_checker_);

  // First check if there is any matching startup tracing data source instance.
  if (MaybeAdoptStartupTracingInDataSource(backend_id, backend_connection_id,
                                           instance_id, cfg, data_sources_)) {
    return;
  }

  for (const auto& rds : data_sources_) {
    if (rds.descriptor.name() != cfg.name())
      continue;
    DataSourceStaticState& static_state = *rds.static_state;

    // If this data source is already active for this exact config, don't start
    // another instance. This happens when we have several data sources with the
    // same name, in which case the service sends one SetupDataSource event for
    // each one. Since we can't map which event maps to which data source, we
    // ensure each event only starts one data source instance.
    // TODO(skyostil): Register a unique id with each data source to the service
    // to disambiguate.
    bool active_for_config = false;
    for (uint32_t i = 0; i < kMaxDataSourceInstances; i++) {
      if (!static_state.TryGet(i))
        continue;
      auto* internal_state =
          reinterpret_cast<DataSourceState*>(&static_state.instances[i]);
      if (internal_state->backend_id == backend_id &&
          internal_state->backend_connection_id == backend_connection_id &&
          internal_state->config && *internal_state->config == cfg) {
        active_for_config = true;
        break;
      }
    }
    if (active_for_config) {
      PERFETTO_DLOG(
          "Data source %s is already active with this config, skipping",
          cfg.name().c_str());
      continue;
    }

    SetupDataSourceImpl(rds, backend_id, backend_connection_id, instance_id,
                        cfg, /*startup_session_id=*/0);
    return;
  }
}

TracingMuxerImpl::FindDataSourceRes TracingMuxerImpl::SetupDataSourceImpl(
    const RegisteredDataSource& rds,
    TracingBackendId backend_id,
    uint32_t backend_connection_id,
    DataSourceInstanceID instance_id,
    const DataSourceConfig& cfg,
    TracingSessionGlobalID startup_session_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  DataSourceStaticState& static_state = *rds.static_state;

  // If any bit is set in `static_state.valid_instances` then at least one
  // other instance of data source is running.
  if (!rds.params.supports_multiple_instances &&
      static_state.valid_instances.load(std::memory_order_acquire) != 0) {
    PERFETTO_ELOG(
        "Failed to setup data source because some another instance of this "
        "data source is already active");
    return FindDataSourceRes();
  }

  for (uint32_t i = 0; i < kMaxDataSourceInstances; i++) {
    // Find a free slot.
    if (static_state.TryGet(i))
      continue;

    auto* internal_state =
        reinterpret_cast<DataSourceState*>(&static_state.instances[i]);
    std::unique_lock<std::recursive_mutex> lock(internal_state->lock);
    static_assert(
        std::is_same<decltype(internal_state->data_source_instance_id),
                     DataSourceInstanceID>::value,
        "data_source_instance_id type mismatch");
    internal_state->muxer_id_for_testing = muxer_id_for_testing_;
    RegisteredProducerBackend& backend = *FindProducerBackendById(backend_id);

    if (startup_session_id) {
      uint16_t& last_reservation =
          backend.producer->last_startup_target_buffer_reservation_;
      if (last_reservation == std::numeric_limits<uint16_t>::max()) {
        PERFETTO_ELOG(
            "Startup buffer reservations exhausted, dropping data source");
        return FindDataSourceRes();
      }
      internal_state->startup_target_buffer_reservation.store(
          ++last_reservation, std::memory_order_relaxed);
    } else {
      internal_state->startup_target_buffer_reservation.store(
          0, std::memory_order_relaxed);
    }

    internal_state->backend_id = backend_id;
    internal_state->backend_connection_id = backend_connection_id;
    internal_state->data_source_instance_id = instance_id;
    internal_state->buffer_id =
        static_cast<internal::BufferId>(cfg.target_buffer());
    internal_state->buffer_exhausted_policy =
        ComputeBufferExhaustedPolicy(cfg, rds.params);
    internal_state->config.reset(new DataSourceConfig(cfg));
    internal_state->startup_session_id = startup_session_id;
    internal_state->data_source = rds.factory();
    internal_state->interceptor = nullptr;
    internal_state->interceptor_id = 0;
    internal_state->will_notify_on_stop = rds.descriptor.will_notify_on_stop();

    if (cfg.has_interceptor_config()) {
      for (size_t j = 0; j < interceptors_.size(); j++) {
        if (cfg.interceptor_config().name() ==
            interceptors_[j].descriptor.name()) {
          PERFETTO_DLOG("Intercepting data source %" PRIu64
                        " \"%s\" into \"%s\"",
                        instance_id, cfg.name().c_str(),
                        cfg.interceptor_config().name().c_str());
          internal_state->interceptor_id = static_cast<uint32_t>(j + 1);
          internal_state->interceptor = interceptors_[j].factory();
          internal_state->interceptor->OnSetup({cfg});
          break;
        }
      }
      if (!internal_state->interceptor_id) {
        PERFETTO_ELOG("Unknown interceptor configured for data source: %s",
                      cfg.interceptor_config().name().c_str());
      }
    }

    // This must be made at the end. See matching acquire-load in
    // DataSource::Trace().
    static_state.valid_instances.fetch_or(1 << i, std::memory_order_release);

    DataSourceBase::SetupArgs setup_args;
    setup_args.config = &cfg;
    setup_args.backend_type = backend.type;
    setup_args.internal_instance_index = i;

    if (!rds.params.requires_callbacks_under_lock)
      lock.unlock();
    internal_state->data_source->OnSetup(setup_args);

    return FindDataSourceRes(&static_state, internal_state, i,
                             rds.params.requires_callbacks_under_lock);
  }
  PERFETTO_ELOG(
      "Maximum number of data source instances exhausted. "
      "Dropping data source %" PRIu64,
      instance_id);
  return FindDataSourceRes();
}

// Called by the service of one of the backends.
void TracingMuxerImpl::StartDataSource(TracingBackendId backend_id,
                                       DataSourceInstanceID instance_id) {
  PERFETTO_DLOG("Starting data source %" PRIu64, instance_id);
  PERFETTO_DCHECK_THREAD(thread_checker_);

  auto ds = FindDataSource(backend_id, instance_id);
  if (!ds) {
    PERFETTO_ELOG("Could not find data source to start");
    return;
  }

  // Check if the data source was already started for startup tracing.
  uint16_t startup_reservation =
      ds.internal_state->startup_target_buffer_reservation.load(
          std::memory_order_relaxed);
  if (startup_reservation) {
    RegisteredProducerBackend& backend = *FindProducerBackendById(backend_id);
    TracingSessionGlobalID session_id = ds.internal_state->startup_session_id;
    auto session_it = std::find_if(
        backend.startup_sessions.begin(), backend.startup_sessions.end(),
        [session_id](const RegisteredStartupSession& session) {
          return session.session_id == session_id;
        });
    PERFETTO_DCHECK(session_it != backend.startup_sessions.end());

    if (session_it->is_aborting) {
      PERFETTO_DLOG("Data source %" PRIu64
                    " was already aborted for startup tracing, not starting it",
                    instance_id);
      return;
    }

    PERFETTO_DLOG(
        "Data source %" PRIu64
        " was already started for startup tracing, binding its target buffer",
        instance_id);

    backend.producer->service_->MaybeSharedMemoryArbiter()
        ->BindStartupTargetBuffer(startup_reservation,
                                  ds.internal_state->buffer_id);

    // The reservation ID can be used even after binding it, so there's no need
    // for any barriers here - we just need atomicity.
    ds.internal_state->startup_target_buffer_reservation.store(
        0, std::memory_order_relaxed);

    // TODO(eseckler): Should we reset incremental state at this point, or
    // notify the data source some other way?

    // The session should not have been fully bound yet (or aborted).
    PERFETTO_DCHECK(session_it->num_unbound_data_sources > 0);

    session_it->num_unbound_data_sources--;
    if (session_it->num_unbound_data_sources == 0) {
      if (session_it->on_adopted)
        task_runner_->PostTask(session_it->on_adopted);
      backend.startup_sessions.erase(session_it);
    }
    return;
  }

  StartDataSourceImpl(ds);
}

void TracingMuxerImpl::StartDataSourceImpl(const FindDataSourceRes& ds) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  DataSourceBase::StartArgs start_args{};
  start_args.internal_instance_index = ds.instance_idx;

  std::unique_lock<std::recursive_mutex> lock(ds.internal_state->lock);
  if (ds.internal_state->interceptor)
    ds.internal_state->interceptor->OnStart({});
  ds.internal_state->trace_lambda_enabled.store(true,
                                                std::memory_order_relaxed);
  PERFETTO_DCHECK(ds.internal_state->data_source != nullptr);

  if (!ds.requires_callbacks_under_lock)
    lock.unlock();
  ds.internal_state->data_source->OnStart(start_args);
}

// Called by the service of one of the backends.
void TracingMuxerImpl::StopDataSource_AsyncBegin(
    TracingBackendId backend_id,
    DataSourceInstanceID instance_id) {
  PERFETTO_DLOG("Stopping data source %" PRIu64, instance_id);
  PERFETTO_DCHECK_THREAD(thread_checker_);

  auto ds = FindDataSource(backend_id, instance_id);
  if (!ds) {
    PERFETTO_ELOG("Could not find data source to stop");
    return;
  }

  StopDataSource_AsyncBeginImpl(ds);
}

void TracingMuxerImpl::StopDataSource_AsyncBeginImpl(
    const FindDataSourceRes& ds) {
  TracingBackendId backend_id = ds.internal_state->backend_id;
  uint32_t backend_connection_id = ds.internal_state->backend_connection_id;
  DataSourceInstanceID instance_id = ds.internal_state->data_source_instance_id;

  StopArgsImpl stop_args{};
  stop_args.internal_instance_index = ds.instance_idx;
  stop_args.async_stop_closure = [this, backend_id, backend_connection_id,
                                  instance_id, ds] {
    // TracingMuxerImpl is long lived, capturing |this| is okay.
    // The notification closure can be moved out of the StopArgs by the
    // embedder to handle stop asynchronously. The embedder might then
    // call the closure on a different thread than the current one, hence
    // this nested PostTask().
    task_runner_->PostTask(
        [this, backend_id, backend_connection_id, instance_id, ds] {
          StopDataSource_AsyncEnd(backend_id, backend_connection_id,
                                  instance_id, ds);
        });
  };

  {
    std::unique_lock<std::recursive_mutex> lock(ds.internal_state->lock);

    // Don't call OnStop again if the datasource is already stopping.
    if (ds.internal_state->async_stop_in_progress)
      return;
    ds.internal_state->async_stop_in_progress = true;

    if (ds.internal_state->interceptor)
      ds.internal_state->interceptor->OnStop({});

    if (!ds.requires_callbacks_under_lock)
      lock.unlock();
    ds.internal_state->data_source->OnStop(stop_args);
  }

  // If the embedder hasn't called StopArgs.HandleStopAsynchronously() run the
  // async closure here. In theory we could avoid the PostTask and call
  // straight into CompleteDataSourceAsyncStop(). We keep that to reduce
  // divergencies between the deferred-stop vs non-deferred-stop code paths.
  if (stop_args.async_stop_closure)
    std::move(stop_args.async_stop_closure)();
}

void TracingMuxerImpl::StopDataSource_AsyncEnd(TracingBackendId backend_id,
                                               uint32_t backend_connection_id,
                                               DataSourceInstanceID instance_id,
                                               const FindDataSourceRes& ds) {
  PERFETTO_DLOG("Ending async stop of data source %" PRIu64, instance_id);
  PERFETTO_DCHECK_THREAD(thread_checker_);

  // Check that the data source instance is still active and was not modified
  // while it was being stopped.
  if (!ds.static_state->TryGet(ds.instance_idx) ||
      ds.internal_state->backend_id != backend_id ||
      ds.internal_state->backend_connection_id != backend_connection_id ||
      ds.internal_state->data_source_instance_id != instance_id) {
    PERFETTO_ELOG(
        "Async stop of data source %" PRIu64
        " failed. This might be due to calling the async_stop_closure twice.",
        instance_id);
    return;
  }

  const uint32_t mask = ~(1U << ds.instance_idx);
  ds.static_state->valid_instances.fetch_and(mask, std::memory_order_acq_rel);

  bool will_notify_on_stop;
  // Take the mutex to prevent that the data source is in the middle of
  // a Trace() execution where it called GetDataSourceLocked() while we
  // destroy it.
  uint16_t startup_buffer_reservation;
  TracingSessionGlobalID startup_session_id;
  {
    std::lock_guard<std::recursive_mutex> guard(ds.internal_state->lock);
    ds.internal_state->trace_lambda_enabled.store(false,
                                                  std::memory_order_relaxed);
    ds.internal_state->data_source.reset();
    ds.internal_state->interceptor.reset();
    ds.internal_state->config.reset();
    ds.internal_state->async_stop_in_progress = false;
    will_notify_on_stop = ds.internal_state->will_notify_on_stop;
    startup_buffer_reservation =
        ds.internal_state->startup_target_buffer_reservation.load(
            std::memory_order_relaxed);
    startup_session_id = ds.internal_state->startup_session_id;
  }

  // The other fields of internal_state are deliberately *not* cleared.
  // See races-related comments of DataSource::Trace().

  TracingMuxer::generation_++;

  // |producer_backends_| is append-only, Backend instances are always valid.
  PERFETTO_CHECK(backend_id < producer_backends_.size());
  RegisteredProducerBackend& backend = *FindProducerBackendById(backend_id);
  ProducerImpl* producer = backend.producer.get();
  if (!producer)
    return;

  // If the data source instance still has a startup buffer reservation, it was
  // only active for startup tracing and never started by the service. Discard
  // the startup buffer reservation.
  if (startup_buffer_reservation) {
    PERFETTO_DCHECK(startup_session_id);

    if (producer->service_ && producer->service_->MaybeSharedMemoryArbiter()) {
      producer->service_->MaybeSharedMemoryArbiter()
          ->AbortStartupTracingForReservation(startup_buffer_reservation);
    }

    auto session_it = std::find_if(
        backend.startup_sessions.begin(), backend.startup_sessions.end(),
        [startup_session_id](const RegisteredStartupSession& session) {
          return session.session_id == startup_session_id;
        });

    // Session should not be removed until abortion of all data source instances
    // is complete.
    PERFETTO_DCHECK(session_it != backend.startup_sessions.end());

    session_it->num_aborting_data_sources--;
    if (session_it->num_aborting_data_sources == 0) {
      if (session_it->on_aborted)
        task_runner_->PostTask(session_it->on_aborted);

      backend.startup_sessions.erase(session_it);
    }
  }

  if (producer->connected_ &&
      backend.producer->connection_id_.load(std::memory_order_relaxed) ==
          backend_connection_id) {
    // Flush any commits that might have been batched by SharedMemoryArbiter.
    producer->service_->MaybeSharedMemoryArbiter()
        ->FlushPendingCommitDataRequests();
    if (instance_id && will_notify_on_stop)
      producer->service_->NotifyDataSourceStopped(instance_id);
  }
  producer->SweepDeadServices();
}

void TracingMuxerImpl::ClearDataSourceIncrementalState(
    TracingBackendId backend_id,
    DataSourceInstanceID instance_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("Clearing incremental state for data source %" PRIu64,
                instance_id);
  auto ds = FindDataSource(backend_id, instance_id);
  if (!ds) {
    PERFETTO_ELOG("Could not find data source to clear incremental state for");
    return;
  }

  DataSourceBase::ClearIncrementalStateArgs clear_incremental_state_args;
  clear_incremental_state_args.internal_instance_index = ds.instance_idx;
  {
    std::unique_lock<std::recursive_mutex> lock;
    if (ds.requires_callbacks_under_lock)
      lock = std::unique_lock<std::recursive_mutex>(ds.internal_state->lock);
    ds.internal_state->data_source->WillClearIncrementalState(
        clear_incremental_state_args);
  }

  // Make DataSource::TraceContext::GetIncrementalState() eventually notice that
  // the incremental state should be cleared.
  ds.static_state->GetUnsafe(ds.instance_idx)
      ->incremental_state_generation.fetch_add(1, std::memory_order_relaxed);
}

bool TracingMuxerImpl::FlushDataSource_AsyncBegin(
    TracingBackendId backend_id,
    DataSourceInstanceID instance_id,
    FlushRequestID flush_id,
    FlushFlags flush_flags) {
  PERFETTO_DLOG("Flushing data source %" PRIu64, instance_id);
  auto ds = FindDataSource(backend_id, instance_id);
  if (!ds) {
    PERFETTO_ELOG("Could not find data source to flush");
    return true;
  }

  uint32_t backend_connection_id = ds.internal_state->backend_connection_id;

  FlushArgsImpl flush_args;
  flush_args.flush_flags = flush_flags;
  flush_args.internal_instance_index = ds.instance_idx;
  flush_args.async_flush_closure = [this, backend_id, backend_connection_id,
                                    instance_id, ds, flush_id] {
    // TracingMuxerImpl is long lived, capturing |this| is okay.
    // The notification closure can be moved out of the StopArgs by the
    // embedder to handle stop asynchronously. The embedder might then
    // call the closure on a different thread than the current one, hence
    // this nested PostTask().
    task_runner_->PostTask(
        [this, backend_id, backend_connection_id, instance_id, ds, flush_id] {
          FlushDataSource_AsyncEnd(backend_id, backend_connection_id,
                                   instance_id, ds, flush_id);
        });
  };
  {
    std::unique_lock<std::recursive_mutex> lock;
    if (ds.requires_callbacks_under_lock)
      lock = std::unique_lock<std::recursive_mutex>(ds.internal_state->lock);
    ds.internal_state->data_source->OnFlush(flush_args);
  }

  // |async_flush_closure| is moved out of |flush_args| if the producer
  // requested to handle the flush asynchronously.
  bool handled = static_cast<bool>(flush_args.async_flush_closure);
  return handled;
}

void TracingMuxerImpl::FlushDataSource_AsyncEnd(
    TracingBackendId backend_id,
    uint32_t backend_connection_id,
    DataSourceInstanceID instance_id,
    const FindDataSourceRes& ds,
    FlushRequestID flush_id) {
  PERFETTO_DLOG("Ending async flush of data source %" PRIu64, instance_id);
  PERFETTO_DCHECK_THREAD(thread_checker_);

  // Check that the data source instance is still active and was not modified
  // while it was being flushed.
  if (!ds.static_state->TryGet(ds.instance_idx) ||
      ds.internal_state->backend_id != backend_id ||
      ds.internal_state->backend_connection_id != backend_connection_id ||
      ds.internal_state->data_source_instance_id != instance_id) {
    PERFETTO_ELOG("Async flush of data source %" PRIu64
                  " failed. This might be due to the data source being stopped "
                  "in the meantime",
                  instance_id);
    return;
  }

  // |producer_backends_| is append-only, Backend instances are always valid.
  PERFETTO_CHECK(backend_id < producer_backends_.size());
  RegisteredProducerBackend& backend = *FindProducerBackendById(backend_id);

  ProducerImpl* producer = backend.producer.get();
  if (!producer)
    return;

  // If the tracing service disconnects and reconnects while a data source is
  // handling a flush request, there's no point is sending the flush reply to
  // the newly reconnected producer.
  if (producer->connected_ &&
      backend.producer->connection_id_.load(std::memory_order_relaxed) ==
          backend_connection_id) {
    producer->NotifyFlushForDataSourceDone(instance_id, flush_id);
  }
}

void TracingMuxerImpl::SyncProducersForTesting() {
  std::mutex mutex;
  std::condition_variable cv;

  // IPC-based producers don't report connection errors explicitly for each
  // command, but instead with an asynchronous callback
  // (ProducerImpl::OnDisconnected). This means that the sync command below
  // may have completed but failed to reach the service because of a
  // disconnection, but we can't tell until the disconnection message comes
  // through. To guard against this, we run two whole rounds of sync round-trips
  // before returning; the first one will detect any disconnected producers and
  // the second one will ensure any reconnections have completed and all data
  // sources are registered in the service again.
  for (size_t i = 0; i < 2; i++) {
    size_t countdown = std::numeric_limits<size_t>::max();
    task_runner_->PostTask([this, &mutex, &cv, &countdown] {
      {
        std::unique_lock<std::mutex> countdown_lock(mutex);
        countdown = producer_backends_.size();
      }
      for (auto& backend : producer_backends_) {
        auto* producer = backend.producer.get();
        producer->service_->Sync([&mutex, &cv, &countdown] {
          std::unique_lock<std::mutex> countdown_lock(mutex);
          countdown--;
          cv.notify_one();
        });
      }
    });

    {
      std::unique_lock<std::mutex> countdown_lock(mutex);
      cv.wait(countdown_lock, [&countdown] { return !countdown; });
    }
  }

  // Check that all producers are indeed connected.
  bool done = false;
  bool all_producers_connected = true;
  task_runner_->PostTask([this, &mutex, &cv, &done, &all_producers_connected] {
    for (auto& backend : producer_backends_)
      all_producers_connected &= backend.producer->connected_;
    std::unique_lock<std::mutex> lock(mutex);
    done = true;
    cv.notify_one();
  });

  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&done] { return done; });
  }
  PERFETTO_DCHECK(all_producers_connected);
}

void TracingMuxerImpl::DestroyStoppedTraceWritersForCurrentThread() {
  // Iterate across all possible data source types.
  auto cur_generation = generation_.load(std::memory_order_acquire);
  auto* root_tls = GetOrCreateTracingTLS();

  auto destroy_stopped_instances = [](DataSourceThreadLocalState& tls) {
    // |tls| has a vector of per-data-source-instance thread-local state.
    DataSourceStaticState* static_state = tls.static_state;
    if (!static_state)
      return;  // Slot not used.

    // Iterate across all possible instances for this data source.
    for (uint32_t inst = 0; inst < kMaxDataSourceInstances; inst++) {
      DataSourceInstanceThreadLocalState& ds_tls = tls.per_instance[inst];
      if (!ds_tls.trace_writer)
        continue;

      DataSourceState* ds_state = static_state->TryGet(inst);
      if (ds_state &&
          ds_state->muxer_id_for_testing == ds_tls.muxer_id_for_testing &&
          ds_state->backend_id == ds_tls.backend_id &&
          ds_state->backend_connection_id == ds_tls.backend_connection_id &&
          ds_state->startup_target_buffer_reservation.load(
              std::memory_order_relaxed) ==
              ds_tls.startup_target_buffer_reservation &&
          ds_state->buffer_id == ds_tls.buffer_id &&
          ds_state->data_source_instance_id == ds_tls.data_source_instance_id) {
        continue;
      }

      // The DataSource instance has been destroyed or recycled.
      ds_tls.Reset();  // Will also destroy the |ds_tls.trace_writer|.
    }
  };

  for (size_t ds_idx = 0; ds_idx < kMaxDataSources; ds_idx++) {
    // |tls| has a vector of per-data-source-instance thread-local state.
    DataSourceThreadLocalState& tls = root_tls->data_sources_tls[ds_idx];
    destroy_stopped_instances(tls);
  }
  destroy_stopped_instances(root_tls->track_event_tls);
  root_tls->generation = cur_generation;
}

// Called both when a new data source is registered or when a new backend
// connects. In both cases we want to be sure we reflected the data source
// registrations on the backends.
void TracingMuxerImpl::UpdateDataSourcesOnAllBackends() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (RegisteredDataSource& rds : data_sources_) {
    UpdateDataSourceOnAllBackends(rds, /*is_changed=*/false);
  }
}

void TracingMuxerImpl::UpdateDataSourceOnAllBackends(RegisteredDataSource& rds,
                                                     bool is_changed) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (RegisteredProducerBackend& backend : producer_backends_) {
    // We cannot call RegisterDataSource on the backend before it connects.
    if (!backend.producer->connected_)
      continue;

    PERFETTO_DCHECK(rds.static_state->index < kMaxDataSources);
    bool is_registered = backend.producer->registered_data_sources_.test(
        rds.static_state->index);
    if (is_registered && !is_changed)
      continue;

    if (!rds.descriptor.no_flush()) {
      rds.descriptor.set_no_flush(rds.no_flush);
    }
    rds.descriptor.set_will_notify_on_start(true);
    if (!rds.descriptor.has_will_notify_on_stop()) {
      rds.descriptor.set_will_notify_on_stop(true);
    }

    rds.descriptor.set_handles_incremental_state_clear(true);
    rds.descriptor.set_id(rds.static_state->id);
    if (is_registered) {
      backend.producer->service_->UpdateDataSource(rds.descriptor);
    } else {
      backend.producer->service_->RegisterDataSource(rds.descriptor);
    }
    backend.producer->registered_data_sources_.set(rds.static_state->index);
  }
}

void TracingMuxerImpl::SetupTracingSession(
    TracingSessionGlobalID session_id,
    const std::shared_ptr<TraceConfig>& trace_config,
    base::ScopedFile trace_fd) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_CHECK(!trace_fd || trace_config->write_into_file());

  auto* consumer = FindConsumer(session_id);
  if (!consumer)
    return;

  consumer->trace_config_ = trace_config;
  if (trace_fd)
    consumer->trace_fd_ = std::move(trace_fd);

  if (!consumer->connected_)
    return;

  // Only used in the deferred start mode.
  if (trace_config->deferred_start()) {
    consumer->service_->EnableTracing(*trace_config,
                                      std::move(consumer->trace_fd_));
  }
}

void TracingMuxerImpl::StartTracingSession(TracingSessionGlobalID session_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  auto* consumer = FindConsumer(session_id);

  if (!consumer)
    return;

  if (!consumer->trace_config_) {
    PERFETTO_ELOG("Must call Setup(config) first");
    return;
  }

  if (!consumer->connected_) {
    consumer->start_pending_ = true;
    return;
  }

  consumer->start_pending_ = false;
  if (consumer->trace_config_->deferred_start()) {
    consumer->service_->StartTracing();
  } else {
    consumer->service_->EnableTracing(*consumer->trace_config_,
                                      std::move(consumer->trace_fd_));
  }

  // TODO implement support for the deferred-start + fast-triggering case.
}

void TracingMuxerImpl::CloneTracingSession(
    TracingSessionGlobalID session_id,
    TracingSession::CloneTraceArgs args,
    TracingSession::CloneTraceCallback callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto* consumer = FindConsumer(session_id);
  if (!consumer) {
    TracingSession::CloneTraceCallbackArgs callback_arg{};
    callback_arg.success = false;
    callback_arg.error = "Tracing session not found";
    callback(callback_arg);
    return;
  }
  // Multiple concurrent cloning isn't supported.
  PERFETTO_DCHECK(!consumer->clone_trace_callback_);
  consumer->clone_trace_callback_ = std::move(callback);
  ConsumerEndpoint::CloneSessionArgs consumer_args{};
  consumer_args.unique_session_name = args.unique_session_name;
  if (!consumer->connected_) {
    consumer->session_to_clone_ = std::move(consumer_args);
    return;
  }
  consumer->session_to_clone_ = std::nullopt;
  consumer->service_->CloneSession(consumer_args);
}

void TracingMuxerImpl::ChangeTracingSessionConfig(
    TracingSessionGlobalID session_id,
    const TraceConfig& trace_config) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  auto* consumer = FindConsumer(session_id);

  if (!consumer)
    return;

  if (!consumer->trace_config_) {
    // Changing the config is only supported for started sessions.
    PERFETTO_ELOG("Must call Setup(config) and Start() first");
    return;
  }

  consumer->trace_config_ = std::make_shared<TraceConfig>(trace_config);
  if (consumer->connected_)
    consumer->service_->ChangeTraceConfig(trace_config);
}

void TracingMuxerImpl::FlushTracingSession(TracingSessionGlobalID session_id,
                                           uint32_t timeout_ms,
                                           std::function<void(bool)> callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto* consumer = FindConsumer(session_id);
  if (!consumer || consumer->start_pending_ || consumer->stop_pending_ ||
      !consumer->trace_config_) {
    PERFETTO_ELOG("Flush() can be called only after Start() and before Stop()");
    std::move(callback)(false);
    return;
  }

  // For now we don't want to expose the flush reason to the consumer-side SDK
  // users to avoid misuses until there is a strong need.
  consumer->service_->Flush(timeout_ms, std::move(callback),
                            FlushFlags(FlushFlags::Initiator::kConsumerSdk,
                                       FlushFlags::Reason::kExplicit));
}

void TracingMuxerImpl::StopTracingSession(TracingSessionGlobalID session_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto* consumer = FindConsumer(session_id);
  if (!consumer)
    return;

  if (consumer->start_pending_) {
    // If the session hasn't started yet, wait until it does before stopping.
    consumer->stop_pending_ = true;
    return;
  }

  consumer->stop_pending_ = false;
  if (consumer->stopped_) {
    // If the session was already stopped (e.g., it failed to start), don't try
    // stopping again.
    consumer->NotifyStopComplete();
  } else if (!consumer->trace_config_) {
    PERFETTO_ELOG("Must call Setup(config) and Start() first");
    return;
  } else {
    consumer->service_->DisableTracing();
  }

  consumer->trace_config_.reset();
}

void TracingMuxerImpl::DestroyTracingSession(
    TracingSessionGlobalID session_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (RegisteredConsumerBackend& backend : consumer_backends_) {
    // We need to find the consumer (if any) and call Disconnect as we destroy
    // the tracing session. We can't call Disconnect() inside this for loop
    // because in the in-process case this will end up to a synchronous call to
    // OnConsumerDisconnect which will invalidate all the iterators to
    // |backend.consumers|.
    ConsumerImpl* consumer = nullptr;
    for (auto& con : backend.consumers) {
      if (con->session_id_ == session_id) {
        consumer = con.get();
        break;
      }
    }
    if (consumer) {
      // We broke out of the loop above on the assumption that each backend will
      // only have a single consumer per session. This DCHECK ensures that
      // this is the case.
      PERFETTO_DCHECK(
          std::count_if(backend.consumers.begin(), backend.consumers.end(),
                        [session_id](const std::unique_ptr<ConsumerImpl>& con) {
                          return con->session_id_ == session_id;
                        }) == 1u);
      consumer->Disconnect();
    }
  }
}

void TracingMuxerImpl::ReadTracingSessionData(
    TracingSessionGlobalID session_id,
    std::function<void(TracingSession::ReadTraceCallbackArgs)> callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto* consumer = FindConsumer(session_id);
  if (!consumer) {
    // TODO(skyostil): Signal an error to the user.
    TracingSession::ReadTraceCallbackArgs callback_arg{};
    callback(callback_arg);
    return;
  }
  PERFETTO_DCHECK(!consumer->read_trace_callback_);
  consumer->read_trace_callback_ = std::move(callback);
  consumer->service_->ReadBuffers();
}

void TracingMuxerImpl::GetTraceStats(
    TracingSessionGlobalID session_id,
    TracingSession::GetTraceStatsCallback callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto* consumer = FindConsumer(session_id);
  if (!consumer) {
    TracingSession::GetTraceStatsCallbackArgs callback_arg{};
    callback_arg.success = false;
    callback(std::move(callback_arg));
    return;
  }
  PERFETTO_DCHECK(!consumer->get_trace_stats_callback_);
  consumer->get_trace_stats_callback_ = std::move(callback);
  if (!consumer->connected_) {
    consumer->get_trace_stats_pending_ = true;
    return;
  }
  consumer->get_trace_stats_pending_ = false;
  consumer->service_->GetTraceStats();
}

void TracingMuxerImpl::QueryServiceState(
    TracingSessionGlobalID session_id,
    TracingSession::QueryServiceStateCallback callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto* consumer = FindConsumer(session_id);
  if (!consumer) {
    TracingSession::QueryServiceStateCallbackArgs callback_arg{};
    callback_arg.success = false;
    callback(std::move(callback_arg));
    return;
  }
  PERFETTO_DCHECK(!consumer->query_service_state_callback_);
  if (!consumer->connected_) {
    consumer->query_service_state_callback_ = std::move(callback);
    return;
  }
  auto callback_wrapper = [callback](bool success,
                                     protos::gen::TracingServiceState state) {
    TracingSession::QueryServiceStateCallbackArgs callback_arg{};
    callback_arg.success = success;
    callback_arg.service_state_data = state.SerializeAsArray();
    callback(std::move(callback_arg));
  };
  consumer->service_->QueryServiceState({}, std::move(callback_wrapper));
}

void TracingMuxerImpl::SetBatchCommitsDurationForTesting(
    uint32_t batch_commits_duration_ms,
    BackendType backend_type) {
  for (RegisteredProducerBackend& backend : producer_backends_) {
    if (backend.producer && backend.producer->connected_ &&
        backend.type == backend_type) {
      backend.producer->service_->MaybeSharedMemoryArbiter()
          ->SetBatchCommitsDuration(batch_commits_duration_ms);
    }
  }
}

bool TracingMuxerImpl::EnableDirectSMBPatchingForTesting(
    BackendType backend_type) {
  for (RegisteredProducerBackend& backend : producer_backends_) {
    if (backend.producer && backend.producer->connected_ &&
        backend.type == backend_type &&
        !backend.producer->service_->MaybeSharedMemoryArbiter()
             ->EnableDirectSMBPatching()) {
      return false;
    }
  }
  return true;
}

TracingMuxerImpl::ConsumerImpl* TracingMuxerImpl::FindConsumer(
    TracingSessionGlobalID session_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  return FindConsumerAndBackend(session_id).first;
}

std::pair<TracingMuxerImpl::ConsumerImpl*,
          TracingMuxerImpl::RegisteredConsumerBackend*>
TracingMuxerImpl::FindConsumerAndBackend(TracingSessionGlobalID session_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (RegisteredConsumerBackend& backend : consumer_backends_) {
    for (auto& consumer : backend.consumers) {
      if (consumer->session_id_ == session_id) {
        return {consumer.get(), &backend};
      }
    }
  }
  return {nullptr, nullptr};
}

void TracingMuxerImpl::InitializeConsumer(TracingSessionGlobalID session_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  auto res = FindConsumerAndBackend(session_id);
  if (!res.first || !res.second)
    return;
  TracingMuxerImpl::ConsumerImpl* consumer = res.first;
  RegisteredConsumerBackend& backend = *res.second;

  TracingBackend::ConnectConsumerArgs conn_args;
  conn_args.consumer = consumer;
  conn_args.task_runner = task_runner_.get();
  consumer->Initialize(backend.backend->ConnectConsumer(conn_args));
}

void TracingMuxerImpl::OnConsumerDisconnected(ConsumerImpl* consumer) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (RegisteredConsumerBackend& backend : consumer_backends_) {
    auto pred = [consumer](const std::unique_ptr<ConsumerImpl>& con) {
      return con.get() == consumer;
    };
    backend.consumers.erase(std::remove_if(backend.consumers.begin(),
                                           backend.consumers.end(), pred),
                            backend.consumers.end());
  }
}

void TracingMuxerImpl::SetMaxProducerReconnectionsForTesting(uint32_t count) {
  max_producer_reconnections_.store(count);
}

void TracingMuxerImpl::OnProducerDisconnected(ProducerImpl* producer) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (RegisteredProducerBackend& backend : producer_backends_) {
    if (backend.producer.get() != producer)
      continue;

    // The tracing service is disconnected. It does not make sense to keep
    // tracing (we wouldn't be able to commit). On reconnection, the tracing
    // service will restart the data sources.
    for (const auto& rds : data_sources_) {
      DataSourceStaticState* static_state = rds.static_state;
      for (uint32_t i = 0; i < kMaxDataSourceInstances; i++) {
        auto* internal_state = static_state->TryGet(i);
        if (internal_state && internal_state->backend_id == backend.id &&
            internal_state->backend_connection_id ==
                backend.producer->connection_id_.load(
                    std::memory_order_relaxed)) {
          StopDataSource_AsyncBeginImpl(
              FindDataSourceRes(static_state, internal_state, i,
                                rds.params.requires_callbacks_under_lock));
        }
      }
    }

    // Try reconnecting the disconnected producer. If the connection succeeds,
    // all the data sources will be automatically re-registered.
    if (producer->connection_id_.load(std::memory_order_relaxed) >
        max_producer_reconnections_.load()) {
      // Avoid reconnecting a failing producer too many times. Instead we just
      // leak the producer instead of trying to avoid further complicating
      // cross-thread trace writer creation.
      PERFETTO_ELOG("Producer disconnected too many times; not reconnecting");
      continue;
    }

    backend.producer->Initialize(
        backend.backend->ConnectProducer(backend.producer_conn_args));
    // Don't use producer-provided SMBs for the next connection unless startup
    // tracing requires it again.
    backend.producer_conn_args.use_producer_provided_smb = false;
  }
}

void TracingMuxerImpl::SweepDeadBackends() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (auto it = dead_backends_.begin(); it != dead_backends_.end();) {
    auto next_it = it;
    next_it++;
    if (it->producer->SweepDeadServices())
      dead_backends_.erase(it);
    it = next_it;
  }
}

TracingMuxerImpl::FindDataSourceRes TracingMuxerImpl::FindDataSource(
    TracingBackendId backend_id,
    DataSourceInstanceID instance_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  RegisteredProducerBackend& backend = *FindProducerBackendById(backend_id);
  for (const auto& rds : data_sources_) {
    DataSourceStaticState* static_state = rds.static_state;
    for (uint32_t i = 0; i < kMaxDataSourceInstances; i++) {
      auto* internal_state = static_state->TryGet(i);
      if (internal_state && internal_state->backend_id == backend_id &&
          internal_state->backend_connection_id ==
              backend.producer->connection_id_.load(
                  std::memory_order_relaxed) &&
          internal_state->data_source_instance_id == instance_id) {
        return FindDataSourceRes(static_state, internal_state, i,
                                 rds.params.requires_callbacks_under_lock);
      }
    }
  }
  return FindDataSourceRes();
}

// Can be called from any thread.
std::unique_ptr<TraceWriterBase> TracingMuxerImpl::CreateTraceWriter(
    DataSourceStaticState* static_state,
    uint32_t data_source_instance_index,
    DataSourceState* data_source,
    BufferExhaustedPolicy buffer_exhausted_policy) {
  if (PERFETTO_UNLIKELY(data_source->interceptor_id)) {
    // If the session is being intercepted, return a heap-backed trace writer
    // instead. This is safe because all the data given to the interceptor is
    // either thread-local (|instance_index|), statically allocated
    // (|static_state|) or constant after initialization (|interceptor|). Access
    // to the interceptor instance itself through |data_source| is protected by
    // a statically allocated lock (similarly to the data source instance).
    auto& interceptor = interceptors_[data_source->interceptor_id - 1];
    return std::unique_ptr<TraceWriterBase>(new InterceptorTraceWriter(
        interceptor.tls_factory(static_state, data_source_instance_index),
        interceptor.packet_callback, static_state, data_source_instance_index));
  }
  ProducerImpl* producer =
      FindProducerBackendById(data_source->backend_id)->producer.get();
  // Atomically load the current service endpoint. We keep the pointer as a
  // shared pointer on the stack to guard against it from being concurrently
  // modified on the thread by ProducerImpl::Initialize() swapping in a
  // reconnected service on the muxer task runner thread.
  //
  // The endpoint may also be concurrently modified by SweepDeadServices()
  // clearing out old disconnected services. We guard against that by
  // SharedMemoryArbiter keeping track of any outstanding trace writers. After
  // shutdown has started, the trace writer created below will be a null one
  // which will drop any written data. See SharedMemoryArbiter::TryShutdown().
  //
  // We use an atomic pointer instead of holding a lock because
  // CreateTraceWriter posts tasks under the hood.
  std::shared_ptr<ProducerEndpoint> service =
      std::atomic_load(&producer->service_);

  // The service may have been disconnected and reconnected concurrently after
  // the data source was enabled, in which case we may not have an arbiter, or
  // would be creating a TraceWriter for the wrong (a newer) connection / SMB.
  // Instead, early-out now. A relaxed load is fine here because the atomic_load
  // above ensures that the |service| isn't newer.
  if (producer->connection_id_.load(std::memory_order_relaxed) !=
      data_source->backend_connection_id) {
    return std::unique_ptr<TraceWriter>(new NullTraceWriter());
  }

  // We just need a relaxed atomic read here: We can use the reservation ID even
  // after the buffer was bound, we just need to be sure to read it atomically.
  uint16_t startup_buffer_reservation =
      data_source->startup_target_buffer_reservation.load(
          std::memory_order_relaxed);
  if (startup_buffer_reservation) {
    return service->MaybeSharedMemoryArbiter()->CreateStartupTraceWriter(
        startup_buffer_reservation);
  }
  return service->CreateTraceWriter(data_source->buffer_id,
                                    buffer_exhausted_policy);
}

// This is called via the public API Tracing::NewTrace().
// Can be called from any thread.
std::unique_ptr<TracingSession> TracingMuxerImpl::CreateTracingSession(
    BackendType requested_backend_type,
    TracingConsumerBackend* (*system_backend_factory)()) {
  TracingSessionGlobalID session_id = ++next_tracing_session_id_;

  // |backend_type| can only specify one backend, not an OR-ed mask.
  PERFETTO_CHECK((requested_backend_type & (requested_backend_type - 1)) == 0);

  // Capturing |this| is fine because the TracingMuxer is a leaky singleton.
  task_runner_->PostTask([this, requested_backend_type, session_id,
                          system_backend_factory] {
    if (requested_backend_type == kSystemBackend && system_backend_factory &&
        !FindConsumerBackendByType(kSystemBackend)) {
      AddConsumerBackend(system_backend_factory(), kSystemBackend);
    }
    for (RegisteredConsumerBackend& backend : consumer_backends_) {
      if (requested_backend_type && backend.type &&
          backend.type != requested_backend_type) {
        continue;
      }

      // Create the consumer now, even if we have to ask the embedder below, so
      // that any other tasks executing after this one can find the consumer and
      // change its pending attributes.
      backend.consumers.emplace_back(
          new ConsumerImpl(this, backend.type, session_id));

      // The last registered backend in |consumer_backends_| is the unsupported
      // backend without a valid type.
      if (!backend.type) {
        PERFETTO_ELOG(
            "No tracing backend ready for type=%u, consumer will disconnect",
            requested_backend_type);
        InitializeConsumer(session_id);
        return;
      }

      // Check if the embedder wants to be asked for permission before
      // connecting the consumer.
      if (!policy_) {
        InitializeConsumer(session_id);
        return;
      }

      BackendType type = backend.type;
      TracingPolicy::ShouldAllowConsumerSessionArgs args;
      args.backend_type = backend.type;
      args.result_callback = [this, type, session_id](bool allow) {
        task_runner_->PostTask([this, type, session_id, allow] {
          if (allow) {
            InitializeConsumer(session_id);
            return;
          }

          PERFETTO_ELOG(
              "Consumer session for backend type type=%u forbidden, "
              "consumer will disconnect",
              type);

          auto* consumer = FindConsumer(session_id);
          if (!consumer)
            return;

          consumer->OnDisconnect();
        });
      };
      policy_->ShouldAllowConsumerSession(args);
      return;
    }
    PERFETTO_DFATAL("Not reached");
  });

  return std::unique_ptr<TracingSession>(
      new TracingSessionImpl(this, session_id, requested_backend_type));
}

// static
// This is called via the public API Tracing::SetupStartupTracing().
// Can be called from any thread.
std::unique_ptr<StartupTracingSession>
TracingMuxerImpl::CreateStartupTracingSession(
    const TraceConfig& config,
    Tracing::SetupStartupTracingOpts opts) {
  BackendType backend_type = opts.backend;
  // |backend_type| can only specify one backend, not an OR-ed mask.
  PERFETTO_CHECK((backend_type & (backend_type - 1)) == 0);
  // The in-process backend doesn't support startup tracing.
  PERFETTO_CHECK(backend_type != BackendType::kInProcessBackend);

  TracingSessionGlobalID session_id = ++next_tracing_session_id_;

  // Capturing |this| is fine because the TracingMuxer is a leaky singleton.
  task_runner_->PostTask([this, config, opts, backend_type, session_id] {
    for (RegisteredProducerBackend& backend : producer_backends_) {
      if (backend_type && backend.type && backend.type != backend_type) {
        continue;
      }

      TracingBackendId backend_id = backend.id;

      // The last registered backend in |producer_backends_| is the unsupported
      // backend without a valid type.
      if (!backend.type) {
        PERFETTO_ELOG(
            "No tracing backend initialized for type=%u, startup tracing "
            "failed",
            backend_type);
        if (opts.on_setup)
          opts.on_setup(Tracing::OnStartupTracingSetupCallbackArgs{
              0 /* num_data_sources_started */});
        return;
      }

      if (!backend.producer->service_ ||
          !backend.producer->service_->shared_memory()) {
        // If we unsuccessfully attempted to use a producer-provided SMB in the
        // past, don't try again.
        if (backend.producer->producer_provided_smb_failed_) {
          PERFETTO_ELOG(
              "Backend %zu doesn't seem to support producer-provided "
              "SMBs, startup tracing failed",
              backend_id);
          if (opts.on_setup)
            opts.on_setup(Tracing::OnStartupTracingSetupCallbackArgs{
                0 /* num_data_sources_started */});
          return;
        }

        PERFETTO_DLOG("Reconnecting backend %zu for startup tracing",
                      backend_id);
        backend.producer_conn_args.use_producer_provided_smb = true;
        backend.producer->service_->Disconnect();  // Causes a reconnect.
        PERFETTO_DCHECK(backend.producer->service_ &&
                        backend.producer->service_->MaybeSharedMemoryArbiter());
      }

      RegisteredStartupSession session;
      session.session_id = session_id;
      session.on_aborted = opts.on_aborted;
      session.on_adopted = opts.on_adopted;

      for (const TraceConfig::DataSource& ds_cfg : config.data_sources()) {
        // Find all matching data sources and start one instance of each.
        for (const auto& rds : data_sources_) {
          if (rds.descriptor.name() != ds_cfg.config().name())
            continue;

          PERFETTO_DLOG(
              "Setting up data source %s for startup tracing with target "
              "buffer reservation %" PRIi32,
              rds.descriptor.name().c_str(),
              backend.producer->last_startup_target_buffer_reservation_ + 1u);
          auto ds = SetupDataSourceImpl(
              rds, backend_id,
              backend.producer->connection_id_.load(std::memory_order_relaxed),
              /*instance_id=*/0, ds_cfg.config(),
              /*startup_session_id=*/session_id);
          if (ds) {
            StartDataSourceImpl(ds);
            session.num_unbound_data_sources++;
          }
        }
      }

      int num_ds = session.num_unbound_data_sources;
      auto on_setup = opts.on_setup;
      if (on_setup) {
        backend.producer->OnStartupTracingSetup();
        task_runner_->PostTask([on_setup, num_ds] {
          on_setup(Tracing::OnStartupTracingSetupCallbackArgs{num_ds});
        });
      }

      if (num_ds > 0) {
        backend.startup_sessions.push_back(std::move(session));

        if (opts.timeout_ms > 0) {
          task_runner_->PostDelayedTask(
              [this, session_id, backend_type] {
                AbortStartupTracingSession(session_id, backend_type);
              },
              opts.timeout_ms);
        }
      }
      return;
    }
    PERFETTO_DFATAL("Invalid startup tracing session backend");
  });

  return std::unique_ptr<StartupTracingSession>(
      new StartupTracingSessionImpl(this, session_id, backend_type));
}

// Must not be called from the SDK's internal thread.
std::unique_ptr<StartupTracingSession>
TracingMuxerImpl::CreateStartupTracingSessionBlocking(
    const TraceConfig& config,
    Tracing::SetupStartupTracingOpts opts) {
  auto previous_on_setup = std::move(opts.on_setup);
  PERFETTO_CHECK(!task_runner_->RunsTasksOnCurrentThread());
  base::WaitableEvent event;
  // It is safe to capture by reference because once on_setup is called only
  // once before this method returns.
  opts.on_setup = [&](Tracing::OnStartupTracingSetupCallbackArgs args) {
    if (previous_on_setup) {
      previous_on_setup(std::move(args));
    }
    event.Notify();
  };
  auto session = CreateStartupTracingSession(config, std::move(opts));
  event.Wait();
  return session;
}

void TracingMuxerImpl::AbortStartupTracingSession(
    TracingSessionGlobalID session_id,
    BackendType backend_type) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  for (RegisteredProducerBackend& backend : producer_backends_) {
    if (backend_type != backend.type)
      continue;

    auto session_it = std::find_if(
        backend.startup_sessions.begin(), backend.startup_sessions.end(),
        [session_id](const RegisteredStartupSession& session) {
          return session.session_id == session_id;
        });

    // The startup session may have already been aborted or fully adopted.
    if (session_it == backend.startup_sessions.end())
      return;
    if (session_it->is_aborting)
      return;

    session_it->is_aborting = true;

    // Iterate all data sources and abort them if they weren't adopted yet.
    for (const auto& rds : data_sources_) {
      DataSourceStaticState* static_state = rds.static_state;
      for (uint32_t i = 0; i < kMaxDataSourceInstances; i++) {
        auto* internal_state = static_state->TryGet(i);
        if (internal_state &&
            internal_state->startup_target_buffer_reservation.load(
                std::memory_order_relaxed) &&
            internal_state->data_source_instance_id == 0 &&
            internal_state->startup_session_id == session_id) {
          PERFETTO_DLOG(
              "Aborting startup tracing for data source %s (target buffer "
              "reservation %" PRIu16 ")",
              rds.descriptor.name().c_str(),
              internal_state->startup_target_buffer_reservation.load(
                  std::memory_order_relaxed));

          // Abort the instance asynchronously by stopping it. From this point
          // onwards, the service will not be able to adopt it via
          // StartDataSource().
          session_it->num_aborting_data_sources++;
          StopDataSource_AsyncBeginImpl(
              FindDataSourceRes(static_state, internal_state, i,
                                rds.params.requires_callbacks_under_lock));
        }
      }
    }

    // If we did everything right, we should have aborted all still-unbound data
    // source instances.
    PERFETTO_DCHECK(session_it->num_unbound_data_sources ==
                    session_it->num_aborting_data_sources);

    if (session_it->num_aborting_data_sources == 0) {
      if (session_it->on_aborted)
        task_runner_->PostTask(session_it->on_aborted);

      backend.startup_sessions.erase(session_it);
    }
    return;
  }
  // We might reach here in tests because when we start a trace, we post the
  // Task(AbortStartupTrace, delay=timeout). When we do
  // perfetto::ResetForTesting, we sweep dead backends, and we are not able to
  // kill those delayed tasks because TaskRunner doesn't have support for
  // deleting scheduled future tasks and TaskRunner doesn't have any API for us
  // to wait for the completion of all the scheduled tasks (apart from
  // deleting the TaskRunner) and we want to avoid doing that because we need
  // a long running TaskRunner in muxer.
  PERFETTO_DLOG("Invalid startup tracing session backend");
}

void TracingMuxerImpl::InitializeInstance(const TracingInitArgs& args) {
  if (instance_ != TracingMuxerFake::Get()) {
    // The tracing muxer was already initialized. We might need to initialize
    // additional backends that were not configured earlier.
    auto* muxer = static_cast<TracingMuxerImpl*>(instance_);
    muxer->task_runner_->PostTask([muxer, args] { muxer->AddBackends(args); });
    return;
  }
  // If we previously had a TracingMuxerImpl instance which was reset,
  // reinitialize and reuse it instead of trying to create a new one. See
  // ResetForTesting().
  if (g_prev_instance) {
    auto* muxer = g_prev_instance;
    g_prev_instance = nullptr;
    instance_ = muxer;
    muxer->task_runner_->PostTask([muxer, args] {
      muxer->Initialize(args);
      muxer->AddBackends(args);
    });
  } else {
    new TracingMuxerImpl(args);
  }
}

// static
void TracingMuxerImpl::ResetForTesting() {
  // Ideally we'd tear down the entire TracingMuxerImpl, but the lifetimes of
  // various objects make that a non-starter. In particular:
  //
  // 1) Any thread that has entered a trace event has a TraceWriter, which holds
  //    a reference back to ProducerImpl::service_.
  //
  // 2) ProducerImpl::service_ has a reference back to the ProducerImpl.
  //
  // 3) ProducerImpl holds reference to TracingMuxerImpl::task_runner_, which in
  //    turn depends on TracingMuxerImpl itself.
  //
  // Because of this, it's not safe to deallocate TracingMuxerImpl until all
  // threads have dropped their TraceWriters. Since we can't really ask the
  // caller to guarantee this, we'll instead reset enough of the muxer's state
  // so that it can be reinitialized later and ensure all necessary objects from
  // the old state remain alive until all references have gone away.
  auto* muxer = reinterpret_cast<TracingMuxerImpl*>(instance_);

  base::WaitableEvent reset_done;
  auto do_reset = [muxer, &reset_done] {
    muxer->DestroyStoppedTraceWritersForCurrentThread();
    // Unregister all data sources so they don't interfere with any future
    // tracing sessions.
    for (RegisteredDataSource& rds : muxer->data_sources_) {
      for (RegisteredProducerBackend& backend : muxer->producer_backends_) {
        if (!backend.producer->service_ || !backend.producer->connected_)
          continue;
        backend.producer->service_->UnregisterDataSource(rds.descriptor.name());
      }
    }
    for (auto& backend : muxer->consumer_backends_) {
      // Check that no consumer session is currently active on any backend.
      for (auto& consumer : backend.consumers)
        PERFETTO_CHECK(!consumer->service_);
    }
    for (auto& backend : muxer->producer_backends_) {
      backend.producer->muxer_ = nullptr;
      backend.producer->DisposeConnection();
      muxer->dead_backends_.push_back(std::move(backend));
    }
    muxer->consumer_backends_.clear();
    muxer->producer_backends_.clear();
    muxer->interceptors_.clear();

    for (auto& ds : muxer->data_sources_) {
      ds.static_state->ResetForTesting();
    }

    muxer->data_sources_.clear();
    muxer->next_data_source_index_ = 0;

    // Free all backends without active trace writers or other inbound
    // references. Note that even if all the backends get swept, the muxer still
    // needs to stay around since |task_runner_| is assumed to be long-lived.
    muxer->SweepDeadBackends();

    // Make sure we eventually discard any per-thread trace writers from the
    // previous instance.
    muxer->muxer_id_for_testing_++;

    g_prev_instance = muxer;
    instance_ = TracingMuxerFake::Get();

    // Call the user provided cleanups on the muxer thread.
    for (auto& cb : muxer->reset_callbacks_) {
      cb();
    }

    reset_done.Notify();
  };

  // Some tests run the muxer and the test on the same thread. In these cases,
  // we can reset synchronously.
  if (muxer->task_runner_->RunsTasksOnCurrentThread()) {
    do_reset();
  } else {
    muxer->DestroyStoppedTraceWritersForCurrentThread();
    muxer->task_runner_->PostTask(std::move(do_reset));
    reset_done.Wait();
    // Call the user provided cleanups also on this thread.
    for (auto& cb : muxer->reset_callbacks_) {
      cb();
    }
  }
  muxer->reset_callbacks_.clear();
}

// static
void TracingMuxerImpl::Shutdown() {
  auto* muxer = reinterpret_cast<TracingMuxerImpl*>(instance_);

  // Shutting down on the muxer thread would lead to a deadlock.
  PERFETTO_CHECK(!muxer->task_runner_->RunsTasksOnCurrentThread());
  muxer->DestroyStoppedTraceWritersForCurrentThread();

  std::unique_ptr<base::TaskRunner> owned_task_runner(
      muxer->task_runner_.get());
  base::WaitableEvent shutdown_done;
  owned_task_runner->PostTask([muxer, &shutdown_done] {
    // Check that no consumer session is currently active on any backend.
    // Producers will be automatically disconnected as a part of deleting the
    // muxer below.
    for (auto& backend : muxer->consumer_backends_) {
      for (auto& consumer : backend.consumers) {
        PERFETTO_CHECK(!consumer->service_);
      }
    }
    // Make sure no trace writers are lingering around on the muxer thread. Note
    // that we can't do this for any arbitrary thread in the process; it is the
    // caller's responsibility to clean them up before shutting down Perfetto.
    muxer->DestroyStoppedTraceWritersForCurrentThread();
    // The task runner must be deleted outside the muxer thread. This is done by
    // `owned_task_runner` above.
    muxer->task_runner_.release();
    auto* platform = muxer->platform_;
    delete muxer;
    instance_ = TracingMuxerFake::Get();
    platform->Shutdown();
    shutdown_done.Notify();
  });
  shutdown_done.Wait();
}

void TracingMuxerImpl::AppendResetForTestingCallback(std::function<void()> cb) {
  reset_callbacks_.push_back(std::move(cb));
}

TracingMuxer::~TracingMuxer() = default;

static_assert(std::is_same<internal::BufferId, BufferID>::value,
              "public's BufferId and tracing/core's BufferID diverged");

}  // namespace internal
}  // namespace perfetto
