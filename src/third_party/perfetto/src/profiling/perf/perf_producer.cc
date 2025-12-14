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

#include "src/profiling/perf/perf_producer.h"

#include <optional>
#include <random>
#include <utility>
#include <vector>

#include <unistd.h>

#include <unwindstack/Error.h>
#include <unwindstack/Unwinder.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/metatrace.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/tracing_service.h"
#include "perfetto/ext/tracing/ipc/producer_ipc_client.h"
#include "perfetto/public/compiler.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "src/profiling/common/callstack_trie.h"
#include "src/profiling/common/proc_cmdline.h"
#include "src/profiling/common/producer_support.h"
#include "src/profiling/common/profiler_guardrails.h"
#include "src/profiling/perf/common_types.h"
#include "src/profiling/perf/event_config.h"
#include "src/profiling/perf/event_reader.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/common/perf_events.pbzero.h"
#include "protos/perfetto/config/profiling/perf_event_config.gen.h"
#include "protos/perfetto/trace/profiling/profile_packet.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"

namespace perfetto {
namespace profiling {
namespace {

// TODO(b/151835887): on Android, when using signals, there exists a vulnerable
// window between a process image being replaced by execve, and the new
// libc instance reinstalling the proper signal handlers. During this window,
// the signal disposition is defaulted to terminating the process.
// This is a best-effort mitigation from the daemon's side, using a heuristic
// that most execve calls follow a fork. So if we get a sample for a very fresh
// process, the grace period will give it a chance to get to
// a properly initialised state prior to getting signalled. This doesn't help
// cases when a mature process calls execve, or when the target gets descheduled
// (since this is a naive walltime wait).
// The proper fix is in the platform, see bug for progress.
constexpr uint32_t kProcDescriptorsAndroidDelayMs = 50;

constexpr uint32_t kMemoryLimitCheckPeriodMs = 1000;

constexpr uint32_t kInitialConnectionBackoffMs = 100;
constexpr uint32_t kMaxConnectionBackoffMs = 30 * 1000;

constexpr char kProducerName[] = "perfetto.traced_perf";
constexpr char kDataSourceName[] = "linux.perf";

uint32_t NumberOfCpus() {
  return static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_CONF));
}

bool IsCpuOnline(uint32_t cpu) {
  base::StackString<128> path("/sys/devices/system/cpu/cpu%" PRIu32 "/online",
                              cpu);
  // Always-on CPUs do not have an "online" attribute so treat an absent |path|
  // as online.
  if (!base::FileExists(path.c_str())) {
    return true;
  }
  std::string res;
  if (!base::ReadFile(path.c_str(), &res)) {
    return false;
  }
  return base::StartsWith(res, "1");
}

// TODO(rsavitski): one thing that perf tool does is consult the cpumask
// from the sysfs pmu description (/sys/bus/event_source/.../cpumask) to
// automatically downscope events to the cpus that they're present on (matters
// for heterogeneous cores and ppmu/uncore events).
// This lets users use "perf record -a" without worrying about cpu scopes.
std::vector<uint32_t> CreateCpuMask(const protos::gen::PerfEventConfig& cfg) {
  const auto& target_cpus_raw = cfg.target_cpu();
  std::set<uint32_t> target_cpus(target_cpus_raw.begin(),
                                 target_cpus_raw.end());

  std::vector<uint32_t> ret;
  uint32_t num_cpus = NumberOfCpus();
  for (uint32_t cpu = 0; cpu < num_cpus; cpu++) {
    // check explicit mask from cfg, or allow all by default
    if (!target_cpus.empty() && target_cpus.count(cpu) == 0)
      continue;

    if (!IsCpuOnline(cpu))
      continue;
    ret.push_back(cpu);
  }
  return ret;
}

int32_t ToBuiltinClock(int32_t clockid) {
  switch (clockid) {
    case CLOCK_REALTIME:
      return protos::pbzero::BUILTIN_CLOCK_REALTIME;
    case CLOCK_MONOTONIC:
      return protos::pbzero::BUILTIN_CLOCK_MONOTONIC;
    case CLOCK_MONOTONIC_RAW:
      return protos::pbzero::BUILTIN_CLOCK_MONOTONIC_RAW;
    case CLOCK_BOOTTIME:
      return protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
    // Should never get invalid input here as otherwise the syscall itself
    // would've failed earlier.
    default:
      return protos::pbzero::BUILTIN_CLOCK_UNKNOWN;
  }
}

TraceWriter::TracePacketHandle StartTracePacket(TraceWriter* trace_writer) {
  auto packet = trace_writer->NewTracePacket();
  packet->set_sequence_flags(
      protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
  return packet;
}

void WritePerfEventDefaultsPacket(const EventConfig& event_config,
                                  TraceWriter* trace_writer) {
  using PE = protos::pbzero::PerfEvents;

  auto packet = trace_writer->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  packet->set_timestamp_clock_id(protos::pbzero::BUILTIN_CLOCK_BOOTTIME);

  // start new incremental state generation:
  packet->set_sequence_flags(
      protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);

  // default packet timestamp clock for the samples:
  perf_event_attr* perf_attr = event_config.perf_attr();
  auto* defaults = packet->set_trace_packet_defaults();

  bool polling_mode = event_config.recording_mode() == RecordingMode::kPolling;
  if (polling_mode) {
    // In polling mode, we snapshot the counters ourselves, always using
    // BOOTTIME as that's the typical default for perfetto tracing.
    defaults->set_timestamp_clock_id(protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
  } else {
    int32_t builtin_clock = ToBuiltinClock(perf_attr->clockid);
    defaults->set_timestamp_clock_id(static_cast<uint32_t>(builtin_clock));
  }

  const PerfCounter& timebase = event_config.timebase_event();
  auto* perf_defaults = defaults->set_perf_sample_defaults();
  auto* timebase_pb = perf_defaults->set_timebase();

  // Polling period, or actual sampling frequency/period passed to the kernel:
  if (polling_mode) {
    timebase_pb->set_poll_period_ms(event_config.read_tick_period_ms());
  } else if (perf_attr->freq) {
    timebase_pb->set_frequency(perf_attr->sample_freq);
  } else {
    timebase_pb->set_period(perf_attr->sample_period);
  }

  auto write_event = [](const PerfCounter& c, auto* pb) {
    switch (c.event_type()) {
      case PerfCounter::Type::kBuiltinCounter: {
        pb->set_counter(static_cast<PE::Counter>(c.counter));
        break;
      }
      case PerfCounter::Type::kTracepoint: {
        auto* tracepoint_pb = pb->set_tracepoint();
        tracepoint_pb->set_name(c.tracepoint_name);
        tracepoint_pb->set_filter(c.tracepoint_filter);
        break;
      }
      case PerfCounter::Type::kRawEvent: {
        auto* raw_pb = pb->set_raw_event();
        raw_pb->set_type(c.attr_type);
        raw_pb->set_config(c.attr_config);
        raw_pb->set_config1(c.attr_config1);
        raw_pb->set_config2(c.attr_config2);
        break;
      }
    }

    // optional name to identify the counter during parsing:
    if (!c.name.empty()) {
      pb->set_name(c.name);
    }

    // Write the counting scope modifiers (e.g. count only while in userspace)
    // only if at least one is set.
    if (c.attr_exclude_user || c.attr_exclude_kernel || c.attr_exclude_hv) {
      if (!c.attr_exclude_user) {
        pb->add_modifiers(PE::EVENT_MODIFIER_COUNT_USERSPACE);
      }
      if (!c.attr_exclude_kernel) {
        pb->add_modifiers(PE::EVENT_MODIFIER_COUNT_KERNEL);
      }
      if (!c.attr_exclude_hv) {
        pb->add_modifiers(PE::EVENT_MODIFIER_COUNT_HYPERVISOR);
      }
    }
  };

  // timebase (leader) event:
  write_event(timebase, timebase_pb);

  // follower events:
  for (const auto& e : event_config.follower_events()) {
    auto* followers_pb = perf_defaults->add_followers();
    write_event(e, followers_pb);
  }

  // Not setting timebase.timestamp_clock since the field that matters during
  // parsing is the root timestamp_clock_id set above.

  // Niche: record the random shard we've chosen so that the post-processing can
  // infer which processes would've been unwound if sampled. In particular this
  // lets us distinguish between "running but not chosen" and "running and
  // chosen, but not sampled" cases.
  const auto& process_sharding = event_config.filter().process_sharding;
  if (process_sharding.has_value()) {
    perf_defaults->set_process_shard_count(process_sharding->shard_count);
    perf_defaults->set_chosen_process_shard(process_sharding->chosen_shard);
  }
}

uint32_t TimeToNextReadTickMs(DataSourceInstanceID ds_id, uint32_t period_ms) {
  // Normally, we'd schedule the next tick at the next |period_ms|
  // boundary of the boot clock. However, to avoid aligning the read tasks of
  // all concurrent data sources, we select a deterministic offset based on the
  // data source id.
  std::minstd_rand prng(static_cast<std::minstd_rand::result_type>(ds_id));
  std::uniform_int_distribution<uint32_t> dist(0, period_ms - 1);
  uint32_t ds_period_offset = dist(prng);

  uint64_t now_ms = static_cast<uint64_t>(base::GetWallTimeMs().count());
  return period_ms - ((now_ms - ds_period_offset) % period_ms);
}

protos::pbzero::Profiling::CpuMode ToCpuModeEnum(uint16_t perf_cpu_mode) {
  using Profiling = protos::pbzero::Profiling;
  switch (perf_cpu_mode) {
    case PERF_RECORD_MISC_KERNEL:
      return Profiling::MODE_KERNEL;
    case PERF_RECORD_MISC_USER:
      return Profiling::MODE_USER;
    case PERF_RECORD_MISC_HYPERVISOR:
      return Profiling::MODE_HYPERVISOR;
    case PERF_RECORD_MISC_GUEST_KERNEL:
      return Profiling::MODE_GUEST_KERNEL;
    case PERF_RECORD_MISC_GUEST_USER:
      return Profiling::MODE_GUEST_USER;
    default:
      return Profiling::MODE_UNKNOWN;
  }
}

protos::pbzero::Profiling::StackUnwindError ToProtoEnum(
    unwindstack::ErrorCode error_code) {
  using Profiling = protos::pbzero::Profiling;
  switch (error_code) {
    case unwindstack::ERROR_NONE:
      return Profiling::UNWIND_ERROR_NONE;
    case unwindstack::ERROR_MEMORY_INVALID:
      return Profiling::UNWIND_ERROR_MEMORY_INVALID;
    case unwindstack::ERROR_UNWIND_INFO:
      return Profiling::UNWIND_ERROR_UNWIND_INFO;
    case unwindstack::ERROR_UNSUPPORTED:
      return Profiling::UNWIND_ERROR_UNSUPPORTED;
    case unwindstack::ERROR_INVALID_MAP:
      return Profiling::UNWIND_ERROR_INVALID_MAP;
    case unwindstack::ERROR_MAX_FRAMES_EXCEEDED:
      return Profiling::UNWIND_ERROR_MAX_FRAMES_EXCEEDED;
    case unwindstack::ERROR_REPEATED_FRAME:
      return Profiling::UNWIND_ERROR_REPEATED_FRAME;
    case unwindstack::ERROR_INVALID_ELF:
      return Profiling::UNWIND_ERROR_INVALID_ELF;
    case unwindstack::ERROR_SYSTEM_CALL:
      return Profiling::UNWIND_ERROR_SYSTEM_CALL;
    case unwindstack::ERROR_THREAD_TIMEOUT:
      return Profiling::UNWIND_ERROR_THREAD_TIMEOUT;
    case unwindstack::ERROR_THREAD_DOES_NOT_EXIST:
      return Profiling::UNWIND_ERROR_THREAD_DOES_NOT_EXIST;
    case unwindstack::ERROR_BAD_ARCH:
      return Profiling::UNWIND_ERROR_BAD_ARCH;
    case unwindstack::ERROR_MAPS_PARSE:
      return Profiling::UNWIND_ERROR_MAPS_PARSE;
    case unwindstack::ERROR_INVALID_PARAMETER:
      return Profiling::UNWIND_ERROR_INVALID_PARAMETER;
    case unwindstack::ERROR_PTRACE_CALL:
      return Profiling::UNWIND_ERROR_PTRACE_CALL;
  }
  return Profiling::UNWIND_ERROR_UNKNOWN;
}

}  // namespace

// static
bool PerfProducer::ShouldRejectDueToFilter(
    pid_t pid,
    const TargetFilter& filter,
    bool skip_cmdline,
    base::FlatSet<std::string>* additional_cmdlines,
    std::function<bool(std::string*)> read_proc_pid_cmdline) {
  PERFETTO_CHECK(additional_cmdlines);

  std::string cmdline;
  bool have_cmdline = false;
  if (!skip_cmdline)
    have_cmdline = read_proc_pid_cmdline(&cmdline);

  const char* binname = "";
  if (have_cmdline) {
    binname = glob_aware::FindBinaryName(cmdline.c_str(), cmdline.size());
  }

  auto has_matching_pattern = [](const std::vector<std::string>& patterns,
                                 const char* cmd, const char* name) {
    for (const std::string& pattern : patterns) {
      if (glob_aware::MatchGlobPattern(pattern.c_str(), cmd, name)) {
        return true;
      }
    }
    return false;
  };

  if (have_cmdline &&
      has_matching_pattern(filter.exclude_cmdlines, cmdline.c_str(), binname)) {
    PERFETTO_DLOG("Explicitly rejecting samples for pid [%d] due to cmdline",
                  static_cast<int>(pid));
    return true;
  }
  if (filter.exclude_pids.count(pid)) {
    PERFETTO_DLOG("Explicitly rejecting samples for pid [%d] due to pid",
                  static_cast<int>(pid));
    return true;
  }

  if (have_cmdline &&
      has_matching_pattern(filter.cmdlines, cmdline.c_str(), binname)) {
    return false;
  }
  if (filter.pids.count(pid)) {
    return false;
  }

  // Empty allow filter means keep everything that isn't explicitly excluded.
  if (filter.cmdlines.empty() && filter.pids.empty() &&
      !filter.additional_cmdline_count &&
      !filter.process_sharding.has_value()) {
    return false;
  }

  // Niche option: process sharding to amortise systemwide unwinding costs.
  // Selects a subset of all processes by using the low order bits of their pid.
  if (filter.process_sharding.has_value()) {
    uint32_t upid = static_cast<uint32_t>(pid);
    if (upid % filter.process_sharding->shard_count ==
        filter.process_sharding->chosen_shard) {
      PERFETTO_DLOG("Process sharding: keeping pid [%d]",
                    static_cast<int>(pid));
      return false;
    } else {
      PERFETTO_DLOG("Process sharding: rejecting pid [%d]",
                    static_cast<int>(pid));
      return true;
    }
  }

  // Niche option: additionally remember the first seen N process cmdlines, and
  // keep all processes with those names.
  if (have_cmdline) {
    if (additional_cmdlines->count(cmdline)) {
      return false;
    }
    if (additional_cmdlines->size() < filter.additional_cmdline_count) {
      additional_cmdlines->insert(cmdline);
      return false;
    }
  }

  PERFETTO_DLOG("Rejecting samples for pid [%d]", static_cast<int>(pid));
  return true;
}

PerfProducer::PerfProducer(ProcDescriptorGetter* proc_fd_getter,
                           base::TaskRunner* task_runner)
    : task_runner_(task_runner),
      proc_fd_getter_(proc_fd_getter),
      unwinding_worker_(this),
      weak_factory_(this) {
  proc_fd_getter->SetDelegate(this);
}

void PerfProducer::SetupDataSource(DataSourceInstanceID,
                                   const DataSourceConfig&) {}

void PerfProducer::StartDataSource(DataSourceInstanceID ds_id,
                                   const DataSourceConfig& config) {
  uint64_t tracing_session_id = config.tracing_session_id();
  PERFETTO_LOG("StartDataSource(ds %zu, session %" PRIu64 ", name %s)",
               static_cast<size_t>(ds_id), tracing_session_id,
               config.name().c_str());

  if (config.name() == MetatraceWriter::kDataSourceName) {
    StartMetatraceSource(ds_id, static_cast<BufferID>(config.target_buffer()));
    return;
  }

  // linux.perf data source
  if (config.name() != kDataSourceName)
    return;

  // Tracepoint name -> id lookup in case the config asks for tracepoints:
  auto tracepoint_id_lookup = [this](const std::string& group,
                                     const std::string& name) {
    if (!tracefs_)  // lazy init or retry
      tracefs_ = Tracefs::CreateGuessingMountPoint();
    if (!tracefs_)  // still didn't find an accessible tracefs
      return 0u;
    return tracefs_->ReadEventId(group, name);
  };

  protos::gen::PerfEventConfig event_config_pb;
  if (!event_config_pb.ParseFromString(config.perf_event_config_raw())) {
    PERFETTO_ELOG("PerfEventConfig could not be parsed.");
    return;
  }

  // Unlikely: handle a callstack sampling option that shares a random decision
  // between all data sources within a tracing session. Instead of introducing
  // session-scoped data, we replicate the decision in each per-DS EventConfig.
  std::optional<ProcessSharding> process_sharding;
  uint32_t shard_count =
      event_config_pb.callstack_sampling().scope().process_shard_count();
  if (shard_count > 0) {
    process_sharding =
        GetOrChooseCallstackProcessShard(tracing_session_id, shard_count);
  }

  std::optional<EventConfig> event_config = EventConfig::Create(
      event_config_pb, config, process_sharding, tracepoint_id_lookup);
  if (!event_config.has_value()) {
    PERFETTO_ELOG("PerfEventConfig rejected.");
    return;
  }

  std::vector<uint32_t> target_cpus = CreateCpuMask(event_config_pb);
  if (target_cpus.empty()) {
    PERFETTO_ELOG("No valid cpus.");
    return;
  }

  std::vector<EventReader> per_cpu_readers;
  for (uint32_t cpu : target_cpus) {
    std::optional<EventReader> event_reader =
        EventReader::ConfigureEvents(cpu, event_config.value());
    if (!event_reader.has_value()) {
      PERFETTO_ELOG("Failed to set up perf events for cpu%" PRIu32
                    ", discarding data source.",
                    cpu);
      return;
    }
    per_cpu_readers.emplace_back(std::move(event_reader.value()));
  }

  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  auto writer =
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall);

  // Construct the data source instance.
  std::map<DataSourceInstanceID, DataSourceState>::iterator ds_it;
  bool inserted;
  std::tie(ds_it, inserted) = data_sources_.emplace(
      std::piecewise_construct, std::forward_as_tuple(ds_id),
      std::forward_as_tuple(event_config.value(), tracing_session_id,
                            std::move(writer), std::move(per_cpu_readers)));
  PERFETTO_CHECK(inserted);
  DataSourceState& ds = ds_it->second;

  // Start the configured events.
  for (auto& per_cpu_reader : ds.per_cpu_readers) {
    per_cpu_reader.EnableEvents();
  }

  WritePerfEventDefaultsPacket(ds.event_config, ds.trace_writer.get());

  // Enqueue the periodic read task.
  auto tick_period_ms = ds.event_config.read_tick_period_ms();
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this, ds_id] {
        if (weak_this)
          weak_this->TickDataSourceRead(ds_id);
      },
      TimeToNextReadTickMs(ds_id, tick_period_ms));

  // Polled counters: done with setup.
  if (event_config->recording_mode() == RecordingMode::kPolling) {
    return;
  }

  // Additional setup for sampling mode.

  InterningOutputTracker::WriteFixedInterningsPacket(
      ds_it->second.trace_writer.get(),
      protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);

  // Inform unwinder of the new data source instance, and optionally start a
  // periodic task to clear its cached state.
  auto unwind_mode = (ds.event_config.unwind_mode() ==
                      protos::gen::PerfEventConfig::UNWIND_FRAME_POINTER)
                         ? Unwinder::UnwindMode::kFramePointer
                         : Unwinder::UnwindMode::kUnwindStack;
  unwinding_worker_->PostStartDataSource(ds_id, ds.event_config.kernel_frames(),
                                         unwind_mode);
  if (ds.event_config.unwind_state_clear_period_ms()) {
    unwinding_worker_->PostClearCachedStatePeriodic(
        ds_id, ds.event_config.unwind_state_clear_period_ms());
  }

  // Optionally kick off periodic memory footprint limit check.
  uint32_t max_daemon_memory_kb = event_config_pb.max_daemon_memory_kb();
  if (max_daemon_memory_kb > 0) {
    task_runner_->PostDelayedTask(
        [weak_this, ds_id, max_daemon_memory_kb] {
          if (weak_this)
            weak_this->CheckMemoryFootprintPeriodic(ds_id,
                                                    max_daemon_memory_kb);
        },
        kMemoryLimitCheckPeriodMs);
  }
}

