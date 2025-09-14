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

#include "src/profiling/memory/heapprofd_producer.h"

#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <optional>
#include <string>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/thread_task_runner.h"
#include "perfetto/ext/base/watchdog_posix.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/ext/tracing/ipc/producer_ipc_client.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "protos/perfetto/trace/profiling/profile_packet.pbzero.h"
#include "src/profiling/common/producer_support.h"
#include "src/profiling/common/profiler_guardrails.h"
#include "src/profiling/memory/shared_ring_buffer.h"
#include "src/profiling/memory/unwound_messages.h"
#include "src/profiling/memory/wire_protocol.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <sys/system_properties.h>
#endif

namespace perfetto {
namespace profiling {
namespace {
using ::perfetto::protos::pbzero::ProfilePacket;

constexpr char kHeapprofdDataSource[] = "android.heapprofd";
constexpr size_t kUnwinderThreads = 5;

constexpr uint32_t kInitialConnectionBackoffMs = 100;
constexpr uint32_t kMaxConnectionBackoffMs = 30 * 1000;
constexpr uint32_t kGuardrailIntervalMs = 30 * 1000;

constexpr uint64_t kDefaultShmemSize = 8 * 1048576;  // ~8 MB
constexpr uint64_t kMaxShmemSize = 500 * 1048576;    // ~500 MB

// Constants specified by bionic, hardcoded here for simplicity.
constexpr int kProfilingSignal = __SIGRTMIN + 4;
constexpr int kHeapprofdSignalValue = 0;

std::vector<UnwindingWorker> MakeUnwindingWorkers(HeapprofdProducer* delegate,
                                                  size_t n) {
  std::vector<UnwindingWorker> ret;
  for (size_t i = 0; i < n; ++i) {
    ret.emplace_back(delegate,
                     base::ThreadTaskRunner::CreateAndStart("heapprofdunwind"));
  }
  return ret;
}

bool ConfigTargetsProcess(const HeapprofdConfig& cfg,
                          const Process& proc,
                          const std::vector<std::string>& normalized_cmdlines) {
  if (cfg.all())
    return true;

  const auto& pids = cfg.pid();
  if (std::find(pids.cbegin(), pids.cend(), static_cast<uint64_t>(proc.pid)) !=
      pids.cend()) {
    return true;
  }

  if (std::find(normalized_cmdlines.cbegin(), normalized_cmdlines.cend(),
                proc.cmdline) != normalized_cmdlines.cend()) {
    return true;
  }
  return false;
}

bool IsFile(int fd, const char* fn) {
  struct stat fdstat;
  struct stat fnstat;
  if (fstat(fd, &fdstat) == -1) {
    PERFETTO_PLOG("fstat");
    return false;
  }
  if (lstat(fn, &fnstat) == -1) {
    PERFETTO_PLOG("lstat");
    return false;
  }
  return fdstat.st_ino == fnstat.st_ino;
}

protos::pbzero::ProfilePacket::ProcessHeapSamples::ClientError
ErrorStateToProto(SharedRingBuffer::ErrorState state) {
  switch (state) {
    case (SharedRingBuffer::kNoError):
      return protos::pbzero::ProfilePacket::ProcessHeapSamples::
          CLIENT_ERROR_NONE;
    case (SharedRingBuffer::kHitTimeout):
      return protos::pbzero::ProfilePacket::ProcessHeapSamples::
          CLIENT_ERROR_HIT_TIMEOUT;
    case (SharedRingBuffer::kInvalidStackBounds):
      return protos::pbzero::ProfilePacket::ProcessHeapSamples::
          CLIENT_ERROR_INVALID_STACK_BOUNDS;
  }
}

}  // namespace

bool HeapprofdConfigToClientConfiguration(
    const HeapprofdConfig& heapprofd_config,
    ClientConfiguration* cli_config) {
  cli_config->default_interval = heapprofd_config.sampling_interval_bytes();
  cli_config->block_client = heapprofd_config.block_client();
  cli_config->disable_fork_teardown = heapprofd_config.disable_fork_teardown();
  cli_config->disable_vfork_detection =
      heapprofd_config.disable_vfork_detection();
  cli_config->block_client_timeout_us =
      heapprofd_config.block_client_timeout_us();
  cli_config->all_heaps = heapprofd_config.all_heaps();
  cli_config->adaptive_sampling_shmem_threshold =
      heapprofd_config.adaptive_sampling_shmem_threshold();
  cli_config->adaptive_sampling_max_sampling_interval_bytes =
      heapprofd_config.adaptive_sampling_max_sampling_interval_bytes();
  size_t n = 0;
  const std::vector<std::string>& exclude_heaps =
      heapprofd_config.exclude_heaps();
  // heaps[i] and heaps_interval[i] represent that the heap named in heaps[i]
  // should be sampled with sampling interval of heap_interval[i].
  std::vector<std::string> heaps = heapprofd_config.heaps();
  std::vector<uint64_t> heap_intervals =
      heapprofd_config.heap_sampling_intervals();
  if (heaps.empty() && !cli_config->all_heaps) {
    heaps.push_back("libc.malloc");
  }

  if (heap_intervals.empty()) {
    heap_intervals.assign(heaps.size(),
                          heapprofd_config.sampling_interval_bytes());
  }
  if (heap_intervals.size() != heaps.size()) {
    PERFETTO_ELOG("heap_sampling_intervals and heaps length mismatch.");
    return false;
  }
  if (std::find(heap_intervals.begin(), heap_intervals.end(), 0u) !=
      heap_intervals.end()) {
    PERFETTO_ELOG("zero sampling interval.");
    return false;
  }
  if (!exclude_heaps.empty()) {
    // For disabled heaps, we add explicit entries but with sampling interval
    // 0. The consumer of the sampling intervals in ClientConfiguration,
    // GetSamplingInterval in wire_protocol.h, uses 0 to signal a heap is
    // disabled, either because it isn't enabled (all_heaps is not set, and the
    // heap isn't named), or because we explicitly set it here.
    heaps.insert(heaps.end(), exclude_heaps.cbegin(), exclude_heaps.cend());
    heap_intervals.insert(heap_intervals.end(), exclude_heaps.size(), 0u);
  }
  if (heaps.size() > base::ArraySize(cli_config->heaps)) {
    heaps.resize(base::ArraySize(cli_config->heaps));
    PERFETTO_ELOG("Too many heaps requested. Truncating.");
  }
  for (size_t i = 0; i < heaps.size(); ++i) {
    const std::string& heap = heaps[i];
    const uint64_t interval = heap_intervals[i];
    // -1 for the \0 byte.
    if (heap.size() > HEAPPROFD_HEAP_NAME_SZ - 1) {
      PERFETTO_ELOG("Invalid heap name %s (larger than %d)", heap.c_str(),
                    HEAPPROFD_HEAP_NAME_SZ - 1);
      continue;
    }
    base::StringCopy(&cli_config->heaps[n].name[0], heap.c_str(),
                     sizeof(cli_config->heaps[n].name));
    cli_config->heaps[n].interval = interval;
    n++;
  }
  cli_config->num_heaps = n;
  return true;
}

// We create kUnwinderThreads unwinding threads. Bookkeeping is done on the main
// thread.
HeapprofdProducer::HeapprofdProducer(HeapprofdMode mode,
                                     base::TaskRunner* task_runner,
                                     bool exit_when_done)
    : task_runner_(task_runner),
      mode_(mode),
      exit_when_done_(exit_when_done),
      socket_delegate_(this),
      weak_factory_(this),
      unwinding_workers_(MakeUnwindingWorkers(this, kUnwinderThreads)) {
  CheckDataSourceCpuTask();
  CheckDataSourceMemoryTask();
}

HeapprofdProducer::~HeapprofdProducer() = default;

void HeapprofdProducer::SetTargetProcess(pid_t target_pid,
                                         std::string target_cmdline) {
  target_process_.pid = target_pid;
  target_process_.cmdline = target_cmdline;
}

void HeapprofdProducer::SetDataSourceCallback(std::function<void()> fn) {
  data_source_callback_ = fn;
}

void HeapprofdProducer::AdoptSocket(base::ScopedFile fd) {
  PERFETTO_DCHECK(mode_ == HeapprofdMode::kChild);
  auto socket = base::UnixSocket::AdoptConnected(
      std::move(fd), &socket_delegate_, task_runner_, base::SockFamily::kUnix,
      base::SockType::kStream);

  HandleClientConnection(std::move(socket), target_process_);
}

void HeapprofdProducer::OnConnect() {
  PERFETTO_DCHECK(state_ == kConnecting);
  state_ = kConnected;
  ResetConnectionBackoff();
  PERFETTO_LOG("Connected to the service, mode [%s].",
               mode_ == HeapprofdMode::kCentral ? "central" : "child");

  DataSourceDescriptor desc;
  desc.set_name(kHeapprofdDataSource);
  desc.set_will_notify_on_stop(true);
  endpoint_->RegisterDataSource(desc);
}

void HeapprofdProducer::OnDisconnect() {
  PERFETTO_DCHECK(state_ == kConnected || state_ == kConnecting);
  PERFETTO_LOG("Disconnected from tracing service");

  // Do not attempt to reconnect if we're a process-private process, just quit.
  if (exit_when_done_) {
    TerminateProcess(/*exit_status=*/1);  // does not return
  }

  // Central mode - attempt to reconnect.
  auto weak_producer = weak_factory_.GetWeakPtr();
  if (state_ == kConnected)
    return task_runner_->PostTask([weak_producer] {
      if (!weak_producer)
        return;
      weak_producer->Restart();
    });

  state_ = kNotConnected;
  IncreaseConnectionBackoff();
  task_runner_->PostDelayedTask(
      [weak_producer] {
        if (!weak_producer)
          return;
        weak_producer->ConnectService();
      },
      connection_backoff_ms_);
}

void HeapprofdProducer::ConnectWithRetries(const char* socket_name) {
  PERFETTO_DCHECK(state_ == kNotStarted);
  state_ = kNotConnected;

  ResetConnectionBackoff();
  producer_sock_name_ = socket_name;
  ConnectService();
}

void HeapprofdProducer::ConnectService() {
  SetProducerEndpoint(ProducerIPCClient::Connect(
      producer_sock_name_, this, "android.heapprofd", task_runner_));
}

void HeapprofdProducer::SetProducerEndpoint(
    std::unique_ptr<TracingService::ProducerEndpoint> endpoint) {
  PERFETTO_DCHECK(state_ == kNotConnected || state_ == kNotStarted);
  state_ = kConnecting;
  endpoint_ = std::move(endpoint);
}

void HeapprofdProducer::IncreaseConnectionBackoff() {
  connection_backoff_ms_ *= 2;
  if (connection_backoff_ms_ > kMaxConnectionBackoffMs)
    connection_backoff_ms_ = kMaxConnectionBackoffMs;
}

void HeapprofdProducer::ResetConnectionBackoff() {
  connection_backoff_ms_ = kInitialConnectionBackoffMs;
}

void HeapprofdProducer::Restart() {
  // We lost the connection with the tracing service. At this point we need
  // to reset all the data sources. Trying to handle that manually is going to
  // be error prone. What we do here is simply destroy the instance and
  // recreate it again.

  // Oneshot producer should not attempt restarts.
  if (exit_when_done_)
    PERFETTO_FATAL("Attempting to restart a one shot producer.");

  HeapprofdMode mode = mode_;
  base::TaskRunner* task_runner = task_runner_;
  const char* socket_name = producer_sock_name_;
  const bool exit_when_done = exit_when_done_;

  // Invoke destructor and then the constructor again.
  this->~HeapprofdProducer();
  new (this) HeapprofdProducer(mode, task_runner, exit_when_done);

  ConnectWithRetries(socket_name);
}

// TODO(rsavitski): would be cleaner to shut down the event loop instead
// (letting main exit). One test-friendly approach is to supply a shutdown
// callback in the constructor.
__attribute__((noreturn)) void HeapprofdProducer::TerminateProcess(
    int exit_status) {
  PERFETTO_CHECK(mode_ == HeapprofdMode::kChild);
  PERFETTO_LOG("Shutting down child heapprofd (status %d).", exit_status);
  exit(exit_status);
}

void HeapprofdProducer::OnTracingSetup() {}

void HeapprofdProducer::WriteRejectedConcurrentSession(BufferID buffer_id,
                                                       pid_t pid) {
  auto trace_writer =
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall);
  auto trace_packet = trace_writer->NewTracePacket();
  trace_packet->set_timestamp(
      static_cast<uint64_t>(base::GetBootTimeNs().count()));
  auto profile_packet = trace_packet->set_profile_packet();
  auto process_dump = profile_packet->add_process_dumps();
  process_dump->set_pid(static_cast<uint64_t>(pid));
  process_dump->set_rejected_concurrent(true);
  trace_packet->Finalize();
  trace_writer->Flush();
}

void HeapprofdProducer::SetupDataSource(DataSourceInstanceID id,
                                        const DataSourceConfig& ds_config) {
  if (ds_config.session_initiator() ==
      DataSourceConfig::SESSION_INITIATOR_TRUSTED_SYSTEM) {
    PERFETTO_LOG("Setting up datasource: statsd initiator.");
  } else {
    PERFETTO_LOG("Setting up datasource: non-statsd initiator.");
  }
  if (mode_ == HeapprofdMode::kChild && ds_config.enable_extra_guardrails()) {
    PERFETTO_ELOG("enable_extra_guardrails is not supported on user.");
    return;
  }

  HeapprofdConfig heapprofd_config;
  heapprofd_config.ParseFromString(ds_config.heapprofd_config_raw());

  if (heapprofd_config.all() && !heapprofd_config.pid().empty())
    PERFETTO_ELOG("No point setting all and pid");
  if (heapprofd_config.all() && !heapprofd_config.process_cmdline().empty())
    PERFETTO_ELOG("No point setting all and process_cmdline");

  if (ds_config.name() != kHeapprofdDataSource) {
    PERFETTO_DLOG("Invalid data source name.");
    return;
  }

  if (data_sources_.find(id) != data_sources_.end()) {
    PERFETTO_ELOG("Received duplicated data source instance id: %" PRIu64, id);
    return;
  }

  std::optional<std::vector<std::string>> normalized_cmdlines =
      NormalizeCmdlines(heapprofd_config.process_cmdline());
  if (!normalized_cmdlines.has_value()) {
    PERFETTO_ELOG("Rejecting data source due to invalid cmdline in config.");
    return;
  }

  // Child mode is only interested in the first data source matching the
  // already-connected process.
  if (mode_ == HeapprofdMode::kChild) {
    if (!ConfigTargetsProcess(heapprofd_config, target_process_,
                              normalized_cmdlines.value())) {
      PERFETTO_DLOG("Child mode skipping setup of unrelated data source.");
      return;
    }

    if (!data_sources_.empty()) {
      PERFETTO_LOG("Child mode skipping concurrent data source.");

      // Manually write one ProfilePacket about the rejected session.
      auto buffer_id = static_cast<BufferID>(ds_config.target_buffer());
      WriteRejectedConcurrentSession(buffer_id, target_process_.pid);
      return;
    }
  }

  std::optional<uint64_t> start_cputime_sec;
  if (heapprofd_config.max_heapprofd_cpu_secs() > 0) {
    start_cputime_sec = GetCputimeSecForCurrentProcess();

    if (!start_cputime_sec) {
      PERFETTO_ELOG("Failed to enforce CPU guardrail. Rejecting config.");
      return;
    }
  }

  auto buffer_id = static_cast<BufferID>(ds_config.target_buffer());
  DataSource data_source(
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
  data_source.id = id;
  auto& cli_config = data_source.client_configuration;
  if (!HeapprofdConfigToClientConfiguration(heapprofd_config, &cli_config))
    return;
  data_source.config = heapprofd_config;
  data_source.ds_config = ds_config;
  data_source.normalized_cmdlines = std::move(normalized_cmdlines.value());
  data_source.stop_timeout_ms = ds_config.stop_timeout_ms()
                                    ? ds_config.stop_timeout_ms()
                                    : 5000 /* kDataSourceStopTimeoutMs */;
  data_source.guardrail_config.cpu_start_secs = start_cputime_sec;
  data_source.guardrail_config.memory_guardrail_kb =
      heapprofd_config.max_heapprofd_memory_kb();
  data_source.guardrail_config.cpu_guardrail_sec =
      heapprofd_config.max_heapprofd_cpu_secs();

  InterningOutputTracker::WriteFixedInterningsPacket(
      data_source.trace_writer.get(),
      protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);
  data_sources_.emplace(id, std::move(data_source));
  PERFETTO_DLOG("Set up data source.");

  if (mode_ == HeapprofdMode::kChild && data_source_callback_)
    (*data_source_callback_)();
}

bool HeapprofdProducer::IsPidProfiled(pid_t pid) {
  return std::any_of(
      data_sources_.cbegin(), data_sources_.cend(),
      [pid](const std::pair<const DataSourceInstanceID, DataSource>& p) {
        const DataSource& ds = p.second;
        return ds.process_states.count(pid) > 0;
      });
}

void HeapprofdProducer::SetStartupProperties(DataSource* data_source) {
  const HeapprofdConfig& heapprofd_config = data_source->config;
  if (heapprofd_config.all())
    data_source->properties.emplace_back(properties_.SetAll());

  for (std::string cmdline : data_source->normalized_cmdlines)
    data_source->properties.emplace_back(
        properties_.SetProperty(std::move(cmdline)));
}

void HeapprofdProducer::SignalRunningProcesses(DataSource* data_source) {
  const HeapprofdConfig& heapprofd_config = data_source->config;

  std::set<pid_t> pids;
  if (heapprofd_config.all())
    FindAllProfilablePids(&pids);
  for (uint64_t pid : heapprofd_config.pid())
    pids.emplace(static_cast<pid_t>(pid));

  if (!data_source->normalized_cmdlines.empty())
    FindPidsForCmdlines(data_source->normalized_cmdlines, &pids);

  if (heapprofd_config.min_anonymous_memory_kb() > 0)
    RemoveUnderAnonThreshold(heapprofd_config.min_anonymous_memory_kb(), &pids);

  for (auto pid_it = pids.cbegin(); pid_it != pids.cend();) {
    pid_t pid = *pid_it;
    if (IsPidProfiled(pid)) {
      PERFETTO_LOG("Rejecting concurrent session for %" PRIdMAX,
                   static_cast<intmax_t>(pid));
      data_source->rejected_pids.emplace(pid);
      pid_it = pids.erase(pid_it);
      continue;
    }

    PERFETTO_DLOG("Sending signal: %d (si_value: %d) to pid: %d",
                  kProfilingSignal, kHeapprofdSignalValue, pid);
    union sigval signal_value;
    signal_value.sival_int = kHeapprofdSignalValue;
    if (sigqueue(pid, kProfilingSignal, signal_value) != 0) {
      PERFETTO_DPLOG("sigqueue");
    }
    ++pid_it;
  }
  data_source->signaled_pids = std::move(pids);
}

void HeapprofdProducer::StartDataSource(DataSourceInstanceID id,
                                        const DataSourceConfig&) {
  PERFETTO_DLOG("Starting data source %" PRIu64, id);

  auto it = data_sources_.find(id);
  if (it == data_sources_.end()) {
    // This is expected in child heapprofd, where we reject uninteresting data
    // sources in SetupDataSource.
    if (mode_ == HeapprofdMode::kCentral) {
      PERFETTO_ELOG("Received invalid data source instance to start: %" PRIu64,
                    id);
    }
    return;
  }

  DataSource& data_source = it->second;
  if (data_source.started) {
    PERFETTO_ELOG("Trying to start already started data-source: %" PRIu64, id);
    return;
  }
  const HeapprofdConfig& heapprofd_config = data_source.config;

  // Central daemon - set system properties for any targets that start later,
  // and signal already-running targets to start the profiling client.
  if (mode_ == HeapprofdMode::kCentral) {
    if (!heapprofd_config.no_startup())
      SetStartupProperties(&data_source);
    if (!heapprofd_config.no_running())
      SignalRunningProcesses(&data_source);
  }

  const auto continuous_dump_config = heapprofd_config.continuous_dump_config();
  uint32_t dump_interval = continuous_dump_config.dump_interval_ms();
  if (dump_interval) {
    data_source.dump_interval_ms = dump_interval;
    auto weak_producer = weak_factory_.GetWeakPtr();
    task_runner_->PostDelayedTask(
        [weak_producer, id] {
          if (!weak_producer)
            return;
          weak_producer->DoDrainAndContinuousDump(id);
        },
        continuous_dump_config.dump_phase_ms());
  }
  data_source.started = true;
  PERFETTO_DLOG("Started DataSource");
}

UnwindingWorker& HeapprofdProducer::UnwinderForPID(pid_t pid) {
  return unwinding_workers_[static_cast<uint64_t>(pid) % kUnwinderThreads];
}

void HeapprofdProducer::StopDataSource(DataSourceInstanceID id) {
  auto it = data_sources_.find(id);
  if (it == data_sources_.end()) {
    endpoint_->NotifyDataSourceStopped(id);
    if (mode_ == HeapprofdMode::kCentral)
      PERFETTO_ELOG("Trying to stop non existing data source: %" PRIu64, id);
    return;
  }

  PERFETTO_LOG("Stopping data source %" PRIu64, id);

  DataSource& data_source = it->second;
  data_source.was_stopped = true;
  ShutdownDataSource(&data_source);
}

void HeapprofdProducer::ShutdownDataSource(DataSource* data_source) {
  data_source->shutting_down = true;
  // If no processes connected, or all of them have already disconnected
  // (and have been dumped) and no PIDs have been rejected,
  // MaybeFinishDataSource can tear down the data source.
  if (MaybeFinishDataSource(data_source))
    return;

  if (!data_source->rejected_pids.empty()) {
    auto trace_packet = data_source->trace_writer->NewTracePacket();
    ProfilePacket* profile_packet = trace_packet->set_profile_packet();
    for (pid_t rejected_pid : data_source->rejected_pids) {
      ProfilePacket::ProcessHeapSamples* proto =
          profile_packet->add_process_dumps();
      proto->set_pid(static_cast<uint64_t>(rejected_pid));
      proto->set_rejected_concurrent(true);
    }
    trace_packet->Finalize();
    data_source->rejected_pids.clear();
    if (MaybeFinishDataSource(data_source))
      return;
  }

  for (const auto& pid_and_process_state : data_source->process_states) {
    pid_t pid = pid_and_process_state.first;
    UnwinderForPID(pid).PostDisconnectSocket(pid);
  }

  auto id = data_source->id;
  auto weak_producer = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_producer, id] {
        if (!weak_producer)
          return;
        auto ds_it = weak_producer->data_sources_.find(id);
        if (ds_it != weak_producer->data_sources_.end()) {
          PERFETTO_ELOG("Final dump timed out.");
          DataSource& ds = ds_it->second;

          for (const auto& pid_and_process_state : ds.process_states) {
            pid_t pid = pid_and_process_state.first;
            weak_producer->UnwinderForPID(pid).PostPurgeProcess(pid);
          }
          // Do not dump any stragglers, just trigger the Flush and tear down
          // the data source.
          ds.process_states.clear();
          ds.rejected_pids.clear();
          PERFETTO_CHECK(weak_producer->MaybeFinishDataSource(&ds));
        }
      },
      data_source->stop_timeout_ms);
}

