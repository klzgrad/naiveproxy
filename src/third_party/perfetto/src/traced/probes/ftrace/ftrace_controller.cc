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

#include "src/traced/probes/ftrace/ftrace_controller.h"

#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdint>

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/metatrace.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "src/kallsyms/kernel_symbol_map.h"
#include "src/kallsyms/lazy_kernel_symbolizer.h"
#include "src/traced/probes/ftrace/atrace_hal_wrapper.h"
#include "src/traced/probes/ftrace/cpu_reader.h"
#include "src/traced/probes/ftrace/cpu_stats_parser.h"
#include "src/traced/probes/ftrace/event_info.h"
#include "src/traced/probes/ftrace/event_info_constants.h"
#include "src/traced/probes/ftrace/ftrace_config_muxer.h"
#include "src/traced/probes/ftrace/ftrace_config_utils.h"
#include "src/traced/probes/ftrace/ftrace_data_source.h"
#include "src/traced/probes/ftrace/ftrace_metadata.h"
#include "src/traced/probes/ftrace/ftrace_stats.h"
#include "src/traced/probes/ftrace/predefined_tracepoints.h"
#include "src/traced/probes/ftrace/proto_translation_table.h"
#include "src/traced/probes/ftrace/tracefs.h"
#include "src/traced/probes/ftrace/vendor_tracepoints.h"

namespace perfetto {
namespace {

constexpr uint32_t kDefaultTickPeriodMs = 100;
constexpr uint32_t kPollBackingTickPeriodMs = 1000;
constexpr uint32_t kMinTickPeriodMs = 1;
constexpr uint32_t kMaxTickPeriodMs = 1000 * 60;
constexpr int kPollRequiredMajorVersion = 6;
constexpr int kPollRequiredMinorVersion = 9;

// Read at most this many pages of data per cpu per read task. If we hit this
// limit on at least one cpu, we stop and repost the read task, letting other
// tasks get some cpu time before continuing reading.
constexpr size_t kMaxPagesPerCpuPerReadTick = 256;  // 1 MB per cpu

bool WriteToFile(const char* path, const char* str) {
  auto fd = base::OpenFile(path, O_WRONLY);
  if (!fd)
    return false;
  const size_t str_len = strlen(str);
  return base::WriteAll(*fd, str, str_len) == static_cast<ssize_t>(str_len);
}

bool ClearFile(const char* path) {
  auto fd = base::OpenFile(path, O_WRONLY | O_TRUNC);
  return !!fd;
}

std::optional<int64_t> ReadFtraceNowTs(const base::ScopedFile& cpu_stats_fd) {
  PERFETTO_CHECK(cpu_stats_fd);

  char buf[512];
  ssize_t res = PERFETTO_EINTR(pread(*cpu_stats_fd, buf, sizeof(buf) - 1, 0));
  if (res <= 0)
    return std::nullopt;
  buf[res] = '\0';

  FtraceCpuStats stats{};
  DumpCpuStats(buf, &stats);
  return static_cast<int64_t>(stats.now_ts * 1000 * 1000 * 1000);
}

std::map<std::string, std::vector<GroupAndName>> GetAtraceVendorEvents(
    Tracefs* tracefs) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  if (base::FileExists(vendor_tracepoints::kCategoriesFile)) {
    std::map<std::string, std::vector<GroupAndName>> vendor_evts;
    base::Status status =
        vendor_tracepoints::DiscoverAccessibleVendorTracepointsWithFile(
            vendor_tracepoints::kCategoriesFile, &vendor_evts, tracefs);
    if (!status.ok()) {
      PERFETTO_ELOG("Cannot load vendor categories: %s", status.c_message());
    }
    return vendor_evts;
  } else {
    AtraceHalWrapper hal;
    return vendor_tracepoints::DiscoverVendorTracepointsWithHal(&hal, tracefs);
  }
#else
  base::ignore_result(tracefs);
  return {};
#endif
}

struct AndroidGkiVersion {
  uint64_t version = 0;
  uint64_t patch_level = 0;
  uint64_t sub_level = 0;
  uint64_t release = 0;
  uint64_t kmi_gen = 0;
};

#define ANDROID_GKI_UNAME_FMT \
  "%" PRIu64 ".%" PRIu64 ".%" PRIu64 "-android%" PRIu64 "-%" PRIu64

std::optional<AndroidGkiVersion> ParseAndroidGkiVersion(const char* s) {
  AndroidGkiVersion v = {};
  if (sscanf(s, ANDROID_GKI_UNAME_FMT, &v.version, &v.patch_level, &v.sub_level,
             &v.release, &v.kmi_gen) != 5) {
    return std::nullopt;
  }
  return v;
}

}  // namespace