void PerfProducer::CheckMemoryFootprintPeriodic(DataSourceInstanceID ds_id,
                                                uint32_t max_daemon_memory_kb) {
  auto ds_it = data_sources_.find(ds_id);
  if (ds_it == data_sources_.end())
    return;  // stop recurring

  GuardrailConfig gconfig = {};
  gconfig.memory_guardrail_kb = max_daemon_memory_kb;

  ProfilerMemoryGuardrails footprint_snapshot;
  if (footprint_snapshot.IsOverMemoryThreshold(gconfig)) {
    PurgeDataSource(ds_id);
    return;  // stop recurring
  }

  // repost
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this, ds_id, max_daemon_memory_kb] {
        if (weak_this)
          weak_this->CheckMemoryFootprintPeriodic(ds_id, max_daemon_memory_kb);
      },
      kMemoryLimitCheckPeriodMs);
}

void PerfProducer::StopDataSource(DataSourceInstanceID ds_id) {
  PERFETTO_LOG("StopDataSource(%zu)", static_cast<size_t>(ds_id));

  // Metatrace: stop immediately (will miss the events from the
  // asynchronous shutdown of the primary data source).
  auto meta_it = metatrace_writers_.find(ds_id);
  if (meta_it != metatrace_writers_.end()) {
    meta_it->second.WriteAllAndFlushTraceWriter([] {});
    metatrace_writers_.erase(meta_it);
    return;
  }

  auto ds_it = data_sources_.find(ds_id);
  if (ds_it == data_sources_.end()) {
    // Most likely, the source is missing due to an abrupt stop (via
    // |PurgeDataSource|). Tell the service that we've stopped the source now,
    // so that it doesn't wait for the ack until the timeout.
    endpoint_->NotifyDataSourceStopped(ds_id);
    return;
  }
  DataSourceState& ds = ds_it->second;

  if (ds.event_config.recording_mode() == RecordingMode::kPolling) {
    // Polling mode: emit a final reading and ack the stop.
    ReadCounters(ds);
    endpoint_->NotifyDataSourceStopped(ds_id);
  } else {
    // Sampling mode: start shutting down the reading frontend, which will
    // propagate the stop further as the intermediate buffers are cleared.
    InitiateReaderStop(&ds);
  }
}