void HeapprofdProducer::DoDrainAndContinuousDump(DataSourceInstanceID id) {
  auto it = data_sources_.find(id);
  if (it == data_sources_.end())
    return;
  DataSource& data_source = it->second;
  PERFETTO_DCHECK(data_source.pending_free_drains == 0);

  for (auto& [pid, process_state] : data_source.process_states) {
    UnwinderForPID(pid).PostDrainFree(data_source.id, pid);
    data_source.pending_free_drains++;
  }

  // In case there are no pending free drains, dump immediately.
  DoContinuousDump(&data_source);
}

void HeapprofdProducer::DoContinuousDump(DataSource* ds) {
  if (ds->pending_free_drains != 0) {
    return;
  }

  DumpProcessesInDataSource(ds);
  auto id = ds->id;
  auto weak_producer = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_producer, id] {
        if (!weak_producer)
          return;
        weak_producer->DoDrainAndContinuousDump(id);
      },
      ds->dump_interval_ms);
}

void HeapprofdProducer::PostDrainDone(UnwindingWorker*,
                                      DataSourceInstanceID ds_id) {
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this, ds_id] {
    if (weak_this)
      weak_this->DrainDone(ds_id);
  });
}

void HeapprofdProducer::DrainDone(DataSourceInstanceID ds_id) {
  auto it = data_sources_.find(ds_id);
  if (it == data_sources_.end()) {
    return;
  }
  DataSource& data_source = it->second;
  data_source.pending_free_drains--;
  DoContinuousDump(&data_source);
}