// Method of last resort to reset ftrace state.
// We don't know what state the rest of the system and process is so as far
// as possible avoid allocations.
bool HardResetFtraceState() {
  for (const char* const* item = Tracefs::kTracingPaths; *item; ++item) {
    std::string prefix(*item);
    PERFETTO_CHECK(base::EndsWith(prefix, "/"));
    bool res = true;
    res &= WriteToFile((prefix + "tracing_on").c_str(), "0");
    res &= WriteToFile((prefix + "buffer_size_kb").c_str(), "4");
    // Not checking success because these files might not be accessible on
    // older or release builds of Android:
    WriteToFile((prefix + "events/enable").c_str(), "0");
    WriteToFile((prefix + "events/raw_syscalls/filter").c_str(), "0");
    WriteToFile((prefix + "current_tracer").c_str(), "nop");
    res &= ClearFile((prefix + "trace").c_str());
    if (res)
      return true;
  }
  return false;
}

// static
std::unique_ptr<FtraceController> FtraceController::Create(
    base::TaskRunner* runner,
    Observer* observer) {
  std::unique_ptr<Tracefs> tracefs = Tracefs::CreateGuessingMountPoint("");
  if (!tracefs)
    return nullptr;

  std::unique_ptr<ProtoTranslationTable> table = ProtoTranslationTable::Create(
      tracefs.get(), GetStaticEventInfo(), GetStaticCommonFieldsInfo());
  if (!table)
    return nullptr;

  auto atrace_wrapper = std::make_unique<AtraceWrapperImpl>();

  std::map<std::string, base::FlatSet<GroupAndName>> predefined_events =
      predefined_tracepoints::GetAccessiblePredefinedTracePoints(table.get(),
                                                                 tracefs.get());

  std::map<std::string, std::vector<GroupAndName>> vendor_evts =
      GetAtraceVendorEvents(tracefs.get());

  SyscallTable syscalls = SyscallTable::FromCurrentArch();

  auto muxer = std::make_unique<FtraceConfigMuxer>(
      tracefs.get(), atrace_wrapper.get(), table.get(), syscalls,
      predefined_events, vendor_evts);
  return std::unique_ptr<FtraceController>(new FtraceController(
      std::move(tracefs), std::move(table), std::move(atrace_wrapper),
      std::move(muxer), runner, observer));
}

FtraceController::FtraceController(
    std::unique_ptr<Tracefs> tracefs,
    std::unique_ptr<ProtoTranslationTable> table,
    std::unique_ptr<AtraceWrapper> atrace_wrapper,
    std::unique_ptr<FtraceConfigMuxer> muxer,
    base::TaskRunner* task_runner,
    Observer* observer)
    : task_runner_(task_runner),
      observer_(observer),
      atrace_wrapper_(std::move(atrace_wrapper)),
      primary_(std::move(tracefs), std::move(table), std::move(muxer)),
      weak_factory_(this) {}

FtraceController::~FtraceController() {
  while (!data_sources_.empty()) {
    RemoveDataSource(*data_sources_.begin());
  }
  PERFETTO_DCHECK(data_sources_.empty());
  PERFETTO_DCHECK(primary_.started_data_sources.empty());
  PERFETTO_DCHECK(primary_.cpu_readers.empty());
  PERFETTO_DCHECK(secondary_instances_.empty());
}

uint64_t FtraceController::NowMs() const {
  return static_cast<uint64_t>(base::GetWallTimeMs().count());
}

template <typename F>
void FtraceController::ForEachInstance(F fn) {
  fn(&primary_);
  for (auto& kv : secondary_instances_) {
    fn(kv.second.get());
  }
}

