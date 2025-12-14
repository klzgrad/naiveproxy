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

#include "src/traced/probes/ps/process_stats_data_source.h"

#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <optional>

#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/metatrace.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/tracing/core/data_source_config.h"

#include "protos/perfetto/config/process_stats/process_stats_config.pbzero.h"
#include "protos/perfetto/trace/ps/process_stats.pbzero.h"
#include "protos/perfetto/trace/ps/process_tree.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

// The notion of PID in the Linux kernel is a bit confusing.
// - PID: is really the thread id (for the main thread: PID == TID).
// - TGID (thread group ID): is the Unix Process ID (the actual PID).
// - PID == TGID for the main thread: the TID of the main thread is also the PID
//   of the process.
// So, in this file, |pid| might refer to either a process id or a thread id.

// Dealing with PID reuse: the knowledge of which PIDs were already scraped is
// forgotten on every |ClearIncrementalState| if the trace config sets
// |incremental_state_config|. Additionally, there's a proactive invalidation
// whenever we see a task rename ftrace event, as that's a good signal that the
// /proc/pid/cmdline needs updating.
// TODO(rsavitski): consider invalidating on task creation or death ftrace
// events if available.
//
// TODO(rsavitski): we're not emitting an explicit description of the main
// thread (instead, it's implied by the process entry). This might be slightly
// inaccurate in edge cases like wanting to know the primary thread's name
// (comm) based on procfs alone.