// static
void HeapprofdProducer::SetStats(
    protos::pbzero::ProfilePacket::ProcessStats* stats,
    const ProcessState& process_state) {
  stats->set_unwinding_errors(process_state.unwinding_errors);
  stats->set_heap_samples(process_state.heap_samples);
  stats->set_map_reparses(process_state.map_reparses);
  stats->set_total_unwinding_time_us(process_state.total_unwinding_time_us);
  stats->set_client_spinlock_blocked_us(
      process_state.client_spinlock_blocked_us);
  auto* unwinding_hist = stats->set_unwinding_time_us();
  for (const auto& p : process_state.unwinding_time_us.GetData()) {
    auto* bucket = unwinding_hist->add_buckets();
    if (p.first == LogHistogram::kMaxBucket)
      bucket->set_max_bucket(true);
    else
      bucket->set_upper_limit(p.first);
    bucket->set_count(p.second);
  }
}

void HeapprofdProducer::DumpProcessState(DataSource* data_source,
                                         pid_t pid,
                                         ProcessState* process_state) {
  for (auto& heap_id_and_heap_info : process_state->heap_infos) {
    ProcessState::HeapInfo& heap_info = heap_id_and_heap_info.second;

    bool from_startup = data_source->signaled_pids.find(pid) ==
                        data_source->signaled_pids.cend();

    auto new_heapsamples = [pid, from_startup, process_state, data_source,
                            &heap_info](
                               ProfilePacket::ProcessHeapSamples* proto) {
      proto->set_pid(static_cast<uint64_t>(pid));
      proto->set_timestamp(heap_info.heap_tracker.dump_timestamp());
      proto->set_from_startup(from_startup);
      proto->set_disconnected(process_state->disconnected);
      proto->set_buffer_overran(process_state->error_state ==
                                SharedRingBuffer::kHitTimeout);
      proto->set_client_error(ErrorStateToProto(process_state->error_state));
      proto->set_buffer_corrupted(process_state->buffer_corrupted);
      proto->set_hit_guardrail(data_source->hit_guardrail);
      if (!heap_info.heap_name.empty())
        proto->set_heap_name(heap_info.heap_name.c_str());
      proto->set_sampling_interval_bytes(heap_info.sampling_interval);
      proto->set_orig_sampling_interval_bytes(heap_info.orig_sampling_interval);
      auto* stats = proto->set_stats();
      SetStats(stats, *process_state);
    };

    DumpState dump_state(data_source->trace_writer.get(),
                         std::move(new_heapsamples),
                         &data_source->intern_state);

    heap_info.heap_tracker.GetCallstackAllocations(
        [&dump_state,
         &data_source](const HeapTracker::CallstackAllocations& alloc) {
          dump_state.WriteAllocation(alloc, data_source->config.dump_at_max());
        });
    dump_state.DumpCallstacks(&callsites_);
  }
}