void FtraceController::StartIfNeeded(FtraceInstanceState* instance,
                                     const std::string& instance_name) {
  if (buffer_watermark_support_ == PollSupport::kUntested) {
    buffer_watermark_support_ = VerifyKernelSupportForBufferWatermark();
  }

  // If instance is already active, then at most we need to update the buffer
  // poll callbacks. The periodic |ReadTick| will pick up any updates to the
  // period the next time it executes.
  if (instance->started_data_sources.size() > 1) {
    UpdateBufferWatermarkWatches(instance, instance_name);
    return;
  }

  // Lazily allocate the memory used for reading & parsing ftrace. In the case
  // of multiple ftrace instances, this might already be valid.
  parsing_mem_.AllocateIfNeeded();

  size_t num_cpus = instance->tracefs->NumberOfCpus();
  PERFETTO_CHECK(instance->cpu_readers.empty());
  instance->cpu_readers.reserve(num_cpus);
  for (size_t cpu = 0; cpu < num_cpus; cpu++) {
    instance->cpu_readers.emplace_back(cpu,
                                       instance->tracefs->OpenPipeForCpu(cpu),
                                       instance->table.get(), &symbolizer_);
  }

  // Special case: if not using the boot clock, cache an fd for taking manual
  // clock snapshots. This lets the trace parser can do a best effort conversion
  // back to boot.
  if (instance->ftrace_config_muxer->ftrace_clock() !=
      protos::pbzero::FtraceClock::FTRACE_CLOCK_UNSPECIFIED) {
    instance->cpu_zero_stats_fd = instance->tracefs->OpenCpuStats(/*cpu=*/0);
  }

  // Set up poll callbacks for the buffers if requested by at least one DS.
  UpdateBufferWatermarkWatches(instance, instance_name);

  // Start a new repeating read task (even if there is already one posted due
  // to a different ftrace instance). Any old tasks will stop due to generation
  // checks.
  auto generation = ++tick_generation_;
  auto tick_period_ms = GetTickPeriodMs();
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this, generation] {
        if (weak_this)
          weak_this->ReadTick(generation);
      },
      tick_period_ms - (NowMs() % tick_period_ms));
}

// We handle the ftrace buffers in a repeating task (ReadTick). On a given tick,
// we iterate over all per-cpu buffers, parse their contents, and then write out
// the serialized packets. This is handled by |CpuReader| instances, which
// attempt to read from their respective per-cpu buffer fd until they catch up
// to the head of the buffer, or hit a transient error.
//
// The readers work in batches of |kParsingBufferSizePages| pages for cache
// locality, and to limit memory usage.
//
// However, the reading happens on the primary thread, shared with the rest of
// the service (including ipc). If there is a lot of ftrace data to read, we
// want to yield to the event loop, re-enqueueing a continuation task at the end
// of the immediate queue (letting other enqueued tasks to run before
// continuing). Therefore we introduce |kMaxPagesPerCpuPerReadTick|.
void FtraceController::ReadTick(int generation) {
  metatrace::ScopedEvent evt(metatrace::TAG_FTRACE,
                             metatrace::FTRACE_READ_TICK);
  if (generation != tick_generation_ || GetStartedDataSourcesCount() == 0) {
    return;
  }

  // Read all per-cpu buffers.
  bool all_cpus_done = true;
  ForEachInstance([&](FtraceInstanceState* instance) {
    all_cpus_done &= ReadPassForInstance(instance);
  });
  observer_->OnFtraceDataWrittenIntoDataSourceBuffers();

  auto weak_this = weak_factory_.GetWeakPtr();
  if (!all_cpus_done) {
    PERFETTO_DLOG("Reposting immediate ReadTick as there's more work.");
    task_runner_->PostTask([weak_this, generation] {
      if (weak_this)
        weak_this->ReadTick(generation);
    });
  } else {
    // Done until next period.
    auto tick_period_ms = GetTickPeriodMs();
    task_runner_->PostDelayedTask(
        [weak_this, generation] {
          if (weak_this)
            weak_this->ReadTick(generation);
        },
        tick_period_ms - (NowMs() % tick_period_ms));
  }

#if PERFETTO_DCHECK_IS_ON()
  // OnFtraceDataWrittenIntoDataSourceBuffers() is supposed to clear
  // all metadata, including the |kernel_addrs| map for symbolization.
  ForEachInstance([&](FtraceInstanceState* instance) {
    for (FtraceDataSource* ds : instance->started_data_sources) {
      FtraceMetadata* ftrace_metadata = ds->mutable_metadata();
      PERFETTO_DCHECK(ftrace_metadata->kernel_addrs.empty());
      PERFETTO_DCHECK(ftrace_metadata->last_kernel_addr_index_written == 0);
    }
  });
#endif
}