namespace perfetto {
namespace {

int32_t ReadNextNumericDir(DIR* dirp) {
  while (struct dirent* dir_ent = readdir(dirp)) {
    if (dir_ent->d_type != DT_DIR)
      continue;
    auto int_value = base::CStringToInt32(dir_ent->d_name);
    if (int_value)
      return *int_value;
  }
  return 0;
}

std::string ProcStatusEntry(const std::string& buf, const char* key) {
  auto begin = buf.find(key);
  if (begin == std::string::npos)
    return "";
  begin = buf.find_first_not_of(" \t", begin + strlen(key));
  if (begin == std::string::npos)
    return "";
  auto end = buf.find('\n', begin);
  if (end == std::string::npos || end <= begin)
    return "";
  return buf.substr(begin, end - begin);
}

// Parses out the thread IDs in each non-root PID namespace from
// /proc/tid/status. Returns true if there is at least one non-root PID
// namespace.
template <typename Callback>
bool ParseNamespacedTids(const std::string& proc_status, Callback callback) {
  std::string str = ProcStatusEntry(proc_status, "NSpid:");
  if (str.empty())
    return false;

  base::StringSplitter ss(std::move(str), '\t');
  ss.Next();  // first element = root tid that we already know
  bool namespaced = false;
  while (ss.Next()) {
    namespaced = true;
    std::optional<int32_t> nstid = base::CStringToInt32(ss.cur_token());
    PERFETTO_DCHECK(nstid.has_value());
    callback(nstid.value_or(0));
  }
  return namespaced;
}

struct ProcessRuntimes {
  uint64_t utime = 0;
  uint64_t stime = 0;
  uint64_t starttime = 0;
};

std::optional<ProcessRuntimes> ParseProcessRuntimes(
    const std::string& proc_stat) {
  // /proc/pid/stat fields of interest, counting from 1:
  //  utime = 14
  //  stime = 15
  //  starttime = 22
  // sscanf format string below is formatted to 10 values per line.
  // clang-format off
  ProcessRuntimes ret = {};
  if (sscanf(proc_stat.c_str(),
             "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u "
             "%*u %*u %*u %" SCNu64 " %" SCNu64 " %*d %*d %*d %*d %*d "
             "%*d %" SCNu64 "",
             &ret.utime, &ret.stime, &ret.starttime) != 3) {
     PERFETTO_DLOG("empty or unexpected /proc/pid/stat contents");
     return std::nullopt;
   }
  // clang-format on
  int64_t tickrate = sysconf(_SC_CLK_TCK);
  if (tickrate <= 0)
    return std::nullopt;
  uint64_t ns_per_tick = 1'000'000'000ULL / static_cast<uint64_t>(tickrate);

  ret.utime *= ns_per_tick;
  ret.stime *= ns_per_tick;
  ret.starttime *= ns_per_tick;
  return ret;
}

// Note: conversions intentionally not checking that the full string was
// numerical as calling code depends on discarding suffixes in cases such as:
// * "92 kB" -> 92
// * "1000 2000" -> 1000
inline int32_t ToInt32(const std::string& str) {
  return static_cast<int32_t>(strtol(str.c_str(), nullptr, 10));
}

inline uint32_t ToUInt32(const char* str) {
  return static_cast<uint32_t>(strtoul(str, nullptr, 10));
}

inline uint64_t ToUInt64(const std::string& str) {
  return static_cast<uint64_t>(strtoull(str.c_str(), nullptr, 10));
}

}  // namespace

// static
const ProbesDataSource::Descriptor ProcessStatsDataSource::descriptor = {
    /*name*/ "linux.process_stats",
    /*flags*/ Descriptor::kHandlesIncrementalState,
    /*fill_descriptor_func*/ nullptr,
};

ProcessStatsDataSource::ProcessStatsDataSource(
    base::TaskRunner* task_runner,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer,
    const DataSourceConfig& ds_config)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {
  using protos::pbzero::ProcessStatsConfig;
  ProcessStatsConfig::Decoder cfg(ds_config.process_stats_config_raw());
  record_thread_names_ = cfg.record_thread_names();
  dump_all_procs_on_start_ = cfg.scan_all_processes_on_start();
  resolve_process_fds_ = cfg.resolve_process_fds();
  scan_smaps_rollup_ = cfg.scan_smaps_rollup();
  record_process_age_ = cfg.record_process_age();
  record_process_runtime_ = cfg.record_process_runtime();
  record_process_dmabuf_rss_ = cfg.record_process_dmabuf_rss();

  enable_on_demand_dumps_ = true;
  for (auto quirk = cfg.quirks(); quirk; ++quirk) {
    if (*quirk == ProcessStatsConfig::DISABLE_ON_DEMAND)
      enable_on_demand_dumps_ = false;
  }

  poll_period_ms_ = cfg.proc_stats_poll_ms();
  if (poll_period_ms_ > 0 && poll_period_ms_ < 100) {
    PERFETTO_ILOG("proc_stats_poll_ms %" PRIu32
                  " is less than minimum of 100ms. Increasing to 100ms.",
                  poll_period_ms_);
    poll_period_ms_ = 100;
  }

  if (poll_period_ms_ > 0) {
    auto proc_stats_ttl_ms = cfg.proc_stats_cache_ttl_ms();
    process_stats_cache_ttl_ticks_ =
        std::max(proc_stats_ttl_ms / poll_period_ms_, 1u);
  }
}

ProcessStatsDataSource::~ProcessStatsDataSource() = default;

void ProcessStatsDataSource::Start() {
  if (dump_all_procs_on_start_) {
    WriteAllProcesses();
  }

  if (poll_period_ms_) {
    auto weak_this = GetWeakPtr();
    task_runner_->PostTask(std::bind(&ProcessStatsDataSource::Tick, weak_this));
  }
}

base::WeakPtr<ProcessStatsDataSource> ProcessStatsDataSource::GetWeakPtr()
    const {
  return weak_factory_.GetWeakPtr();
}

void ProcessStatsDataSource::WriteAllProcesses() {
  PERFETTO_METATRACE_SCOPED(TAG_PROC_POLLERS, PS_WRITE_ALL_PROCESSES);
  PERFETTO_DCHECK(!cur_ps_tree_);

  CacheProcFsScanStartTimestamp();

  base::ScopedDir proc_dir = OpenProcDir();
  if (!proc_dir)
    return;
  base::FlatSet<int32_t> pids;
  while (int32_t pid = ReadNextNumericDir(*proc_dir)) {
    std::string pid_status = ReadProcPidFile(pid, "status");
    std::string pid_stat =
        record_process_age_ ? ReadProcPidFile(pid, "stat") : "";
    bool namespaced_process = WriteProcess(pid, pid_status, pid_stat);

    base::StackString<128> task_path("/proc/%d/task", pid);
    base::ScopedDir task_dir(opendir(task_path.c_str()));
    if (!task_dir)
      continue;

    while (int32_t tid = ReadNextNumericDir(*task_dir)) {
      if (tid == pid)
        continue;
      if (record_thread_names_ || namespaced_process) {
        std::string tid_status = ReadProcPidFile(tid, "status");
        WriteDetailedThread(tid, pid, tid_status);
      } else {
        WriteThread(tid, pid);
      }
    }

    pids.insert(pid);
  }
  FinalizeCurPacket();

  // Also collect any fds open when starting up (niche option).
  for (const auto pid : pids) {
    cur_ps_stats_process_ = nullptr;
    WriteFds(pid);
  }
  FinalizeCurPacket();
}

void ProcessStatsDataSource::OnPids(const base::FlatSet<int32_t>& pids) {
  if (!enable_on_demand_dumps_)
    return;
  WriteProcessTree(pids);
}

void ProcessStatsDataSource::WriteProcessTree(
    const base::FlatSet<int32_t>& pids) {
  PERFETTO_METATRACE_SCOPED(TAG_PROC_POLLERS, PS_ON_PIDS);
  PERFETTO_DCHECK(!cur_ps_tree_);
  int pids_scanned = 0;
  for (int32_t pid : pids) {
    if (seen_pids_.count(pid) || pid == 0)
      continue;
    WriteProcessOrThread(pid);
    pids_scanned++;
  }
  FinalizeCurPacket();
  PERFETTO_METATRACE_COUNTER(TAG_PROC_POLLERS, PS_PIDS_SCANNED, pids_scanned);
}

void ProcessStatsDataSource::OnRenamePids(const base::FlatSet<int32_t>& pids) {
  PERFETTO_METATRACE_SCOPED(TAG_PROC_POLLERS, PS_ON_RENAME_PIDS);
  if (!enable_on_demand_dumps_)
    return;
  PERFETTO_DCHECK(!cur_ps_tree_);
  for (int32_t pid : pids)
    seen_pids_.erase(pid);
}

void ProcessStatsDataSource::OnFds(
    const base::FlatSet<std::pair<pid_t, uint64_t>>& fds) {
  if (!resolve_process_fds_)
    return;

  pid_t last_pid = 0;
  for (const auto& tid_fd : fds) {
    const auto tid = tid_fd.first;
    const auto fd = tid_fd.second;

    auto it = seen_pids_.find(tid);
    if (it == seen_pids_.end()) {
      // TID is not known yet, skip resolving the fd and let the
      // periodic stats scanner resolve the fd together with its TID later
      continue;
    }
    const auto pid = it->tgid;

    if (last_pid != pid) {
      cur_ps_stats_process_ = nullptr;
      last_pid = pid;
    }
    WriteSingleFd(pid, fd);
  }
  FinalizeCurPacket();
}

void ProcessStatsDataSource::Flush(FlushRequestID,
                                   std::function<void()> callback) {
  // We shouldn't get this in the middle of WriteAllProcesses() or OnPids().
  PERFETTO_DCHECK(!cur_ps_tree_);
  PERFETTO_DCHECK(!cur_ps_stats_);
  PERFETTO_DCHECK(!cur_ps_stats_process_);
  writer_->Flush(callback);
}

void ProcessStatsDataSource::WriteProcessOrThread(int32_t pid) {
  // In case we're called from outside WriteAllProcesses()
  CacheProcFsScanStartTimestamp();

  std::string proc_status = ReadProcPidFile(pid, "status");
  if (proc_status.empty())
    return;
  int32_t tgid = ToInt32(ProcStatusEntry(proc_status, "Tgid:"));
  int32_t tid = ToInt32(ProcStatusEntry(proc_status, "Pid:"));
  if (tgid <= 0 || tid <= 0)
    return;

  if (!seen_pids_.count(tgid)) {
    // We need to read the status file if |pid| is non-main thread.
    const std::string& proc_status_tgid =
        (tgid == tid ? proc_status : ReadProcPidFile(tgid, "status"));
    const std::string& proc_stat =
        record_process_age_ ? ReadProcPidFile(tgid, "stat") : "";
    WriteProcess(tgid, proc_status_tgid, proc_stat);
  }
  if (pid != tgid) {
    PERFETTO_DCHECK(!seen_pids_.count(pid));
    WriteDetailedThread(pid, tgid, proc_status);
  }
}

// Returns true if the process is within a PID namespace.
bool ProcessStatsDataSource::WriteProcess(int32_t pid,
                                          const std::string& proc_status,
                                          const std::string& proc_stat) {
  PERFETTO_DCHECK(ToInt32(ProcStatusEntry(proc_status, "Pid:")) == pid);

  // pid might've been reused for a non-main thread before our procfs read
  if (PERFETTO_UNLIKELY(pid != ToInt32(ProcStatusEntry(proc_status, "Tgid:"))))
    return false;

  protos::pbzero::ProcessTree::Process* proc =
      GetOrCreatePsTree()->add_processes();
  proc->set_pid(pid);
  proc->set_ppid(ToInt32(ProcStatusEntry(proc_status, "PPid:")));
  // Uid will have multiple entries, only return first (real uid).
  proc->set_uid(ToInt32(ProcStatusEntry(proc_status, "Uid:")));
  bool namespaced = ParseNamespacedTids(
      proc_status, [proc](int32_t nspid) { proc->add_nspid(nspid); });

  std::string cmdline = ReadProcPidFile(pid, "cmdline");
  if (!cmdline.empty()) {
    if (cmdline.back() != '\0') {
      // Some kernels can miss the NUL terminator due to a bug. b/147438623.
      cmdline.push_back('\0');
    }
    for (base::StringSplitter ss(cmdline.data(), cmdline.size(), '\0');
         ss.Next();) {
      proc->add_cmdline(ss.cur_token());
    }
  } else {
    // Nothing in cmdline so use the thread name instead (which is == "comm").
    // This comes up at least for zombies and kthreads.
    proc->add_cmdline(ProcStatusEntry(proc_status, "Name:"));
    proc->set_cmdline_is_comm(true);
  }

  if (record_process_age_ && !proc_stat.empty()) {
    std::optional<ProcessRuntimes> times = ParseProcessRuntimes(proc_stat);
    if (times.has_value()) {
      proc->set_process_start_from_boot(times->starttime);
    }
  }

  // Linux v6.4 and onwards has an explicit field for whether this is a kthread.
  std::optional<int32_t> kthread =
      base::StringToInt32(ProcStatusEntry(proc_status, "Kthread:"));
  if (kthread.has_value() && (*kthread == 0 || *kthread == 1)) {
    proc->set_is_kthread(*kthread);
  }

  seen_pids_.insert({pid, pid});
  return namespaced;
}

void ProcessStatsDataSource::WriteThread(int32_t tid, int32_t tgid) {
  auto* thread = GetOrCreatePsTree()->add_threads();
  thread->set_tid(tid);
  thread->set_tgid(tgid);
  seen_pids_.insert({tid, tgid});
}

// Emit thread proto that requires /proc/tid/status contents. May also be called
// from places where the proc status contents are already available, but might
// end up unused.
void ProcessStatsDataSource::WriteDetailedThread(
    int32_t tid,
    int32_t tgid,
    const std::string& proc_status) {
  auto* thread = GetOrCreatePsTree()->add_threads();
  thread->set_tid(tid);
  thread->set_tgid(tgid);

  ParseNamespacedTids(proc_status,
                      [thread](int32_t nstid) { thread->add_nstid(nstid); });

  if (record_thread_names_) {
    std::string thread_name = ProcStatusEntry(proc_status, "Name:");
    thread->set_name(thread_name);
  }
  seen_pids_.insert({tid, tgid});
}

const char* ProcessStatsDataSource::GetProcMountpoint() {
  static constexpr char kDefaultProcMountpoint[] = "/proc";
  return kDefaultProcMountpoint;
}

base::ScopedDir ProcessStatsDataSource::OpenProcDir() {
  base::ScopedDir proc_dir(opendir(GetProcMountpoint()));
  if (!proc_dir)
    PERFETTO_PLOG("Failed to opendir(%s)", GetProcMountpoint());
  return proc_dir;
}

std::string ProcessStatsDataSource::ReadProcPidFile(int32_t pid,
                                                    const std::string& file) {
  base::StackString<128> path("/proc/%" PRId32 "/%s", pid, file.c_str());
  std::string contents;
  contents.reserve(4096);
  if (!base::ReadFile(path.c_str(), &contents))
    return "";
  return contents;
}

void ProcessStatsDataSource::StartNewPacketIfNeeded() {
  if (cur_packet_)
    return;
  cur_packet_ = writer_->NewTracePacket();
  cur_packet_->set_timestamp(CacheProcFsScanStartTimestamp());

  if (did_clear_incremental_state_) {
    cur_packet_->set_incremental_state_cleared(true);
    did_clear_incremental_state_ = false;
  }
}

protos::pbzero::ProcessTree* ProcessStatsDataSource::GetOrCreatePsTree() {
  StartNewPacketIfNeeded();
  if (!cur_ps_tree_)
    cur_ps_tree_ = cur_packet_->set_process_tree();
  cur_ps_stats_ = nullptr;
  cur_ps_stats_process_ = nullptr;
  return cur_ps_tree_;
}

protos::pbzero::ProcessStats* ProcessStatsDataSource::GetOrCreateStats() {
  StartNewPacketIfNeeded();
  if (!cur_ps_stats_)
    cur_ps_stats_ = cur_packet_->set_process_stats();
  cur_ps_tree_ = nullptr;
  cur_ps_stats_process_ = nullptr;
  return cur_ps_stats_;
}

protos::pbzero::ProcessStats_Process*
ProcessStatsDataSource::GetOrCreateStatsProcess(int32_t pid) {
  if (cur_ps_stats_process_)
    return cur_ps_stats_process_;
  cur_ps_stats_process_ = GetOrCreateStats()->add_processes();
  cur_ps_stats_process_->set_pid(pid);
  return cur_ps_stats_process_;
}

void ProcessStatsDataSource::FinalizeCurPacket() {
  PERFETTO_DCHECK(!cur_ps_tree_ || cur_packet_);
  PERFETTO_DCHECK(!cur_ps_stats_ || cur_packet_);
  uint64_t now = static_cast<uint64_t>(base::GetBootTimeNs().count());
  if (cur_ps_tree_) {
    cur_ps_tree_->set_collection_end_timestamp(now);
    cur_ps_tree_ = nullptr;
  }
  if (cur_ps_stats_) {
    cur_ps_stats_->set_collection_end_timestamp(now);
    cur_ps_stats_ = nullptr;
  }
  cur_ps_stats_process_ = nullptr;
  cur_procfs_scan_start_timestamp_ = 0;
  cur_packet_ = TraceWriter::TracePacketHandle{};
}

// static
void ProcessStatsDataSource::Tick(
    base::WeakPtr<ProcessStatsDataSource> weak_this) {
  if (!weak_this)
    return;
  ProcessStatsDataSource& thiz = *weak_this;
  uint32_t period_ms = thiz.poll_period_ms_;
  uint32_t delay_ms =
      period_ms -
      static_cast<uint32_t>(base::GetWallTimeMs().count() % period_ms);
  thiz.task_runner_->PostDelayedTask(
      std::bind(&ProcessStatsDataSource::Tick, weak_this), delay_ms);
  thiz.WriteAllProcessStats();

  // We clear the cache every process_stats_cache_ttl_ticks_ ticks.
  if (++thiz.cache_ticks_ == thiz.process_stats_cache_ttl_ticks_) {
    thiz.cache_ticks_ = 0;
    thiz.process_stats_cache_.clear();
  }
}

void ProcessStatsDataSource::WriteAllProcessStats() {
  CacheProcFsScanStartTimestamp();
  PERFETTO_METATRACE_SCOPED(TAG_PROC_POLLERS, PS_WRITE_ALL_PROCESS_STATS);
  base::ScopedDir proc_dir = OpenProcDir();
  if (!proc_dir)
    return;
  base::FlatSet<int32_t> pids;
  while (int32_t pid = ReadNextNumericDir(*proc_dir)) {
    cur_ps_stats_process_ = nullptr;
    uint32_t pid_u = static_cast<uint32_t>(pid);

    // optional /proc/pid/stat fields
    if (record_process_runtime_) {
      std::string proc_stat = ReadProcPidFile(pid, "stat");
      if (WriteProcessRuntimes(pid, proc_stat)) {
        pids.insert(pid);
      }
    }

    // memory counters
    if (skip_mem_for_pids_.size() > pid_u && skip_mem_for_pids_[pid_u])
      continue;

    std::string proc_status = ReadProcPidFile(pid, "status");
    if (proc_status.empty())
      continue;

    if (record_process_dmabuf_rss_) {
      std::string dmabuf_rss = ReadProcPidFile(pid, "dmabuf_rss");
      if (!dmabuf_rss.empty()) {
        CachedProcessStats& cached = process_stats_cache_[pid];
        uint32_t counter = static_cast<uint32_t>(ToUInt64(dmabuf_rss) / 1024);
        if (counter != cached.dmabuf_rss_kb) {
          GetOrCreateStatsProcess(pid)->set_dmabuf_rss_kb(counter);
          cached.dmabuf_rss_kb = counter;
        }
      }
    }

    if (scan_smaps_rollup_) {
      std::string proc_smaps_rollup = ReadProcPidFile(pid, "smaps_rollup");
      proc_status.append(proc_smaps_rollup);
    }

    if (!WriteMemCounters(pid, proc_status)) {
      // If WriteMemCounters() fails the pid is very likely a kernel thread
      // that has a valid /proc/[pid]/status but no memory values. In this
      // case avoid keep polling it over and over.
      if (skip_mem_for_pids_.size() <= pid_u)
        skip_mem_for_pids_.resize(pid_u + 1);
      skip_mem_for_pids_[pid_u] = true;
      continue;
    }

    std::string oom_score_adj = ReadProcPidFile(pid, "oom_score_adj");
    if (!oom_score_adj.empty()) {
      CachedProcessStats& cached = process_stats_cache_[pid];
      int32_t counter = ToInt32(oom_score_adj);
      if (counter != cached.oom_score_adj) {
        GetOrCreateStatsProcess(pid)->set_oom_score_adj(counter);
        cached.oom_score_adj = counter;
      }
    }

    // Ensure we write data on any fds not seen before (niche option).
    WriteFds(pid);

    pids.insert(pid);
  }
  FinalizeCurPacket();

  // Ensure that we write once long-term process info (e.g., name) for new pids
  // that we haven't seen before.
  WriteProcessTree(pids);
}

bool ProcessStatsDataSource::WriteProcessRuntimes(
    int32_t pid,
    const std::string& proc_stat) {
  std::optional<ProcessRuntimes> times = ParseProcessRuntimes(proc_stat);
  if (!times.has_value())
    return false;

  CachedProcessStats& cached = process_stats_cache_[pid];
  if (times->utime != cached.runtime_user_mode_ns) {
    GetOrCreateStatsProcess(pid)->set_runtime_user_mode(times->utime);
    cached.runtime_user_mode_ns = times->utime;
  }
  if (times->stime != cached.runtime_kernel_mode_ns) {
    GetOrCreateStatsProcess(pid)->set_runtime_kernel_mode(times->stime);
    cached.runtime_kernel_mode_ns = times->stime;
  }
  return true;
}

// Returns true if the stats for the given |pid| have been written, false it
// it failed (e.g., |pid| was a kernel thread and, as such, didn't report any
// memory counters).
bool ProcessStatsDataSource::WriteMemCounters(int32_t pid,
                                              const std::string& proc_status) {
  bool proc_status_has_mem_counters = false;
  CachedProcessStats& cached = process_stats_cache_[pid];

  // Parse /proc/[pid]/status, which looks like this:
  // Name:   cat
  // Umask:  0027
  // State:  R (running)
  // FDSize: 256
  // Groups: 4 20 24 46 997
  // VmPeak:     5992 kB
  // VmSize:     5992 kB
  // VmLck:         0 kB
  // ...
  std::vector<char> key;
  std::vector<char> value;
  enum { kKey, kSeparator, kValue } state = kKey;
  for (char c : proc_status) {
    if (c == '\n') {
      key.push_back('\0');
      value.push_back('\0');

      // |value| will contain "1234 KB". We rely on ToUInt32() to stop parsing
      // at the first non-numeric character.
      if (strcmp(key.data(), "VmSize") == 0) {
        // Assume that if we see VmSize we'll see also the others.
        proc_status_has_mem_counters = true;

        auto counter = ToUInt32(value.data());
        if (counter != cached.vm_size_kb) {
          GetOrCreateStatsProcess(pid)->set_vm_size_kb(counter);
          cached.vm_size_kb = counter;
        }
      } else if (strcmp(key.data(), "VmLck") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.vm_locked_kb) {
          GetOrCreateStatsProcess(pid)->set_vm_locked_kb(counter);
          cached.vm_locked_kb = counter;
        }
      } else if (strcmp(key.data(), "VmHWM") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.vm_hvm_kb) {
          GetOrCreateStatsProcess(pid)->set_vm_hwm_kb(counter);
          cached.vm_hvm_kb = counter;
        }
      } else if (strcmp(key.data(), "VmRSS") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.vm_rss_kb) {
          GetOrCreateStatsProcess(pid)->set_vm_rss_kb(counter);
          cached.vm_rss_kb = counter;
        }
      } else if (strcmp(key.data(), "RssAnon") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.rss_anon_kb) {
          GetOrCreateStatsProcess(pid)->set_rss_anon_kb(counter);
          cached.rss_anon_kb = counter;
        }
      } else if (strcmp(key.data(), "RssFile") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.rss_file_kb) {
          GetOrCreateStatsProcess(pid)->set_rss_file_kb(counter);
          cached.rss_file_kb = counter;
        }
      } else if (strcmp(key.data(), "RssShmem") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.rss_shmem_kb) {
          GetOrCreateStatsProcess(pid)->set_rss_shmem_kb(counter);
          cached.rss_shmem_kb = counter;
        }
      } else if (strcmp(key.data(), "VmSwap") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.vm_swap_kb) {
          GetOrCreateStatsProcess(pid)->set_vm_swap_kb(counter);
          cached.vm_swap_kb = counter;
        }
        // The entries below come from smaps_rollup, WriteAllProcessStats merges
        // everything into the same buffer for convenience.
      } else if (strcmp(key.data(), "Rss") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.smr_rss_kb) {
          GetOrCreateStatsProcess(pid)->set_smr_rss_kb(counter);
          cached.smr_rss_kb = counter;
        }
      } else if (strcmp(key.data(), "Pss") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.smr_pss_kb) {
          GetOrCreateStatsProcess(pid)->set_smr_pss_kb(counter);
          cached.smr_pss_kb = counter;
        }
      } else if (strcmp(key.data(), "Pss_Anon") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.smr_pss_anon_kb) {
          GetOrCreateStatsProcess(pid)->set_smr_pss_anon_kb(counter);
          cached.smr_pss_anon_kb = counter;
        }
      } else if (strcmp(key.data(), "Pss_File") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.smr_pss_file_kb) {
          GetOrCreateStatsProcess(pid)->set_smr_pss_file_kb(counter);
          cached.smr_pss_file_kb = counter;
        }
      } else if (strcmp(key.data(), "Pss_Shmem") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.smr_pss_shmem_kb) {
          GetOrCreateStatsProcess(pid)->set_smr_pss_shmem_kb(counter);
          cached.smr_pss_shmem_kb = counter;
        }
      } else if (strcmp(key.data(), "SwapPss") == 0) {
        auto counter = ToUInt32(value.data());
        if (counter != cached.smr_swap_pss_kb) {
          GetOrCreateStatsProcess(pid)->set_smr_swap_pss_kb(counter);
          cached.smr_swap_pss_kb = counter;
        }
      }

      key.clear();
      state = kKey;
      continue;
    }

    if (state == kKey) {
      if (c == ':') {
        state = kSeparator;
        continue;
      }
      key.push_back(c);
      continue;
    }

    if (state == kSeparator) {
      if (isspace(c))
        continue;
      value.clear();
      value.push_back(c);
      state = kValue;
      continue;
    }

    if (state == kValue) {
      value.push_back(c);
    }
  }
  return proc_status_has_mem_counters;
}