void HeapprofdProducer::DumpProcessesInDataSource(DataSource* ds) {
  for (std::pair<const pid_t, ProcessState>& pid_and_process_state :
       ds->process_states) {
    pid_t pid = pid_and_process_state.first;
    ProcessState& process_state = pid_and_process_state.second;
    DumpProcessState(ds, pid, &process_state);
  }
}

void HeapprofdProducer::DumpAll() {
  PERFETTO_LOG("Received signal. Dumping all data sources.");
  for (auto& id_and_data_source : data_sources_)
    DumpProcessesInDataSource(&id_and_data_source.second);
}

void HeapprofdProducer::Flush(FlushRequestID flush_id,
                              const DataSourceInstanceID* ids,
                              size_t num_ids,
                              FlushFlags) {
  size_t& flush_in_progress = flushes_in_progress_[flush_id];
  PERFETTO_DCHECK(flush_in_progress == 0);
  flush_in_progress = num_ids;
  for (size_t i = 0; i < num_ids; ++i) {
    auto it = data_sources_.find(ids[i]);
    if (it == data_sources_.end()) {
      PERFETTO_ELOG("Trying to flush unknown data-source %" PRIu64, ids[i]);
      flush_in_progress--;
      continue;
    }
    DataSource& data_source = it->second;
    auto weak_producer = weak_factory_.GetWeakPtr();

    auto callback = [weak_producer, flush_id] {
      if (weak_producer)
        // Reposting because this task runner could be on a different thread
        // than the IPC task runner.
        return weak_producer->task_runner_->PostTask([weak_producer, flush_id] {
          if (weak_producer)
            return weak_producer->FinishDataSourceFlush(flush_id);
        });
    };
    data_source.trace_writer->Flush(std::move(callback));
  }
  if (flush_in_progress == 0) {
    endpoint_->NotifyFlushComplete(flush_id);
    flushes_in_progress_.erase(flush_id);
  }
}