// The perf data sources ignore flush requests, as flushing would be
// unnecessarily complicated given out-of-order unwinding and proc-fd timeouts.
// Instead of responding to explicit flushes, we can ensure that we're otherwise
// well-behaved (do not reorder packets too much), and let the service scrape
// the SMB.
void PerfProducer::Flush(FlushRequestID flush_id,
                         const DataSourceInstanceID* data_source_ids,
                         size_t num_data_sources,
                         FlushFlags) {
  // Flush metatracing if requested.
  for (size_t i = 0; i < num_data_sources; i++) {
    auto ds_id = data_source_ids[i];
    PERFETTO_DLOG("Flush(%zu)", static_cast<size_t>(ds_id));

    auto meta_it = metatrace_writers_.find(ds_id);
    if (meta_it != metatrace_writers_.end()) {
      meta_it->second.WriteAllAndFlushTraceWriter([] {});
    }
  }

  endpoint_->NotifyFlushComplete(flush_id);
}

void PerfProducer::ClearIncrementalState(
    const DataSourceInstanceID* data_source_ids,
    size_t num_data_sources) {
  for (size_t i = 0; i < num_data_sources; i++) {
    auto ds_id = data_source_ids[i];
    PERFETTO_DLOG("ClearIncrementalState(%zu)", static_cast<size_t>(ds_id));

    if (metatrace_writers_.find(ds_id) != metatrace_writers_.end())
      continue;

    auto ds_it = data_sources_.find(ds_id);
    if (ds_it == data_sources_.end()) {
      PERFETTO_DLOG("ClearIncrementalState(%zu): did not find matching entry",
                    static_cast<size_t>(ds_id));
      continue;
    }
    DataSourceState& ds = ds_it->second;

    WritePerfEventDefaultsPacket(ds.event_config, ds.trace_writer.get());

    // Forget which incremental state we've emitted before.
    ds.interning_output.ClearHistory();
    InterningOutputTracker::WriteFixedInterningsPacket(
        ds.trace_writer.get(),
        protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);

    // Drop the cross-datasource callstack interning trie. This is not
    // necessary for correctness (the preceding step is sufficient). However,
    // incremental clearing is likely to be used in ring buffer traces, where
    // it makes sense to reset the trie's size periodically, and this is a
    // reasonable point to do so. The trie keeps the monotonic interning IDs,
    // so there is no confusion for other concurrent data sources. We do not
    // bother with clearing concurrent sources' interning output trackers as
    // their footprint should be trivial.
    callstack_trie_.ClearTrie();
  }
}