bool FtraceController::ReadPassForInstance(FtraceInstanceState* instance) {
  if (instance->started_data_sources.empty())
    return true;

  std::optional<CpuReader::FtraceClockSnapshot> clock_snapshot =
      SnapshotFtraceClockIfNotBoot(instance);

  bool all_cpus_done = true;
  for (size_t i = 0; i < instance->cpu_readers.size(); i++) {
    size_t max_pages = kMaxPagesPerCpuPerReadTick;
    size_t pages_read = instance->cpu_readers[i].ReadCycle(
        &parsing_mem_, max_pages, instance->started_data_sources,
        clock_snapshot);
    PERFETTO_DCHECK(pages_read <= max_pages);
    if (pages_read == max_pages) {
      all_cpus_done = false;
    }
  }
  return all_cpus_done;
}

uint32_t FtraceController::GetTickPeriodMs() {
  if (data_sources_.empty())
    return kDefaultTickPeriodMs;
  uint32_t kUnsetPeriod = std::numeric_limits<uint32_t>::max();
  uint32_t min_period_ms = kUnsetPeriod;
  bool using_poll = true;
  ForEachInstance([&](FtraceInstanceState* instance) {
    using_poll &= instance->buffer_watches_posted;
    for (FtraceDataSource* ds : instance->started_data_sources) {
      if (ds->config().has_drain_period_ms()) {
        min_period_ms = std::min(min_period_ms, ds->config().drain_period_ms());
      }
    }
  });

  // None of the active data sources requested an explicit tick period.
  // The historical default is 100ms, but if we know that all instances are also
  // using buffer watermark polling, we can raise it. We don't disable the tick
  // entirely as it spreads the read work more evenly, and ensures procfs
  // scrapes of seen TIDs are not too stale.
  if (min_period_ms == kUnsetPeriod) {
    return using_poll ? kPollBackingTickPeriodMs : kDefaultTickPeriodMs;
  }

  if (min_period_ms < kMinTickPeriodMs || min_period_ms > kMaxTickPeriodMs) {
    PERFETTO_LOG(
        "drain_period_ms was %u should be between %u and %u. "
        "Falling back onto a default.",
        min_period_ms, kMinTickPeriodMs, kMaxTickPeriodMs);
    return kDefaultTickPeriodMs;
  }
  return min_period_ms;
}

void FtraceController::UpdateBufferWatermarkWatches(
    FtraceInstanceState* instance,
    const std::string& instance_name) {
  PERFETTO_DCHECK(buffer_watermark_support_ != PollSupport::kUntested);
  if (buffer_watermark_support_ == PollSupport::kUnsupported)
    return;

  bool requested_poll = false;
  for (const FtraceDataSource* ds : instance->started_data_sources) {
    requested_poll |= ds->config().has_drain_buffer_percent();
  }

  if (!requested_poll || instance->buffer_watches_posted)
    return;

  auto weak_this = weak_factory_.GetWeakPtr();
  for (size_t i = 0; i < instance->cpu_readers.size(); i++) {
    int fd = instance->cpu_readers[i].RawBufferFd();
    task_runner_->AddFileDescriptorWatch(fd, [weak_this, instance_name, i] {
      if (weak_this)
        weak_this->OnBufferPastWatermark(instance_name, i,
                                         /*repoll_watermark=*/true);
    });
  }
  instance->buffer_watches_posted = true;
}

void FtraceController::RemoveBufferWatermarkWatches(
    FtraceInstanceState* instance) {
  if (!instance->buffer_watches_posted)
    return;

  for (size_t i = 0; i < instance->cpu_readers.size(); i++) {
    int fd = instance->cpu_readers[i].RawBufferFd();
    task_runner_->RemoveFileDescriptorWatch(fd);
  }
  instance->buffer_watches_posted = false;
}