void ProcessStatsDataSource::WriteFds(int32_t pid) {
  if (!resolve_process_fds_) {
    return;
  }

  base::StackString<256> path("%s/%" PRId32 "/fd", GetProcMountpoint(), pid);
  base::ScopedDir proc_dir(opendir(path.c_str()));
  if (!proc_dir) {
    PERFETTO_DPLOG("Failed to opendir(%s)", path.c_str());
    return;
  }
  while (struct dirent* dir_ent = readdir(*proc_dir)) {
    if (dir_ent->d_type != DT_LNK)
      continue;
    auto fd = base::CStringToUInt64(dir_ent->d_name);
    if (fd)
      WriteSingleFd(pid, *fd);
  }
}

void ProcessStatsDataSource::WriteSingleFd(int32_t pid, uint64_t fd) {
  CachedProcessStats& cached = process_stats_cache_[pid];
  if (cached.seen_fds.count(fd)) {
    return;
  }

  base::StackString<128> proc_fd("%s/%" PRId32 "/fd/%" PRIu64,
                                 GetProcMountpoint(), pid, fd);
  std::array<char, 256> path;
  ssize_t actual = readlink(proc_fd.c_str(), path.data(), path.size());
  if (actual >= 0) {
    auto* fd_info = GetOrCreateStatsProcess(pid)->add_fds();
    fd_info->set_fd(fd);
    fd_info->set_path(path.data(), static_cast<size_t>(actual));
    cached.seen_fds.insert(fd);
  } else if (ENOENT != errno) {
    PERFETTO_DPLOG("Failed to readlink '%s'", proc_fd.c_str());
  }
}

uint64_t ProcessStatsDataSource::CacheProcFsScanStartTimestamp() {
  if (!cur_procfs_scan_start_timestamp_)
    cur_procfs_scan_start_timestamp_ =
        static_cast<uint64_t>(base::GetBootTimeNs().count());
  return cur_procfs_scan_start_timestamp_;
}

void ProcessStatsDataSource::ClearIncrementalState() {
  PERFETTO_DLOG("ProcessStatsDataSource clearing incremental state.");
  seen_pids_.clear();
  skip_mem_for_pids_.clear();

  cache_ticks_ = 0;
  process_stats_cache_.clear();

  // Set the relevant flag in the next packet.
  did_clear_incremental_state_ = true;
}

}  // namespace perfetto