void PerfProducer::TickDataSourceRead(DataSourceInstanceID ds_id) {
  auto it = data_sources_.find(ds_id);
  if (it == data_sources_.end()) {
    PERFETTO_DLOG("TickDataSourceRead(%zu): source gone",
                  static_cast<size_t>(ds_id));
    return;
  }
  DataSourceState& ds = it->second;

  PERFETTO_METATRACE_SCOPED(TAG_PRODUCER, PROFILER_READ_TICK);

  bool repost_tick = true;
  if (ds.event_config.recording_mode() == RecordingMode::kPolling) {
    ReadCounters(ds);
  } else {
    repost_tick = ReadRingBuffers(ds_id, ds);
  }

  if (repost_tick) {
    auto tick_period_ms = it->second.event_config.read_tick_period_ms();
    auto weak_this = weak_factory_.GetWeakPtr();
    task_runner_->PostDelayedTask(
        [weak_this, ds_id] {
          if (weak_this)
            weak_this->TickDataSourceRead(ds_id);
        },
        TimeToNextReadTickMs(ds_id, tick_period_ms));
  }
}

void PerfProducer::ReadCounters(DataSourceState& ds) {
  for (EventReader& reader : ds.per_cpu_readers) {
    std::optional<CommonSampleData> v = reader.ReadCounters();
    if (PERFETTO_LIKELY(v.has_value())) {
      EmitCounterOnlySample(ds, *v, /*has_process_context=*/false);
    }
  }
}