// TODO(rsavitski): consider calling OnFtraceData only if we're not reposting
// a continuation. It's a tradeoff between procfs scrape freshness and urgency
// to drain ftrace kernel buffers.
void FtraceController::OnBufferPastWatermark(const std::string& instance_name,
                                             size_t cpu,
                                             bool repoll_watermark) {
  metatrace::ScopedEvent evt(metatrace::TAG_FTRACE,
                             metatrace::FTRACE_CPU_BUFFER_WATERMARK);

  // Instance might have been stopped before this callback runs.
  FtraceInstanceState* instance = GetInstance(instance_name);
  if (!instance || cpu >= instance->cpu_readers.size())
    return;

  // Repoll all per-cpu buffers with zero timeout to confirm that at least
  // one is still past the watermark. This might not be true if a different
  // callback / readtick / flush did a read pass before this callback reached
  // the front of the task runner queue.
  if (repoll_watermark) {
    size_t num_cpus = instance->cpu_readers.size();
    std::vector<struct pollfd> pollfds(num_cpus);
    for (size_t i = 0; i < num_cpus; i++) {
      pollfds[i].fd = instance->cpu_readers[i].RawBufferFd();
      pollfds[i].events = POLLIN;
    }
    int r = PERFETTO_EINTR(poll(pollfds.data(), num_cpus, 0));
    if (r < 0) {
      PERFETTO_DPLOG("poll failed");
      return;
    } else if (r == 0) {  // no buffers below the watermark -> we're done.
      return;
    }
    // Count the number of readable fds, as some poll results might be POLLERR,
    // as seen in cases with offlined cores. It's still fine to attempt reading
    // from those buffers as CpuReader will handle the ENODEV.
    bool has_readable_fd = false;
    for (size_t i = 0; i < num_cpus; i++) {
      has_readable_fd |= (pollfds[i].revents & POLLIN);
    }
    if (!has_readable_fd) {
      return;
    }
  }

  bool all_cpus_done = ReadPassForInstance(instance);
  observer_->OnFtraceDataWrittenIntoDataSourceBuffers();
  if (!all_cpus_done) {
    // More data to be read, but we want to let other task_runner tasks to run.
    // Repost a continuation task.
    auto weak_this = weak_factory_.GetWeakPtr();
    task_runner_->PostTask([weak_this, instance_name, cpu] {
      if (weak_this)
        weak_this->OnBufferPastWatermark(instance_name, cpu,
                                         /*repoll_watermark=*/false);
    });
  }
}

void FtraceController::Flush(FlushRequestID flush_id) {
  metatrace::ScopedEvent evt(metatrace::TAG_FTRACE,
                             metatrace::FTRACE_CPU_FLUSH);

  ForEachInstance([&](FtraceInstanceState* instance) {  // for clang-format
    FlushForInstance(instance);
  });
  observer_->OnFtraceDataWrittenIntoDataSourceBuffers();

  ForEachInstance([&](FtraceInstanceState* instance) {
    for (FtraceDataSource* ds : instance->started_data_sources) {
      ds->OnFtraceFlushComplete(flush_id);
    }
  });
}

void FtraceController::FlushForInstance(FtraceInstanceState* instance) {
  if (instance->started_data_sources.empty())
    return;

  std::optional<CpuReader::FtraceClockSnapshot> clock_snapshot =
      SnapshotFtraceClockIfNotBoot(instance);

  // Read all cpus in one go, limiting the per-cpu read amount to make sure we
  // don't get stuck chasing the writer if there's a very high bandwidth of
  // events.
  size_t max_pages = instance->ftrace_config_muxer->GetPerCpuBufferSizePages();
  for (size_t i = 0; i < instance->cpu_readers.size(); i++) {
    instance->cpu_readers[i].ReadCycle(&parsing_mem_, max_pages,
                                       instance->started_data_sources,
                                       clock_snapshot);
  }
}

// We are not implicitly flushing on Stop. The tracing service is supposed to
// ask for an explicit flush before stopping, unless it needs to perform a
// non-graceful stop.
void FtraceController::StopIfNeeded(FtraceInstanceState* instance) {
  if (!instance->started_data_sources.empty())
    return;

  RemoveBufferWatermarkWatches(instance);
  instance->cpu_readers.clear();
  instance->cpu_zero_stats_fd.reset();
  // Muxer cannot change the current_tracer until we close the trace pipe fds
  // (i.e. per_cpu). Hence an explicit request here.
  instance->ftrace_config_muxer->ResetCurrentTracer();

  DestroyIfUnusedSeconaryInstance(instance);

  // Clean up global state if done with all data sources.
  if (!data_sources_.empty())
    return;

  // The kernel symbol table is discarded by default to save memory as we run as
  // a long-lived daemon. Check if the config asked to retain the symbols (e.g.
  // lab tests). And in either case, reset a set-but-empty table to allow trying
  // again next time a config requests symbols.
  if (!retain_ksyms_on_stop_ ||
      (symbolizer_.is_valid() &&
       symbolizer_.GetOrCreateKernelSymbolMap()->num_syms() == 0)) {
    symbolizer_.Destroy();
  }
  retain_ksyms_on_stop_ = false;

  // Note: might have never been allocated if data sources were rejected.
  parsing_mem_.Release();
}