void HeapprofdProducer::FinishDataSourceFlush(FlushRequestID flush_id) {
  auto it = flushes_in_progress_.find(flush_id);
  if (it == flushes_in_progress_.end()) {
    PERFETTO_ELOG("FinishDataSourceFlush id invalid: %" PRIu64, flush_id);
    return;
  }
  size_t& flush_in_progress = it->second;
  if (--flush_in_progress == 0) {
    endpoint_->NotifyFlushComplete(flush_id);
    flushes_in_progress_.erase(flush_id);
  }
}

void HeapprofdProducer::SocketDelegate::OnDisconnect(base::UnixSocket* self) {
  auto it = producer_->pending_processes_.find(self->peer_pid_linux());
  if (it == producer_->pending_processes_.end()) {
    PERFETTO_ELOG("Unexpected disconnect.");
    return;
  }

  if (self == it->second.sock.get())
    producer_->pending_processes_.erase(it);
}

void HeapprofdProducer::SocketDelegate::OnNewIncomingConnection(
    base::UnixSocket*,
    std::unique_ptr<base::UnixSocket> new_connection) {
  Process peer_process;
  peer_process.pid = new_connection->peer_pid_linux();
  if (!GetCmdlineForPID(peer_process.pid, &peer_process.cmdline))
    PERFETTO_PLOG("Failed to get cmdline for %d", peer_process.pid);

  producer_->HandleClientConnection(std::move(new_connection), peer_process);
}