bool PerfProducer::ReadRingBuffers(DataSourceInstanceID ds_id,
                                   DataSourceState& ds) {
  // Make a pass over all per-cpu readers.
  uint64_t max_samples = ds.event_config.samples_per_tick_limit();
  bool more_records_available = false;
  for (EventReader& reader : ds.per_cpu_readers) {
    if (ReadAndParsePerCpuBuffer(&reader, max_samples, ds_id, ds)) {
      more_records_available = true;
    }
  }

  // Wake up the unwinder as we've (likely) pushed samples into its queue.
  unwinding_worker_->PostProcessQueue();

  if (PERFETTO_UNLIKELY(ds.status == DataSourceState::Status::kShuttingDown) &&
      !more_records_available) {
    unwinding_worker_->PostInitiateDataSourceStop(ds_id);
    return false;  // stop reposting the read callback
  }
  return true;  // continue reading
}

bool PerfProducer::ReadAndParsePerCpuBuffer(EventReader* reader,
                                            uint64_t max_samples,
                                            DataSourceInstanceID ds_id,
                                            DataSourceState& ds) {
  PERFETTO_METATRACE_SCOPED(TAG_PRODUCER, PROFILER_READ_CPU);

  // If the kernel ring buffer dropped data, record it in the trace.
  size_t cpu = reader->cpu();
  auto records_lost_callback = [this, ds_id, cpu](uint64_t records_lost) {
    auto weak_this = weak_factory_.GetWeakPtr();
    task_runner_->PostTask([weak_this, ds_id, cpu, records_lost] {
      if (weak_this)
        weak_this->EmitRingBufferLoss(ds_id, cpu, records_lost);
    });
  };

  for (uint64_t i = 0; i < max_samples; i++) {
    std::optional<ParsedSample> sample =
        reader->ReadUntilSample(records_lost_callback);
    if (!sample) {
      return false;  // caught up to the writer
    }

    // Counter-only mode: skip the unwinding stage, serialise the sample
    // immediately.
    const EventConfig& event_config = ds.event_config;
    if (!event_config.sample_callstacks()) {
      EmitCounterOnlySample(ds, sample->common, /*has_process_context=*/true);
      continue;
    }

    // Sampling either or both of userspace and kernel callstacks.
    pid_t pid = sample->common.pid;
    auto& process_state = ds.process_states[pid];  // insert if new

    // Asynchronous proc-fd lookup timed out.
    if (process_state == ProcessTrackingStatus::kFdsTimedOut) {
      PERFETTO_DLOG("Skipping sample for pid [%d]: kFdsTimedOut",
                    static_cast<int>(pid));
      EmitSkippedSample(ds_id, std::move(sample.value()),
                        SampleSkipReason::kReadFdTimeout);
      continue;
    }

    // Previously excluded, e.g. due to failing the target filter check.
    if (process_state == ProcessTrackingStatus::kRejected) {
      PERFETTO_DLOG("Skipping sample for pid [%d]: kRejected",
                    static_cast<int>(pid));
      EmitSkippedSample(ds_id, std::move(sample.value()),
                        SampleSkipReason::kRejected);
      continue;
    }

    // Seeing pid for the first time. We need to consider whether the process
    // is a kernel thread, and which callstacks we're recording.
    //
    // {user} stacks -> user processes: signal for proc-fd lookup
    //               -> kthreads: reject
    //
    // {kernel} stacks -> user processes: accept without proc-fds
    //                 -> kthreads: accept without proc-fds
    //
    // {kernel+user} stacks -> user processes: signal for proc-fd lookup
    //                      -> kthreads: accept without proc-fds
    //
    if (process_state == ProcessTrackingStatus::kInitial) {
      PERFETTO_DLOG("New pid: [%d]", static_cast<int>(pid));

      // Kernel threads (which have no userspace state) are never relevant if
      // we're not recording kernel callchains.
      bool is_kthread = !sample->regs;  // no userspace regs
      if (is_kthread && !event_config.kernel_frames()) {
        process_state = ProcessTrackingStatus::kRejected;
        EmitSkippedSample(ds_id, std::move(sample.value()),
                          SampleSkipReason::kRejected);
        continue;
      }

      // Check whether samples for this new process should be dropped due to
      // the target filtering. Kernel threads don't have a cmdline, but we
      // still check against pid inclusion/exclusion.
      if (ShouldRejectDueToFilter(
              pid, event_config.filter(), is_kthread, &ds.additional_cmdlines,
              [pid](std::string* cmdline) {
                return glob_aware::ReadProcCmdlineForPID(pid, cmdline);
              })) {
        process_state = ProcessTrackingStatus::kRejected;
        EmitSkippedSample(ds_id, std::move(sample.value()),
                          SampleSkipReason::kRejected);
        continue;
      }

      // At this point, sampled process is known to be of interest.
      if (!is_kthread && event_config.user_frames()) {
        // Start resolving the proc-fds. Response is async.
        process_state = ProcessTrackingStatus::kFdsResolving;
        InitiateDescriptorLookup(ds_id, pid,
                                 event_config.remote_descriptor_timeout_ms());
        // note: fallthrough
      } else {
        // Either a kernel thread (no need to obtain proc-fds), or a userspace
        // process but we're not recording userspace callstacks.
        process_state = ProcessTrackingStatus::kAccepted;
        unwinding_worker_->PostRecordNoUserspaceProcess(ds_id, pid);
        // note: fallthrough
      }
    }

    PERFETTO_CHECK(process_state == ProcessTrackingStatus::kAccepted ||
                   process_state == ProcessTrackingStatus::kFdsResolving);

    // If we're only interested in the kernel callchains, then userspace
    // process samples are relevant only if they were sampled during kernel
    // context.
    if (!event_config.user_frames() &&
        sample->common.cpu_mode == PERF_RECORD_MISC_USER) {
      PERFETTO_DLOG("Skipping usermode sample for kernel-only config");
      EmitCounterOnlySample(ds, sample->common, /*has_process_context=*/true);
      continue;
    }

    // Optionally: drop sample if above a given threshold of sampled stacks
    // that are waiting in the unwinding queue.
    uint64_t max_footprint_bytes = event_config.max_enqueued_footprint_bytes();
    uint64_t sample_stack_size = sample->stack.size();
    if (max_footprint_bytes) {
      uint64_t footprint_bytes = unwinding_worker_->GetEnqueuedFootprint();
      if (footprint_bytes + sample_stack_size >= max_footprint_bytes) {
        PERFETTO_DLOG("Skipping sample enqueueing due to footprint limit.");
        EmitSkippedSample(ds_id, std::move(sample.value()),
                          SampleSkipReason::kUnwindEnqueue);
        continue;
      }
    }

    // Push the sample into the unwinding queue if there is room.
    auto& queue = unwinding_worker_->unwind_queue();
    WriteView write_view = queue.BeginWrite();
    if (write_view.valid) {
      queue.at(write_view.write_pos) =
          UnwindEntry{ds_id, std::move(sample.value())};
      queue.CommitWrite();
      unwinding_worker_->IncrementEnqueuedFootprint(sample_stack_size);
    } else {
      PERFETTO_DLOG("Unwinder queue full, skipping sample");
      EmitSkippedSample(ds_id, std::move(sample.value()),
                        SampleSkipReason::kUnwindEnqueue);
    }
  }  // for (i < max_samples)

  // Most likely more events in the kernel buffer. Though we might be exactly on
  // the boundary due to |max_samples|.
  return true;
}