bool FtraceController::AddDataSource(FtraceDataSource* data_source) {
  if (!ValidConfig(data_source->config()))
    return false;

  FtraceInstanceState* instance =
      GetOrCreateInstance(data_source->config().instance_name());
  if (!instance)
    return false;

  // note: from this point onwards, need to not leak a possibly created
  // instance if returning early.

  FtraceConfigId config_id = next_cfg_id_++;
  if (!instance->ftrace_config_muxer->SetupConfig(
          config_id, data_source->config(),
          data_source->mutable_setup_errors())) {
    DestroyIfUnusedSeconaryInstance(instance);
    return false;
  }

  const FtraceDataSourceConfig* ds_config =
      instance->ftrace_config_muxer->GetDataSourceConfig(config_id);
  auto it_and_inserted = data_sources_.insert(data_source);
  PERFETTO_DCHECK(it_and_inserted.second);
  data_source->Initialize(config_id, ds_config);
  return true;
}

bool FtraceController::StartDataSource(FtraceDataSource* data_source) {
  PERFETTO_DCHECK(data_sources_.count(data_source) > 0);

  FtraceConfigId config_id = data_source->config_id();
  PERFETTO_CHECK(config_id);
  const std::string& instance_name = data_source->config().instance_name();
  FtraceInstanceState* instance = GetOrCreateInstance(instance_name);
  PERFETTO_CHECK(instance);

  if (!instance->ftrace_config_muxer->ActivateConfig(config_id))
    return false;
  instance->started_data_sources.insert(data_source);
  StartIfNeeded(instance, instance_name);

  // Parse kernel symbols if required by the config. This can be an expensive
  // operation (cpu-bound for 500ms+), so delay the StartDataSource
  // acknowledgement until after we're done. This lets a consumer wait for the
  // expensive work to be done by waiting on the "all data sources started"
  // fence. This helps isolate the effects of the cpu-bound work on
  // frequency scaling of cpus when recording benchmarks (b/236143653).
  // Note that we're already recording data into the kernel ftrace
  // buffers while doing the symbol parsing.
  if (data_source->config().symbolize_ksyms()) {
    symbolizer_.GetOrCreateKernelSymbolMap();
    // If at least one config sets the KSYMS_RETAIN flag, keep the ksyms map
    // around in StopIfNeeded().
    const auto KRET = FtraceConfig::KSYMS_RETAIN;
    retain_ksyms_on_stop_ |= data_source->config().ksyms_mem_policy() == KRET;
  }

  return true;
}

void FtraceController::RemoveDataSource(FtraceDataSource* data_source) {
  size_t removed = data_sources_.erase(data_source);
  if (!removed)
    return;  // can happen if AddDataSource failed

  FtraceInstanceState* instance =
      GetOrCreateInstance(data_source->config().instance_name());
  PERFETTO_CHECK(instance);

  instance->ftrace_config_muxer->RemoveConfig(data_source->config_id());
  instance->started_data_sources.erase(data_source);
  StopIfNeeded(instance);
}

bool DumpKprobeStats(std::string text, FtraceStats* ftrace_stats) {
  int64_t hits = 0;
  int64_t misses = 0;

  base::StringSplitter line(std::move(text), '\n');
  while (line.Next()) {
    base::StringSplitter tok(line.cur_token(), line.cur_token_size() + 1, ' ');

    if (!tok.Next())
      return false;
    // Skip the event name field

    if (!tok.Next())
      return false;
    hits += static_cast<int64_t>(std::strtoll(tok.cur_token(), nullptr, 10));

    if (!tok.Next())
      return false;
    misses += static_cast<int64_t>(std::strtoll(tok.cur_token(), nullptr, 10));
  }

  ftrace_stats->kprobe_stats.hits = hits;
  ftrace_stats->kprobe_stats.misses = misses;

  return true;
}

void FtraceController::DumpFtraceStats(FtraceDataSource* data_source,
                                       FtraceStats* stats_out) {
  FtraceInstanceState* instance =
      GetInstance(data_source->config().instance_name());
  PERFETTO_DCHECK(instance);
  if (!instance)
    return;

  DumpAllCpuStats(instance->tracefs.get(), stats_out);

  // Record the per-cpu buffer size as cached by the muxer, and the actual value
  // returned by the tracefs. Helps catch rogue tracefs modifications under us,
  // as well as to check that the caching is accurate in practice (depending on
  // the kernel version, the chosen value might be different to what was written
  // into the file).
  stats_out->cpu_buffer_size_pages =
      static_cast<uint32_t>(instance->tracefs->GetCpuBufferSizeInPages());
  stats_out->cached_cpu_buffer_size_pages = static_cast<uint32_t>(
      instance->ftrace_config_muxer->GetPerCpuBufferSizePages());

  if (symbolizer_.is_valid()) {
    auto* symbol_map = symbolizer_.GetOrCreateKernelSymbolMap();
    stats_out->kernel_symbols_parsed =
        static_cast<uint32_t>(symbol_map->num_syms());
    stats_out->kernel_symbols_mem_kb =
        static_cast<uint32_t>(symbol_map->size_bytes() / 1024);
  }

  if (data_source->parsing_config()->kprobes.size() > 0) {
    DumpKprobeStats(instance->tracefs->ReadKprobeStats(), stats_out);
  }
}