void HeapprofdProducer::SocketDelegate::OnDataAvailable(
    base::UnixSocket* self) {
  auto it = producer_->pending_processes_.find(self->peer_pid_linux());
  if (it == producer_->pending_processes_.end()) {
    PERFETTO_ELOG("Unexpected data.");
    return;
  }

  PendingProcess& pending_process = it->second;

  base::ScopedFile fds[kHandshakeSize];
  char buf[1];
  self->Receive(buf, sizeof(buf), fds, base::ArraySize(fds));

  static_assert(kHandshakeSize == 2, "change if and else if below.");
  if (fds[kHandshakeMaps] && fds[kHandshakeMem]) {
    auto ds_it =
        producer_->data_sources_.find(pending_process.data_source_instance_id);
    if (ds_it == producer_->data_sources_.end()) {
      producer_->pending_processes_.erase(it);
      return;
    }
    DataSource& data_source = ds_it->second;

    if (data_source.shutting_down) {
      producer_->pending_processes_.erase(it);
      PERFETTO_LOG("Got handshake for DS that is shutting down. Rejecting.");
      return;
    }

    std::string maps_file =
        "/proc/" + std::to_string(self->peer_pid_linux()) + "/maps";
    if (!IsFile(*fds[kHandshakeMaps], maps_file.c_str())) {
      producer_->pending_processes_.erase(it);
      PERFETTO_ELOG("Received invalid maps FD.");
      return;
    }

    std::string mem_file =
        "/proc/" + std::to_string(self->peer_pid_linux()) + "/mem";
    if (!IsFile(*fds[kHandshakeMem], mem_file.c_str())) {
      producer_->pending_processes_.erase(it);
      PERFETTO_ELOG("Received invalid mem FD.");
      return;
    }

    data_source.process_states.emplace(
        std::piecewise_construct, std::forward_as_tuple(self->peer_pid_linux()),
        std::forward_as_tuple(&producer_->callsites_,
                              data_source.config.dump_at_max()));

    PERFETTO_DLOG("%d: Received FDs.", self->peer_pid_linux());
    int raw_fd = pending_process.shmem.fd();
    // TODO(fmayer): Full buffer could deadlock us here.
    if (!self->Send(&data_source.client_configuration,
                    sizeof(data_source.client_configuration), &raw_fd, 1)) {
      // If Send fails, the socket will have been Shutdown, and the raw socket
      // closed.
      producer_->pending_processes_.erase(it);
      return;
    }

    UnwindingWorker::HandoffData handoff_data;
    handoff_data.data_source_instance_id =
        pending_process.data_source_instance_id;
    handoff_data.sock = self->ReleaseSocket();
    handoff_data.maps_fd = std::move(fds[kHandshakeMaps]);
    handoff_data.mem_fd = std::move(fds[kHandshakeMem]);
    handoff_data.shmem = std::move(pending_process.shmem);
    handoff_data.client_config = data_source.client_configuration;
    handoff_data.stream_allocations = data_source.config.stream_allocations();

    producer_->UnwinderForPID(self->peer_pid_linux())
        .PostHandoffSocket(std::move(handoff_data));
    producer_->pending_processes_.erase(it);
  } else if (fds[kHandshakeMaps] || fds[kHandshakeMem]) {
    PERFETTO_ELOG("%d: Received partial FDs.", self->peer_pid_linux());
    producer_->pending_processes_.erase(it);
  } else {
    PERFETTO_ELOG("%d: Received no FDs.", self->peer_pid_linux());
  }
}

HeapprofdProducer::DataSource* HeapprofdProducer::GetDataSourceForProcess(
    const Process& proc) {
  for (auto& ds_id_and_datasource : data_sources_) {
    DataSource& ds = ds_id_and_datasource.second;
    if (ConfigTargetsProcess(ds.config, proc, ds.normalized_cmdlines))
      return &ds;
  }
  return nullptr;
}

void HeapprofdProducer::RecordOtherSourcesAsRejected(DataSource* active_ds,
                                                     const Process& proc) {
  for (auto& ds_id_and_datasource : data_sources_) {
    DataSource& ds = ds_id_and_datasource.second;
    if (&ds != active_ds &&
        ConfigTargetsProcess(ds.config, proc, ds.normalized_cmdlines))
      ds.rejected_pids.emplace(proc.pid);
  }
}