// Note: first-fit makes descriptor request fulfillment not true FIFO. But the
// edge-cases where it matters are very unlikely.
void PerfProducer::OnProcDescriptors(pid_t pid,
                                     uid_t uid,
                                     base::ScopedFile maps_fd,
                                     base::ScopedFile mem_fd) {
  // Find first-fit data source that requested descriptors for the process.
  for (auto& it : data_sources_) {
    DataSourceState& ds = it.second;
    auto proc_status_it = ds.process_states.find(pid);
    if (proc_status_it == ds.process_states.end())
      continue;

    // TODO(rsavitski): consider checking ProcessTrackingStatus before
    // CanProfile.
    if (!CanProfile(ds.event_config.raw_ds_config(), uid,
                    ds.event_config.target_installed_by())) {
      PERFETTO_DLOG("Not profileable: pid [%d], uid [%d] for DS [%zu]",
                    static_cast<int>(pid), static_cast<int>(uid),
                    static_cast<size_t>(it.first));
      continue;
    }

    // Match against either resolving, or expired state. In the latter
    // case, it means that the async response was slow enough that we've marked
    // the lookup as expired (but can now recover for future samples).
    auto proc_status = proc_status_it->second;
    if (proc_status == ProcessTrackingStatus::kFdsResolving ||
        proc_status == ProcessTrackingStatus::kFdsTimedOut) {
      PERFETTO_DLOG("Handing off proc-fds for pid [%d] to DS [%zu]",
                    static_cast<int>(pid), static_cast<size_t>(it.first));

      proc_status_it->second = ProcessTrackingStatus::kAccepted;
      unwinding_worker_->PostAdoptProcDescriptors(
          it.first, pid, std::move(maps_fd), std::move(mem_fd));
      return;  // done
    }
  }
  PERFETTO_DLOG(
      "Discarding proc-fds for pid [%d] as found no outstanding requests.",
      static_cast<int>(pid));
}

void PerfProducer::InitiateDescriptorLookup(DataSourceInstanceID ds_id,
                                            pid_t pid,
                                            uint32_t timeout_ms) {
  if (!proc_fd_getter_->RequiresDelayedRequest()) {
    StartDescriptorLookup(ds_id, pid, timeout_ms);
    return;
  }

  // Delay lookups on Android. See comment on |kProcDescriptorsAndroidDelayMs|.
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this, ds_id, pid, timeout_ms] {
        if (weak_this)
          weak_this->StartDescriptorLookup(ds_id, pid, timeout_ms);
      },
      kProcDescriptorsAndroidDelayMs);
}

void PerfProducer::StartDescriptorLookup(DataSourceInstanceID ds_id,
                                         pid_t pid,
                                         uint32_t timeout_ms) {
  proc_fd_getter_->GetDescriptorsForPid(pid);

  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this, ds_id, pid] {
        if (weak_this)
          weak_this->EvaluateDescriptorLookupTimeout(ds_id, pid);
      },
      timeout_ms);
}

void PerfProducer::EvaluateDescriptorLookupTimeout(DataSourceInstanceID ds_id,
                                                   pid_t pid) {
  auto ds_it = data_sources_.find(ds_id);
  if (ds_it == data_sources_.end())
    return;

  DataSourceState& ds = ds_it->second;
  auto proc_status_it = ds.process_states.find(pid);
  if (proc_status_it == ds.process_states.end())
    return;

  // If the request is still outstanding, mark the process as expired (causing
  // outstanding and future samples to be discarded).
  auto proc_status = proc_status_it->second;
  if (proc_status == ProcessTrackingStatus::kFdsResolving) {
    PERFETTO_DLOG("Descriptor lookup timeout of pid [%d] for DS [%zu]",
                  static_cast<int>(pid), static_cast<size_t>(ds_it->first));

    proc_status_it->second = ProcessTrackingStatus::kFdsTimedOut;
    // Also inform the unwinder of the state change (so that it can discard any
    // of the already-enqueued samples).
    unwinding_worker_->PostRecordTimedOutProcDescriptors(ds_id, pid);
  }
}

void PerfProducer::EmitCounterOnlySample(DataSourceState& ds,
                                         const CommonSampleData& sample,
                                         bool has_process_context) {
  auto packet = StartTracePacket(ds.trace_writer.get());
  packet->set_timestamp(sample.timestamp);

  auto* perf_sample = packet->set_perf_sample();
  perf_sample->set_cpu(sample.cpu);
  perf_sample->set_timebase_count(sample.timebase_count);
  for (uint64_t follower_count : sample.follower_counts) {
    perf_sample->add_follower_counts(follower_count);
  }

  if (has_process_context) {
    perf_sample->set_pid(static_cast<uint32_t>(sample.pid));
    perf_sample->set_tid(static_cast<uint32_t>(sample.tid));
    perf_sample->set_cpu_mode(ToCpuModeEnum(sample.cpu_mode));
  }
}

void PerfProducer::PostEmitSample(DataSourceInstanceID ds_id,
                                  CompletedSample sample) {
  // hack: c++11 lambdas can't be moved into, so stash the sample on the heap.
  CompletedSample* raw_sample = new CompletedSample(std::move(sample));
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this, ds_id, raw_sample] {
    if (weak_this)
      weak_this->EmitSample(ds_id, std::move(*raw_sample));
    delete raw_sample;
  });
}