std::optional<CpuReader::FtraceClockSnapshot>
FtraceController::SnapshotFtraceClockIfNotBoot(FtraceInstanceState* instance) {
  auto ftrace_clock = instance->ftrace_config_muxer->ftrace_clock();
  if (!instance->cpu_zero_stats_fd ||
      (ftrace_clock == protos::pbzero::FtraceClock::FTRACE_CLOCK_UNSPECIFIED)) {
    return std::nullopt;
  }

  CpuReader::FtraceClockSnapshot ret;
  ret.ftrace_clock = ftrace_clock;
  ret.boot_clock_ts = base::GetBootTimeNs().count();
  ret.ftrace_clock_ts =
      ReadFtraceNowTs(instance->cpu_zero_stats_fd).value_or(0);
  return ret;
}

FtraceController::PollSupport
FtraceController::VerifyKernelSupportForBufferWatermark() {
  struct utsname uts = {};
  if (uname(&uts) < 0 || strcmp(uts.sysname, "Linux") != 0)
    return PollSupport::kUnsupported;
  if (!PollSupportedOnKernelVersion(uts.release))
    return PollSupport::kUnsupported;

  // buffer_percent exists and is writable
  auto* tracefs = primary_.tracefs.get();
  uint32_t current = tracefs->ReadBufferPercent();
  if (!tracefs->SetBufferPercent(current ? current : 50)) {
    return PollSupport::kUnsupported;
  }

  // Polling on per_cpu/cpu0/trace_pipe_raw doesn't return errors.
  base::ScopedFile fd = tracefs->OpenPipeForCpu(0);
  struct pollfd pollset = {};
  pollset.fd = fd.get();
  pollset.events = POLLIN;
  int r = PERFETTO_EINTR(poll(&pollset, 1, 0));
  if (r < 0 || (r > 0 && (pollset.revents & POLLERR))) {
    return PollSupport::kUnsupported;
  }
  return PollSupport::kSupported;
}

// Check kernel version since the poll implementation has historical bugs.
// We're looking for at least 6.9 for the following:
//   ffe3986fece6 ring-buffer: Only update pages_touched when a new page...
// static
bool FtraceController::PollSupportedOnKernelVersion(const char* uts_release) {
  int major = 0, minor = 0;
  if (sscanf(uts_release, "%d.%d", &major, &minor) != 2) {
    return false;
  }
  if (major < kPollRequiredMajorVersion ||
      (major == kPollRequiredMajorVersion &&
       minor < kPollRequiredMinorVersion)) {
    // Android: opportunistically detect a few select GKI kernels that are known
    // to have the fixes.
    std::optional<AndroidGkiVersion> gki = ParseAndroidGkiVersion(uts_release);
    if (!gki.has_value())
      return false;
    // android14-6.1.86 or higher sublevel:
    //   2d5f12de4cf5 ring-buffer: Only update pages_touched when a new page...
    // android15-6.6.27 or higher sublevel:
    //   a9cd92bc051f ring-buffer: Only update pages_touched when a new page...
    bool gki_patched = (gki->release == 14 && gki->version == 6 &&
                        gki->patch_level == 1 && gki->sub_level >= 86) ||
                       (gki->release == 15 && gki->version == 6 &&
                        gki->patch_level == 6 && gki->sub_level >= 27);
    return gki_patched;
  }
  return true;
}

size_t FtraceController::GetStartedDataSourcesCount() {
  size_t cnt = 0;
  ForEachInstance([&](FtraceInstanceState* instance) {
    cnt += instance->started_data_sources.size();
  });
  return cnt;
}

FtraceController::FtraceInstanceState::FtraceInstanceState(
    std::unique_ptr<Tracefs> ft,
    std::unique_ptr<ProtoTranslationTable> ptt,
    std::unique_ptr<FtraceConfigMuxer> fcm)
    : tracefs(std::move(ft)),
      table(std::move(ptt)),
      ftrace_config_muxer(std::move(fcm)) {}