void HeapprofdProducer::HandleClientConnection(
    std::unique_ptr<base::UnixSocket> new_connection,
    Process process) {
  DataSource* data_source = GetDataSourceForProcess(process);
  if (!data_source) {
    PERFETTO_LOG("No data source found.");
    return;
  }
  RecordOtherSourcesAsRejected(data_source, process);

  // In fork mode, right now we check whether the target is not profileable
  // in the client, because we cannot read packages.list there.
  if (mode_ == HeapprofdMode::kCentral &&
      !CanProfile(data_source->ds_config, new_connection->peer_uid_posix(),
                  data_source->config.target_installed_by())) {
    PERFETTO_ELOG("%d (%s) is not profileable.", process.pid,
                  process.cmdline.c_str());
    return;
  }

  uint64_t shmem_size = data_source->config.shmem_size_bytes();
  if (!shmem_size)
    shmem_size = kDefaultShmemSize;
  if (shmem_size > kMaxShmemSize) {
    PERFETTO_LOG("Specified shared memory size of %" PRIu64
                 " exceeds maximum size of %" PRIu64 ". Reducing.",
                 shmem_size, kMaxShmemSize);
    shmem_size = kMaxShmemSize;
  }

  auto shmem = SharedRingBuffer::Create(static_cast<size_t>(shmem_size));
  if (!shmem || !shmem->is_valid()) {
    PERFETTO_LOG("Failed to create shared memory.");
    return;
  }

  pid_t peer_pid = new_connection->peer_pid_linux();
  if (peer_pid != process.pid) {
    PERFETTO_ELOG("Invalid PID connected.");
    return;
  }

  PendingProcess pending_process;
  pending_process.sock = std::move(new_connection);
  pending_process.data_source_instance_id = data_source->id;
  pending_process.shmem = std::move(*shmem);
  pending_processes_.emplace(peer_pid, std::move(pending_process));
}

void HeapprofdProducer::PostAllocRecord(
    UnwindingWorker* worker,
    std::unique_ptr<AllocRecord> alloc_rec) {
  // Once we can use C++14, this should be std::moved into the lambda instead.
  auto* raw_alloc_rec = alloc_rec.release();
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this, raw_alloc_rec, worker] {
    std::unique_ptr<AllocRecord> unique_alloc_ref =
        std::unique_ptr<AllocRecord>(raw_alloc_rec);
    if (weak_this) {
      weak_this->HandleAllocRecord(unique_alloc_ref.get());
      worker->ReturnAllocRecord(std::move(unique_alloc_ref));
    }
  });
}

void HeapprofdProducer::PostFreeRecord(UnwindingWorker*,
                                       std::vector<FreeRecord> free_recs) {
  // Once we can use C++14, this should be std::moved into the lambda instead.
  std::vector<FreeRecord>* raw_free_recs =
      new std::vector<FreeRecord>(std::move(free_recs));
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this, raw_free_recs] {
    if (weak_this) {
      for (FreeRecord& free_rec : *raw_free_recs)
        weak_this->HandleFreeRecord(std::move(free_rec));
    }
    delete raw_free_recs;
  });
}

void HeapprofdProducer::PostHeapNameRecord(UnwindingWorker*,
                                           HeapNameRecord rec) {
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this, rec] {
    if (weak_this)
      weak_this->HandleHeapNameRecord(rec);
  });
}

void HeapprofdProducer::PostSocketDisconnected(UnwindingWorker*,
                                               DataSourceInstanceID ds_id,
                                               pid_t pid,
                                               SharedRingBuffer::Stats stats) {
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this, ds_id, pid, stats] {
    if (weak_this)
      weak_this->HandleSocketDisconnected(ds_id, pid, stats);
  });
}

void HeapprofdProducer::HandleAllocRecord(AllocRecord* alloc_rec) {
  const AllocMetadata& alloc_metadata = alloc_rec->alloc_metadata;
  auto it = data_sources_.find(alloc_rec->data_source_instance_id);
  if (it == data_sources_.end()) {
    PERFETTO_LOG("Invalid data source in alloc record.");
    return;
  }

  DataSource& ds = it->second;
  auto process_state_it = ds.process_states.find(alloc_rec->pid);
  if (process_state_it == ds.process_states.end()) {
    PERFETTO_LOG("Invalid PID in alloc record.");
    return;
  }

  if (ds.config.stream_allocations()) {
    auto packet = ds.trace_writer->NewTracePacket();
    auto* streaming_alloc = packet->set_streaming_allocation();
    streaming_alloc->add_address(alloc_metadata.alloc_address);
    streaming_alloc->add_size(alloc_metadata.alloc_size);
    streaming_alloc->add_sample_size(alloc_metadata.sample_size);
    streaming_alloc->add_clock_monotonic_coarse_timestamp(
        alloc_metadata.clock_monotonic_coarse_timestamp);
    streaming_alloc->add_heap_id(alloc_metadata.heap_id);
    streaming_alloc->add_sequence_number(alloc_metadata.sequence_number);
    return;
  }

  const auto& prefixes = ds.config.skip_symbol_prefix();
  if (!prefixes.empty()) {
    for (unwindstack::FrameData& frame_data : alloc_rec->frames) {
      if (frame_data.map_info == nullptr) {
        continue;
      }
      const std::string& map = frame_data.map_info->name();
      if (std::find_if(prefixes.cbegin(), prefixes.cend(),
                       [&map](const std::string& prefix) {
                         return base::StartsWith(map, prefix);
                       }) != prefixes.cend()) {
        frame_data.function_name = "FILTERED";
      }
    }
  }

  ProcessState& process_state = process_state_it->second;
  HeapTracker& heap_tracker =
      process_state.GetHeapTracker(alloc_rec->alloc_metadata.heap_id);

  if (alloc_rec->error)
    process_state.unwinding_errors++;
  if (alloc_rec->reparsed_map)
    process_state.map_reparses++;
  process_state.heap_samples++;
  process_state.unwinding_time_us.Add(alloc_rec->unwinding_time_us);
  process_state.total_unwinding_time_us += alloc_rec->unwinding_time_us;

  // abspc may no longer refer to the same functions, as we had to reparse
  // maps. Reset the cache.
  if (alloc_rec->reparsed_map)
    heap_tracker.ClearFrameCache();

  heap_tracker.RecordMalloc(
      alloc_rec->frames, alloc_rec->build_ids, alloc_metadata.alloc_address,
      alloc_metadata.sample_size, alloc_metadata.alloc_size,
      alloc_metadata.sequence_number,
      alloc_metadata.clock_monotonic_coarse_timestamp);
}