void PerfProducer::EmitSample(DataSourceInstanceID ds_id,
                              CompletedSample sample) {
  auto ds_it = data_sources_.find(ds_id);
  if (ds_it == data_sources_.end()) {
    PERFETTO_DLOG("EmitSample(ds: %zu): source gone",
                  static_cast<size_t>(ds_id));
    return;
  }
  DataSourceState& ds = ds_it->second;

  // intern callsite
  GlobalCallstackTrie::Node* callstack_root =
      callstack_trie_.CreateCallsite(sample.frames, sample.build_ids);
  uint64_t callstack_iid = callstack_root->id();

  // start packet, timestamp domain defaults to monotonic_raw
  auto packet = StartTracePacket(ds.trace_writer.get());
  packet->set_timestamp(sample.common.timestamp);

  // write new interning data (if any)
  protos::pbzero::InternedData* interned_out = packet->set_interned_data();
  ds.interning_output.WriteCallstack(callstack_root, &callstack_trie_,
                                     interned_out);

  // write the sample itself
  auto* perf_sample = packet->set_perf_sample();
  perf_sample->set_cpu(sample.common.cpu);
  perf_sample->set_pid(static_cast<uint32_t>(sample.common.pid));
  perf_sample->set_tid(static_cast<uint32_t>(sample.common.tid));
  perf_sample->set_cpu_mode(ToCpuModeEnum(sample.common.cpu_mode));
  perf_sample->set_timebase_count(sample.common.timebase_count);
  for (uint64_t follower_count : sample.common.follower_counts) {
    perf_sample->add_follower_counts(follower_count);
  }

  perf_sample->set_callstack_iid(callstack_iid);
  if (sample.unwind_error != unwindstack::ERROR_NONE) {
    perf_sample->set_unwind_error(ToProtoEnum(sample.unwind_error));
  }
}

void PerfProducer::EmitRingBufferLoss(DataSourceInstanceID ds_id,
                                      size_t cpu,
                                      uint64_t records_lost) {
  auto ds_it = data_sources_.find(ds_id);
  if (ds_it == data_sources_.end())
    return;
  DataSourceState& ds = ds_it->second;
  PERFETTO_DLOG("DataSource(%zu): cpu%zu lost [%" PRIu64 "] records",
                static_cast<size_t>(ds_id), cpu, records_lost);

  // The data loss record relates to a single ring buffer, and indicates loss
  // since the last successfully-written record in that buffer. Therefore the
  // data loss record itself has no timestamp.
  // We timestamp the packet with the boot clock for packet ordering purposes,
  // but it no longer has a (precise) interpretation relative to the sample
  // stream from that per-cpu buffer. See the proto comments for more details.
  auto packet = StartTracePacket(ds.trace_writer.get());
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  packet->set_timestamp_clock_id(
      protos::pbzero::BuiltinClock::BUILTIN_CLOCK_BOOTTIME);

  auto* perf_sample = packet->set_perf_sample();
  perf_sample->set_cpu(static_cast<uint32_t>(cpu));
  perf_sample->set_kernel_records_lost(records_lost);
}

void PerfProducer::PostEmitUnwinderSkippedSample(DataSourceInstanceID ds_id,
                                                 ParsedSample sample) {
  PostEmitSkippedSample(ds_id, std::move(sample),
                        SampleSkipReason::kUnwindStage);
}

void PerfProducer::PostEmitSkippedSample(DataSourceInstanceID ds_id,
                                         ParsedSample sample,
                                         SampleSkipReason reason) {
  // hack: c++11 lambdas can't be moved into, so stash the sample on the heap.
  ParsedSample* raw_sample = new ParsedSample(std::move(sample));
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this, ds_id, raw_sample, reason] {
    if (weak_this)
      weak_this->EmitSkippedSample(ds_id, std::move(*raw_sample), reason);
    delete raw_sample;
  });
}

void PerfProducer::EmitSkippedSample(DataSourceInstanceID ds_id,
                                     ParsedSample sample,
                                     SampleSkipReason reason) {
  auto ds_it = data_sources_.find(ds_id);
  if (ds_it == data_sources_.end())
    return;
  DataSourceState& ds = ds_it->second;

  // Note: timestamp defaults to the monotonic_raw domain.
  auto packet = StartTracePacket(ds.trace_writer.get());
  packet->set_timestamp(sample.common.timestamp);
  auto* perf_sample = packet->set_perf_sample();
  perf_sample->set_cpu(sample.common.cpu);
  perf_sample->set_pid(static_cast<uint32_t>(sample.common.pid));
  perf_sample->set_tid(static_cast<uint32_t>(sample.common.tid));
  perf_sample->set_cpu_mode(ToCpuModeEnum(sample.common.cpu_mode));
  perf_sample->set_timebase_count(sample.common.timebase_count);

  for (size_t i = 0; i < sample.common.follower_counts.size(); ++i) {
    perf_sample->add_follower_counts(sample.common.follower_counts[i]);
  }

  using PerfSample = protos::pbzero::PerfSample;
  switch (reason) {
    case SampleSkipReason::kReadFdTimeout:
      perf_sample->set_sample_skipped_reason(
          PerfSample::PROFILER_SKIP_READ_STAGE);
      break;
    case SampleSkipReason::kUnwindEnqueue:
      perf_sample->set_sample_skipped_reason(
          PerfSample::PROFILER_SKIP_UNWIND_ENQUEUE);
      break;
    case SampleSkipReason::kUnwindStage:
      perf_sample->set_sample_skipped_reason(
          PerfSample::PROFILER_SKIP_UNWIND_STAGE);
      break;
    case SampleSkipReason::kRejected:
      perf_sample->set_sample_skipped_reason(
          PerfSample::PROFILER_SKIP_NOT_IN_SCOPE);
      break;
  }
}

void PerfProducer::InitiateReaderStop(DataSourceState* ds) {
  PERFETTO_DLOG("InitiateReaderStop");
  PERFETTO_CHECK(ds->status != DataSourceState::Status::kShuttingDown);

  ds->status = DataSourceState::Status::kShuttingDown;
  for (auto& event_reader : ds->per_cpu_readers) {
    event_reader.DisableEvents();
  }
}

void PerfProducer::PostFinishDataSourceStop(DataSourceInstanceID ds_id) {
  auto weak_producer = weak_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_producer, ds_id] {
    if (weak_producer)
      weak_producer->FinishDataSourceStop(ds_id);
  });
}

void PerfProducer::FinishDataSourceStop(DataSourceInstanceID ds_id) {
  PERFETTO_LOG("FinishDataSourceStop(%zu)", static_cast<size_t>(ds_id));
  auto ds_it = data_sources_.find(ds_id);
  if (ds_it == data_sources_.end()) {
    PERFETTO_DLOG("FinishDataSourceStop(%zu): source gone",
                  static_cast<size_t>(ds_id));
    return;
  }
  DataSourceState& ds = ds_it->second;
  PERFETTO_CHECK(ds.status == DataSourceState::Status::kShuttingDown);

  ds.trace_writer->Flush();
  data_sources_.erase(ds_it);

  endpoint_->NotifyDataSourceStopped(ds_id);

  // Clean up resources if there are no more active sources.
  if (data_sources_.empty()) {
    callstack_trie_.ClearTrie();  // purge internings
    base::MaybeReleaseAllocatorMemToOS();
  }
}