FtraceController::FtraceInstanceState* FtraceController::GetOrCreateInstance(
    const std::string& instance_name) {
  FtraceInstanceState* maybe_existing = GetInstance(instance_name);
  if (maybe_existing)
    return maybe_existing;

  PERFETTO_DCHECK(!instance_name.empty());
  std::unique_ptr<FtraceInstanceState> instance =
      CreateSecondaryInstance(instance_name);
  if (!instance)
    return nullptr;

  auto it_and_inserted = secondary_instances_.emplace(
      std::piecewise_construct, std::forward_as_tuple(instance_name),
      std::forward_as_tuple(std::move(instance)));
  PERFETTO_CHECK(it_and_inserted.second);
  return it_and_inserted.first->second.get();
}

FtraceController::FtraceInstanceState* FtraceController::GetInstance(
    const std::string& instance_name) {
  if (instance_name.empty())
    return &primary_;

  auto it = secondary_instances_.find(instance_name);
  return it != secondary_instances_.end() ? it->second.get() : nullptr;
}

void FtraceController::DestroyIfUnusedSeconaryInstance(
    FtraceInstanceState* instance) {
  if (instance == &primary_)
    return;
  for (auto it = secondary_instances_.begin(); it != secondary_instances_.end();
       ++it) {
    if (it->second.get() == instance &&
        instance->ftrace_config_muxer->GetDataSourcesCount() == 0) {
      // no data sources left referencing this secondary instance
      secondary_instances_.erase(it);
      return;
    }
  }
  PERFETTO_FATAL("Bug in ftrace instance lifetimes");
}

std::unique_ptr<FtraceController::FtraceInstanceState>
FtraceController::CreateSecondaryInstance(const std::string& instance_name) {
  std::optional<std::string> instance_path =
      AbsolutePathForInstance(primary_.tracefs->GetRootPath(), instance_name);
  if (!instance_path.has_value()) {
    PERFETTO_ELOG("Invalid ftrace instance name: \"%s\"",
                  instance_name.c_str());
    return nullptr;
  }

  auto tracefs = Tracefs::Create(*instance_path);
  if (!tracefs) {
    PERFETTO_ELOG("Failed to create tracefs for \"%s\"",
                  instance_path->c_str());
    return nullptr;
  }

  auto table = ProtoTranslationTable::Create(
      tracefs.get(), GetStaticEventInfo(), GetStaticCommonFieldsInfo());
  if (!table) {
    PERFETTO_ELOG("Failed to create proto translation table for \"%s\"",
                  instance_path->c_str());
    return nullptr;
  }

  std::map<std::string, base::FlatSet<GroupAndName>> predefined_events =
      predefined_tracepoints::GetAccessiblePredefinedTracePoints(table.get(),
                                                                 tracefs.get());

  // secondary instances don't support atrace and vendor tracepoint HAL
  std::map<std::string, std::vector<GroupAndName>> vendor_evts;

  auto syscalls = SyscallTable::FromCurrentArch();

  auto muxer = std::make_unique<FtraceConfigMuxer>(
      tracefs.get(), atrace_wrapper_.get(), table.get(), syscalls,
      predefined_events, vendor_evts,
      /* secondary_instance= */ true);
  return std::make_unique<FtraceInstanceState>(
      std::move(tracefs), std::move(table), std::move(muxer));
}

// TODO(rsavitski): we want to eventually add support for the default
// (primary_) tracefs path to be an instance itself, at which point we'll need
// to be careful to distinguish the tracefs mount point from the default
// instance path.
// static
std::optional<std::string> FtraceController::AbsolutePathForInstance(
    const std::string& tracefs_root,
    const std::string& raw_cfg_name) {
  if (base::Contains(raw_cfg_name, '/') ||
      base::StartsWith(raw_cfg_name, "..")) {
    return std::nullopt;
  }

  // ARM64 pKVM hypervisor tracing emulates an instance, but is not under
  // instances/, we special-case that name for now.
  if (raw_cfg_name == "hyp" || raw_cfg_name == "hypervisor") {
    std::string hyp_path = tracefs_root + raw_cfg_name + "/";
    PERFETTO_LOG(
        "Config specified reserved \"%s\" instance name, using %s for events.",
        raw_cfg_name.c_str(), hyp_path.c_str());
    return std::make_optional(hyp_path);
  }

  return tracefs_root + "instances/" + raw_cfg_name + "/";
}

FtraceController::Observer::~Observer() = default;

}  // namespace perfetto