void HeapprofdProducer::HandleFreeRecord(FreeRecord free_rec) {
  auto it = data_sources_.find(free_rec.data_source_instance_id);
  if (it == data_sources_.end()) {
    PERFETTO_LOG("Invalid data source in free record.");
    return;
  }

  DataSource& ds = it->second;
  auto process_state_it = ds.process_states.find(free_rec.pid);
  if (process_state_it == ds.process_states.end()) {
    PERFETTO_LOG("Invalid PID in free record.");
    return;
  }

  if (ds.config.stream_allocations()) {
    auto packet = ds.trace_writer->NewTracePacket();
    auto* streaming_free = packet->set_streaming_free();
    streaming_free->add_address(free_rec.entry.addr);
    streaming_free->add_heap_id(free_rec.entry.heap_id);
    streaming_free->add_sequence_number(free_rec.entry.sequence_number);
    return;
  }

  ProcessState& process_state = process_state_it->second;

  const FreeEntry& entry = free_rec.entry;
  HeapTracker& heap_tracker = process_state.GetHeapTracker(entry.heap_id);
  heap_tracker.RecordFree(entry.addr, entry.sequence_number, 0);
}

void HeapprofdProducer::HandleHeapNameRecord(HeapNameRecord rec) {
  auto it = data_sources_.find(rec.data_source_instance_id);
  if (it == data_sources_.end()) {
    PERFETTO_LOG("Invalid data source in free record.");
    return;
  }

  DataSource& ds = it->second;
  auto process_state_it = ds.process_states.find(rec.pid);
  if (process_state_it == ds.process_states.end()) {
    PERFETTO_LOG("Invalid PID in free record.");
    return;
  }

  ProcessState& process_state = process_state_it->second;
  const HeapName& entry = rec.entry;
  if (entry.heap_name[0] != '\0') {
    std::string heap_name = entry.heap_name;
    if (entry.heap_id == 0) {
      PERFETTO_ELOG("Invalid zero heap ID.");
      return;
    }
    ProcessState::HeapInfo& hi = process_state.GetHeapInfo(entry.heap_id);
    if (!hi.heap_name.empty() && hi.heap_name != heap_name) {
      PERFETTO_ELOG("Overriding heap name %s with %s", hi.heap_name.c_str(),
                    heap_name.c_str());
    }
    hi.heap_name = entry.heap_name;
  }
  if (entry.sample_interval != 0) {
    ProcessState::HeapInfo& hi = process_state.GetHeapInfo(entry.heap_id);
    if (!hi.sampling_interval)
      hi.orig_sampling_interval = entry.sample_interval;
    hi.sampling_interval = entry.sample_interval;
  }
}

void HeapprofdProducer::TerminateWhenDone() {
  if (data_sources_.empty())
    TerminateProcess(0);
  exit_when_done_ = true;
}

bool HeapprofdProducer::MaybeFinishDataSource(DataSource* ds) {
  if (!ds->process_states.empty() || !ds->rejected_pids.empty() ||
      !ds->shutting_down) {
    return false;
  }

  bool was_stopped = ds->was_stopped;
  DataSourceInstanceID ds_id = ds->id;
  auto weak_producer = weak_factory_.GetWeakPtr();
  bool exit_when_done = exit_when_done_;
  ds->trace_writer->Flush([weak_producer, exit_when_done, ds_id, was_stopped] {
    if (!weak_producer)
      return;

    if (was_stopped)
      weak_producer->endpoint_->NotifyDataSourceStopped(ds_id);
    weak_producer->data_sources_.erase(ds_id);

    if (exit_when_done) {
      // Post this as a task to allow NotifyDataSourceStopped to post tasks.
      weak_producer->task_runner_->PostTask([weak_producer] {
        if (!weak_producer)
          return;
        weak_producer->TerminateProcess(
            /*exit_status=*/0);  // does not return
      });
    }
  });
  return true;
}

void HeapprofdProducer::HandleSocketDisconnected(
    DataSourceInstanceID ds_id,
    pid_t pid,
    SharedRingBuffer::Stats stats) {
  auto it = data_sources_.find(ds_id);
  if (it == data_sources_.end())
    return;
  DataSource& ds = it->second;

  auto process_state_it = ds.process_states.find(pid);
  if (process_state_it == ds.process_states.end()) {
    PERFETTO_ELOG("Unexpected disconnect from %d", pid);
    return;
  }

  PERFETTO_LOG("%d disconnected from heapprofd (ds shutting down: %d).", pid,
               ds.shutting_down);

  ProcessState& process_state = process_state_it->second;
  process_state.disconnected = !ds.shutting_down;
  process_state.error_state = stats.error_state;
  process_state.client_spinlock_blocked_us = stats.client_spinlock_blocked_us;
  process_state.buffer_corrupted =
      stats.num_writes_corrupt > 0 || stats.num_reads_corrupt > 0;

  DumpProcessState(&ds, pid, &process_state);
  ds.process_states.erase(pid);
  MaybeFinishDataSource(&ds);
}

void HeapprofdProducer::CheckDataSourceCpuTask() {
  auto weak_producer = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_producer] {
        if (!weak_producer)
          return;
        weak_producer->CheckDataSourceCpuTask();
      },
      kGuardrailIntervalMs);

  ProfilerCpuGuardrails gr;
  for (auto& p : data_sources_) {
    DataSource& ds = p.second;
    if (gr.IsOverCpuThreshold(ds.guardrail_config)) {
      ds.hit_guardrail = true;
      PERFETTO_LOG("Data source %" PRIu64 " hit CPU guardrail. Shutting down.",
                   ds.id);
      ShutdownDataSource(&ds);
    }
  }
}

void HeapprofdProducer::CheckDataSourceMemoryTask() {
  auto weak_producer = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_producer] {
        if (!weak_producer)
          return;
        weak_producer->CheckDataSourceMemoryTask();
      },
      kGuardrailIntervalMs);
  ProfilerMemoryGuardrails gr;
  for (auto& p : data_sources_) {
    DataSource& ds = p.second;
    if (gr.IsOverMemoryThreshold(ds.guardrail_config)) {
      ds.hit_guardrail = true;
      PERFETTO_LOG("Data source %" PRIu64
                   " hit memory guardrail. Shutting down.",
                   ds.id);
      ShutdownDataSource(&ds);
    }
  }
}

}  // namespace profiling
}  // namespace perfetto