// TODO(rsavitski): maybe make the tracing service respect premature
// producer-driven stops, and then issue a NotifyDataSourceStopped here.
// Alternatively (and at the expense of higher complexity) introduce a new data
// source status of "tombstoned", and propagate it until the source is stopped
// by the service (this would technically allow for stricter lifetime checking
// of data sources, and help with discarding periodic flushes).
// TODO(rsavitski): Purging while stopping will currently leave the stop
// unacknowledged. Consider checking whether the DS is stopping here, and if so,
// notifying immediately after erasing.
void PerfProducer::PurgeDataSource(DataSourceInstanceID ds_id) {
  auto ds_it = data_sources_.find(ds_id);
  if (ds_it == data_sources_.end())
    return;
  DataSourceState& ds = ds_it->second;

  PERFETTO_LOG("Stopping DataSource(%zu) prematurely",
               static_cast<size_t>(ds_id));

  unwinding_worker_->PostPurgeDataSource(ds_id);

  // Write a packet indicating the abrupt stop.
  {
    auto packet = StartTracePacket(ds.trace_writer.get());
    packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
    packet->set_timestamp_clock_id(
        protos::pbzero::BuiltinClock::BUILTIN_CLOCK_BOOTTIME);
    auto* perf_sample = packet->set_perf_sample();
    auto* producer_event = perf_sample->set_producer_event();
    producer_event->set_source_stop_reason(
        protos::pbzero::PerfSample::ProducerEvent::PROFILER_STOP_GUARDRAIL);
  }

  ds.trace_writer->Flush();
  data_sources_.erase(ds_it);

  // Clean up resources if there are no more active sources.
  if (data_sources_.empty()) {
    callstack_trie_.ClearTrie();  // purge internings
    base::MaybeReleaseAllocatorMemToOS();
  }
}

// Either:
// * choose a random number up to |shard_count|.
// * reuse a choice made previously by a data source within this tracing
//   session. The config option requires that all data sources within one config
//   have the same shard count.
std::optional<ProcessSharding> PerfProducer::GetOrChooseCallstackProcessShard(
    uint64_t tracing_session_id,
    uint32_t shard_count) {
  for (auto& it : data_sources_) {
    const DataSourceState& ds = it.second;
    const auto& sharding = ds.event_config.filter().process_sharding;
    if ((ds.tracing_session_id != tracing_session_id) || !sharding.has_value())
      continue;

    // Found existing data source, reuse its decision while doing best-effort
    // error reporting (logging) if the shard count is not the same.
    if (sharding->shard_count != shard_count) {
      PERFETTO_ELOG(
          "Mismatch of process_shard_count between data sources in tracing "
          "session %" PRIu64 ". Overriding shard count to match.",
          tracing_session_id);
    }
    return sharding;
  }

  // First data source in this session, choose random shard.
  std::random_device r;
  std::minstd_rand minstd(r());
  std::uniform_int_distribution<uint32_t> dist(0, shard_count - 1);
  uint32_t chosen_shard = dist(minstd);

  ProcessSharding ret;
  ret.shard_count = shard_count;
  ret.chosen_shard = chosen_shard;

  PERFETTO_DCHECK(ret.shard_count && ret.chosen_shard < ret.shard_count);
  return ret;
}

void PerfProducer::StartMetatraceSource(DataSourceInstanceID ds_id,
                                        BufferID target_buffer) {
  auto writer = endpoint_->CreateTraceWriter(target_buffer,
                                             BufferExhaustedPolicy::kStall);

  auto it_and_inserted = metatrace_writers_.emplace(
      std::piecewise_construct, std::make_tuple(ds_id), std::make_tuple());
  PERFETTO_DCHECK(it_and_inserted.second);
  // Note: only the first concurrent writer will actually be active.
  metatrace_writers_[ds_id].Enable(task_runner_, std::move(writer),
                                   metatrace::TAG_ANY);
}

void PerfProducer::ConnectWithRetries(const char* socket_name) {
  PERFETTO_DCHECK(state_ == kNotStarted);
  state_ = kNotConnected;

  ResetConnectionBackoff();
  producer_socket_name_ = socket_name;
  ConnectService();
}

void PerfProducer::ConnectService() {
  PERFETTO_DCHECK(state_ == kNotConnected);
  state_ = kConnecting;
  endpoint_ = ProducerIPCClient::Connect(
      producer_socket_name_, this, kProducerName, task_runner_,
      TracingService::ProducerSMBScrapingMode::kEnabled);
}

void PerfProducer::IncreaseConnectionBackoff() {
  connection_backoff_ms_ *= 2;
  if (connection_backoff_ms_ > kMaxConnectionBackoffMs)
    connection_backoff_ms_ = kMaxConnectionBackoffMs;
}

void PerfProducer::ResetConnectionBackoff() {
  connection_backoff_ms_ = kInitialConnectionBackoffMs;
}

void PerfProducer::OnConnect() {
  PERFETTO_DCHECK(state_ == kConnecting);
  state_ = kConnected;
  ResetConnectionBackoff();
  PERFETTO_LOG("Connected to the service");

  {
    // linux.perf
    DataSourceDescriptor desc;
    desc.set_name(kDataSourceName);
    desc.set_handles_incremental_state_clear(true);
    desc.set_will_notify_on_stop(true);
    endpoint_->RegisterDataSource(desc);
  }
  {
    // metatrace
    DataSourceDescriptor desc;
    desc.set_name(MetatraceWriter::kDataSourceName);
    endpoint_->RegisterDataSource(desc);
  }
  // Used by tracebox to synchronize with traced_probes being registered.
  if (all_data_sources_registered_cb_) {
    endpoint_->Sync(all_data_sources_registered_cb_);
  }
}

void PerfProducer::OnDisconnect() {
  PERFETTO_DCHECK(state_ == kConnected || state_ == kConnecting);
  PERFETTO_LOG("Disconnected from tracing service");

  auto weak_producer = weak_factory_.GetWeakPtr();
  if (state_ == kConnected)
    return task_runner_->PostTask([weak_producer] {
      if (weak_producer)
        weak_producer->Restart();
    });

  state_ = kNotConnected;
  IncreaseConnectionBackoff();
  task_runner_->PostDelayedTask(
      [weak_producer] {
        if (weak_producer)
          weak_producer->ConnectService();
      },
      connection_backoff_ms_);
}

void PerfProducer::Restart() {
  // We lost the connection with the tracing service. At this point we need
  // to reset all the data sources. Trying to handle that manually is going to
  // be error prone. What we do here is simply destroy the instance and
  // recreate it again.
  base::TaskRunner* task_runner = task_runner_;
  const char* socket_name = producer_socket_name_;
  ProcDescriptorGetter* proc_fd_getter = proc_fd_getter_;

  // Invoke destructor and then the constructor again.
  this->~PerfProducer();
  new (this) PerfProducer(proc_fd_getter, task_runner);

  ConnectWithRetries(socket_name);
}

}  // namespace profiling
}  // namespace perfetto
