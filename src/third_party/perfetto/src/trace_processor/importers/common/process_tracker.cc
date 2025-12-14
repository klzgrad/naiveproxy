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

#include "src/trace_processor/importers/common/process_tracker.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

ProcessTracker::ProcessTracker(TraceProcessorContext* context)
    : context_(context), args_tracker_(context) {
  // Reserve utid/upid 0. These are special as embedders (e.g. Perfetto UI)
  // exclude them from certain views (e.g. thread state) under the assumption
  // that they correspond to the idle (swapper) process. When parsing Linux
  // system traces, SetPidZeroIsUpidZeroIdleProcess will be called to associate
  // tid0/pid0 to utid0/upid0. If other types of traces refer to tid0/pid0,
  // then they will get their own non-zero utid/upid, so that those threads are
  // still surfaced in embedder UIs.
  //
  // Note on multi-machine tracing: utid/upid of the swapper process of
  // secondary machine will not be 0. The ProcessTracker needs to insert to the
  // thread and process tables to reserve utid and upid.
  tables::ProcessTable::Row process_row;
  process_row.pid = 0u;
  process_row.machine_id = context_->machine_id();
  auto upid =
      context_->storage->mutable_process_table()->Insert(process_row).row;

  tables::ThreadTable::Row thread_row;
  thread_row.tid = 0u;
  thread_row.upid = upid;  // The swapper upid may be != 0 for remote machines.
  thread_row.is_main_thread = true;
  thread_row.is_idle = true;
  thread_row.machine_id = context_->machine_id();
  auto utid = context_->storage->mutable_thread_table()->Insert(thread_row).row;

  swapper_upid_ = upid;
  swapper_utid_ = utid;

  // An element to match the reserved tid = 0.
  thread_name_priorities_.push_back(ThreadNamePriority::kOther);
}

ProcessTracker::~ProcessTracker() = default;

UniqueTid ProcessTracker::StartNewThread(std::optional<int64_t> timestamp,
                                         int64_t tid) {
  tables::ThreadTable::Row row;
  row.tid = tid;
  row.start_ts = timestamp;
  row.machine_id = context_->machine_id();

  auto* thread_table = context_->storage->mutable_thread_table();
  UniqueTid new_utid = thread_table->Insert(row).row;
  tids_[tid].emplace_back(new_utid);

  if (PERFETTO_UNLIKELY(thread_name_priorities_.size() <= new_utid)) {
    // This condition can happen in a multi-machine tracing session:
    // Machine 1 gets utid 0, 1
    // Machine 2 gets utid 2, 3
    // Machine 1 gets utid 4: where thread_name_priorities_.size() == 2.
    thread_name_priorities_.resize(new_utid + 1);
  }
  thread_name_priorities_[new_utid] = ThreadNamePriority::kOther;
  return new_utid;
}

void ProcessTracker::EndThread(int64_t timestamp, int64_t tid) {
  auto& thread_table = *context_->storage->mutable_thread_table();
  auto& process_table = *context_->storage->mutable_process_table();

  // Don't bother creating a new thread if we're just going to
  // end it straight away.
  //
  // This is useful in situations where we get a sched_process_free event for a
  // worker thread in a process *after* the main thread finishes - in that case
  // we would have already ended the process and we don't want to
  // create a new thread here (see b/193520421 for an example of a trace
  // where this happens in practice).
  std::optional<UniqueTid> opt_utid = GetThreadOrNull(tid);
  if (!opt_utid)
    return;

  UniqueTid utid = *opt_utid;

  auto td = thread_table[utid];
  td.set_end_ts(timestamp);

  // Remove the thread from the list of threads being tracked as any event after
  // this one should be ignored.
  auto& vector = tids_[tid];
  vector.erase(std::remove(vector.begin(), vector.end(), utid), vector.end());

  auto opt_upid = td.upid();
  if (!opt_upid) {
    return;
  }
  auto ps = process_table[*opt_upid];
  if (ps.pid() != tid) {
    return;
  }

  // If the process pid and thread tid are equal then, as is the main thread
  // of the process, we should also finish the process itself.
  PERFETTO_DCHECK(*td.is_main_thread());
  ps.set_end_ts(timestamp);
  pids_.Erase(tid);
}

std::optional<UniqueTid> ProcessTracker::GetThreadOrNull(int64_t tid) {
  return GetThreadOrNull(tid, std::nullopt);
}

UniqueTid ProcessTracker::GetOrCreateThread(int64_t tid) {
  auto utid = GetThreadOrNull(tid);
  return utid ? *utid : StartNewThread(std::nullopt, tid);
}

UniqueTid ProcessTracker::GetOrCreateThreadWithParentInternal(
    int64_t tid,
    UniquePid upid,
    bool is_main_thread,
    bool associate_main_threads) {
  auto& thread_table = *context_->storage->mutable_thread_table();
  auto& process_table = *context_->storage->mutable_process_table();

  auto ps = process_table[upid];

  auto opt_utid = GetThreadOrNull(tid, ps.pid());
  UniqueTid utid = opt_utid ? *opt_utid : StartNewThread(std::nullopt, tid);

  auto td = thread_table[utid];
  PERFETTO_DCHECK(td.tid() == tid);
  // Ensure that the thread's machine ID matches the context's machine ID.
  PERFETTO_DCHECK(td.machine_id() == context_->machine_id());

  if (!td.upid().has_value()) {
    AssociateThreadToProcessInternal(utid, upid, is_main_thread);
  }
  ResolvePendingAssociations(utid, *td.upid(), associate_main_threads);

  return utid;
}

UniqueTid ProcessTracker::GetOrCreateThreadWithParent(
    int64_t tid,
    UniquePid upid,
    bool associate_main_threads) {
  return GetOrCreateThreadWithParentInternal(
      tid, upid, /*is_main_thread*/ false, associate_main_threads);
}

void ProcessTracker::UpdateThreadName(UniqueTid utid,
                                      StringId thread_name_id,
                                      ThreadNamePriority priority) {
  if (thread_name_id.is_null())
    return;

  auto& thread_table = *context_->storage->mutable_thread_table();
  if (PERFETTO_UNLIKELY(thread_name_priorities_.size() <= utid)) {
    // This condition can happen in a multi-machine tracing session:
    // Machine 1 gets utid 0, 1
    // Machine 2 gets utid 2, 3
    // Machine 1 gets utid 4: where thread_name_priorities_.size() == 2.
    thread_name_priorities_.resize(utid + 1);
  }
  if (priority >= thread_name_priorities_[utid]) {
    thread_table[utid].set_name(thread_name_id);
    thread_name_priorities_[utid] = priority;
  }
}

bool ProcessTracker::IsThreadAlive(UniqueTid utid) {
  auto& threads = *context_->storage->mutable_thread_table();
  auto& processes = *context_->storage->mutable_process_table();

  // If the thread has an end ts, it's certainly dead.
  auto rr = threads[utid];
  if (rr.end_ts().has_value())
    return false;

  // If we don't know the parent process, we have to consider this thread alive.
  auto opt_current_upid = rr.upid();
  if (!opt_current_upid)
    return true;

  // If the process is already dead, the thread can't be alive.
  UniquePid current_upid = *opt_current_upid;
  auto prr = processes[current_upid];
  if (prr.end_ts().has_value())
    return false;

  // If the process has been replaced in |pids_|, this thread is dead.
  int64_t current_pid = prr.pid();
  auto* pid_it = pids_.Find(current_pid);
  return !pid_it || *pid_it == current_upid;
}

std::optional<UniqueTid> ProcessTracker::GetThreadOrNull(
    int64_t tid,
    std::optional<int64_t> pid) {
  auto& threads = *context_->storage->mutable_thread_table();
  auto& processes = *context_->storage->mutable_process_table();

  auto* vector_it = tids_.Find(tid);
  if (!vector_it)
    return std::nullopt;

  // Iterate backwards through the threads so ones later in the trace are more
  // likely to be picked.
  const auto& vector = *vector_it;
  for (auto it = vector.rbegin(); it != vector.rend(); it++) {
    UniqueTid current_utid = *it;
    auto rr = threads[current_utid];

    // Ensure that the tid matches the tid we were looking for.
    PERFETTO_DCHECK(rr.tid() == tid);
    // Ensure that the thread's machine ID matches the context's machine ID.
    PERFETTO_DCHECK(rr.machine_id() == context_->machine_id());
    // If we finished this thread, we should have removed it from the vector
    // entirely.
    PERFETTO_DCHECK(!rr.end_ts().has_value());

    // If the thread is dead, ignore it.
    if (!IsThreadAlive(current_utid))
      continue;

    // If we don't know the parent process, we have to choose this thread.
    auto opt_current_upid = rr.upid();
    if (!opt_current_upid)
      return current_utid;

    // We found a thread that matches both the tid and its parent pid.
    auto prr = processes[*opt_current_upid];
    int64_t current_pid = prr.pid();
    if (!pid || current_pid == *pid)
      return current_utid;
  }
  return std::nullopt;
}

UniqueTid ProcessTracker::UpdateThread(int64_t tid, int64_t pid) {
  return GetOrCreateThreadWithParentInternal(tid, GetOrCreateProcess(pid),
                                             /*is_main_thread*/ tid == pid,
                                             /*associate_main_threads*/ true);
}

void ProcessTracker::UpdateTrustedPid(int64_t trusted_pid, uint64_t uuid) {
  trusted_pids_[uuid] = trusted_pid;
}

std::optional<int64_t> ProcessTracker::GetTrustedPid(uint64_t uuid) {
  if (trusted_pids_.find(uuid) == trusted_pids_.end())
    return std::nullopt;
  return trusted_pids_[uuid];
}

std::optional<int64_t> ProcessTracker::ResolveNamespacedTid(
    int64_t root_level_pid,
    int64_t tid) {
  if (root_level_pid <= 0)  // Not a valid pid.
    return std::nullopt;

  // If the process doesn't run in a namespace (or traced_probes doesn't observe
  // that), return std::nullopt as failure to resolve.
  auto process_it = namespaced_processes_.find(root_level_pid);
  if (process_it == namespaced_processes_.end())
    return std::nullopt;

  // Check if it's the main thread.
  const auto& process = process_it->second;
  auto ns_level = process.nspid.size() - 1;
  auto pid_local = process.nspid.back();
  if (pid_local == tid)
    return root_level_pid;

  // Check if any non-main thread has a matching ns-local thread ID.
  for (const auto& root_level_tid : process.threads) {
    const auto& thread = namespaced_threads_[root_level_tid];
    PERFETTO_DCHECK(thread.nstid.size() > ns_level);
    auto tid_ns_local = thread.nstid[ns_level];
    if (tid_ns_local == tid)
      return thread.tid;
  }

  // Failed to resolve or the thread isn't namespaced
  return std::nullopt;
}

UniquePid ProcessTracker::StartNewProcessInternal(
    std::optional<int64_t> timestamp,
    std::optional<UniquePid> parent_upid,
    int64_t pid,
    StringId process_name,
    ThreadNamePriority priority,
    bool associate_main_thread) {
  pids_.Erase(pid);

  // Same pid is never used concurrently by multiple processes, therefore remove
  // the tid completely
  tids_.Erase(pid);

  // Note that we erased the pid above so this should always return a new
  // process.
  if (associate_main_thread) {
    // Create a new UTID for the main thread, so we don't end up reusing an old
    // entry in case of TID recycling.
    UniqueTid utid = StartNewThread(timestamp, /*tid=*/pid);
    UpdateThreadName(utid, process_name, priority);
  }

  UniquePid upid = GetOrCreateProcessInternal(pid, associate_main_thread);

  auto& process_table = *context_->storage->mutable_process_table();

  auto prr = process_table[upid];
  PERFETTO_DCHECK(!prr.name().has_value());
  PERFETTO_DCHECK(!prr.start_ts().has_value());

  if (timestamp) {
    prr.set_start_ts(*timestamp);
  }
  prr.set_name(process_name);

  if (parent_upid) {
    prr.set_parent_upid(*parent_upid);
  }
  return upid;
}

UniquePid ProcessTracker::StartNewProcess(std::optional<int64_t> timestamp,
                                          std::optional<UniquePid> parent_upid,
                                          int64_t pid,
                                          StringId process_name,
                                          ThreadNamePriority priority) {
  return StartNewProcessInternal(timestamp, parent_upid, pid, process_name,
                                 priority, /*associate_main_thread=*/true);
}

UniquePid ProcessTracker::StartNewProcessWithoutMainThread(
    std::optional<int64_t> timestamp,
    std::optional<UniquePid> parent_upid,
    int64_t pid,
    StringId process_name,
    ThreadNamePriority priority) {
  return StartNewProcessInternal(timestamp, parent_upid, pid, process_name,
                                 priority, /*associate_main_thread=*/false);
}

void ProcessTracker::AssociateCreatedProcessToParentThread(
    UniquePid upid,
    UniqueTid parent_utid) {
  auto& process_table = *context_->storage->mutable_process_table();
  auto& thread_table = *context_->storage->mutable_thread_table();

  auto prr = process_table[upid];

  auto opt_parent_upid = thread_table[parent_utid].upid();
  if (opt_parent_upid.has_value()) {
    prr.set_parent_upid(*opt_parent_upid);
  } else {
    pending_parent_assocs_.emplace_back(parent_utid, upid);
  }
}

UniquePid ProcessTracker::UpdateProcessWithParent(UniquePid upid,
                                                  UniquePid pupid,
                                                  bool associate_main_thread) {
  auto& process_table = *context_->storage->mutable_process_table();

  auto prr = process_table[upid];

  // If the previous and new parent pid don't match, the process must have
  // died and the pid reused. Create a new process.
  std::optional<UniquePid> prev_parent_upid = prr.parent_upid();
  if (prev_parent_upid && *prev_parent_upid != pupid) {
    upid = StartNewProcessInternal(std::nullopt, pupid, prr.pid(),
                                   kNullStringId, ThreadNamePriority::kOther,
                                   associate_main_thread);
  } else {
    prr.set_parent_upid(pupid);
  }
  return upid;
}

void ProcessTracker::SetProcessMetadata(UniquePid upid,
                                        base::StringView name,
                                        base::StringView cmdline) {
  auto& process_table = *context_->storage->mutable_process_table();
  auto prr = process_table[upid];
  StringId proc_name_id = context_->storage->InternString(name);
  prr.set_name(proc_name_id);
  prr.set_cmdline(context_->storage->InternString(cmdline));
}

void ProcessTracker::SetProcessUid(UniquePid upid, uint32_t uid) {
  auto& process_table = *context_->storage->mutable_process_table();
  auto rr = process_table[upid];
  rr.set_uid(uid);

  // The notion of the app ID (as derived from the uid) is defined in
  // frameworks/base/core/java/android/os/UserHandle.java
  rr.set_android_appid(uid % 100000);
  rr.set_android_user_id(uid / 100000);
}

void ProcessTracker::SetProcessNameIfUnset(UniquePid upid,
                                           StringId process_name_id) {
  auto& pt = *context_->storage->mutable_process_table();
  if (auto rr = pt[upid]; !rr.name().has_value()) {
    rr.set_name(process_name_id);
  }
}

void ProcessTracker::SetStartTsIfUnset(UniquePid upid,
                                       int64_t start_ts_nanoseconds) {
  auto& pt = *context_->storage->mutable_process_table();
  if (auto rr = pt[upid]; !rr.start_ts().has_value()) {
    rr.set_start_ts(start_ts_nanoseconds);
  }
}

void ProcessTracker::UpdateThreadNameAndMaybeProcessName(
    UniqueTid utid,
    StringId thread_name,
    ThreadNamePriority priority) {
  auto& tt = *context_->storage->mutable_thread_table();
  auto& pt = *context_->storage->mutable_process_table();

  UpdateThreadName(utid, thread_name, priority);
  auto trr = tt[utid];
  std::optional<UniquePid> opt_upid = trr.upid();
  if (!opt_upid.has_value()) {
    return;
  }
  auto prr = pt[*opt_upid];
  if (prr.pid() == trr.tid()) {
    PERFETTO_DCHECK(trr.is_main_thread());
    prr.set_name(thread_name);
  }
}

UniquePid ProcessTracker::GetOrCreateProcessInternal(
    int64_t pid,
    bool associate_main_thread) {
  auto& process_table = *context_->storage->mutable_process_table();

  // If the insertion succeeds, we'll fill the upid below.
  auto it_and_ins = pids_.Insert(pid, UniquePid{0});
  if (!it_and_ins.second) {
    // Ensure that the process has not ended.
    PERFETTO_DCHECK(!process_table[*it_and_ins.first].end_ts().has_value());
    return *it_and_ins.first;
  }

  tables::ProcessTable::Row row;
  row.pid = pid;
  row.machine_id = context_->machine_id();

  UniquePid upid = process_table.Insert(row).row;
  *it_and_ins.first = upid;  // Update the newly inserted hashmap entry.

  if (associate_main_thread) {
    // Create an entry for the main thread.
    // We cannot call StartNewThread() here, because threads for this process
    // (including the main thread) might have been seen already prior to this
    // call. This call usually comes from the ProcessTree dump which is delayed.
    UpdateThread(/*tid=*/pid, pid);
  }
  return upid;
}

UniquePid ProcessTracker::GetOrCreateProcess(int64_t pid) {
  return GetOrCreateProcessInternal(pid, /*associate_main_thread=*/true);
}

UniquePid ProcessTracker::GetOrCreateProcessWithoutMainThread(int64_t pid) {
  return GetOrCreateProcessInternal(pid, /*associate_main_thread=*/false);
}

void ProcessTracker::AssociateThreads(UniqueTid utid1,
                                      UniqueTid utid2,
                                      bool associate_main_threads) {
  auto& tt = *context_->storage->mutable_thread_table();
  auto& pt = *context_->storage->mutable_process_table();

  // First of all check if one of the two threads is already bound to a process.
  // If that is the case, map the other thread to the same process and resolve
  // recursively any associations pending on the other thread.

  auto rr1 = tt[utid1];
  auto rr2 = tt[utid2];
  auto opt_upid1 = rr1.upid();
  auto opt_upid2 = rr2.upid();

  if (opt_upid1.has_value() && !opt_upid2.has_value()) {
    auto prr = pt[*opt_upid1];
    bool is_main_thread = associate_main_threads && rr2.tid() == prr.pid();
    AssociateThreadToProcessInternal(utid2, *opt_upid1, is_main_thread);
    ResolvePendingAssociations(utid2, *opt_upid1, associate_main_threads);
    return;
  }

  if (opt_upid2.has_value() && !opt_upid1.has_value()) {
    auto prr = pt[*opt_upid2];
    bool is_main_thread = associate_main_threads && rr1.tid() == prr.pid();
    AssociateThreadToProcessInternal(utid1, *opt_upid2, is_main_thread);
    ResolvePendingAssociations(utid1, *opt_upid2, associate_main_threads);
    return;
  }

  if (opt_upid1.has_value() && opt_upid1 != opt_upid2) {
    // Cannot associate two threads that belong to two different processes.
    PERFETTO_ELOG("Process tracker failure. Cannot associate threads %ld, %ld",
                  static_cast<long>(rr1.tid()), static_cast<long>(rr2.tid()));
    context_->storage->IncrementStats(stats::process_tracker_errors);
    return;
  }

  pending_assocs_.emplace_back(utid1, utid2);
}

void ProcessTracker::ResolvePendingAssociations(UniqueTid utid_arg,
                                                UniquePid upid,
                                                bool associate_main_threads) {
  auto& tt = *context_->storage->mutable_thread_table();
  auto& pt = *context_->storage->mutable_process_table();

  auto trr = tt[utid_arg];
  PERFETTO_DCHECK(trr.upid() == upid);

  std::vector<UniqueTid> resolved_utids;
  resolved_utids.emplace_back(utid_arg);

  while (!resolved_utids.empty()) {
    UniqueTid utid = resolved_utids.back();
    resolved_utids.pop_back();
    for (auto it = pending_parent_assocs_.begin();
         it != pending_parent_assocs_.end();) {
      UniqueTid parent_utid = it->first;
      UniquePid child_upid = it->second;

      if (parent_utid != utid) {
        ++it;
        continue;
      }
      PERFETTO_DCHECK(child_upid != upid);

      // Set the parent pid of the other process
      auto crr = pt[child_upid];
      PERFETTO_DCHECK(!crr.parent_upid() || crr.parent_upid() == upid);
      crr.set_parent_upid(upid);

      // Erase the pair. The |pending_parent_assocs_| vector is not sorted and
      // swapping a std::pair<uint32_t, uint32_t> is cheap.
      std::swap(*it, pending_parent_assocs_.back());
      pending_parent_assocs_.pop_back();
    }

    auto end = pending_assocs_.end();
    for (auto it = pending_assocs_.begin(); it != end;) {
      UniqueTid other_utid;
      if (it->first == utid) {
        other_utid = it->second;
      } else if (it->second == utid) {
        other_utid = it->first;
      } else {
        ++it;
        continue;
      }

      PERFETTO_DCHECK(other_utid != utid);

      // Update the other thread and associated it to the same process.
      auto orr = tt[other_utid];
      auto parent_prr = pt[upid];
      PERFETTO_DCHECK(!orr.upid() || orr.upid() == upid);
      bool is_main_thread =
          associate_main_threads && orr.tid() == parent_prr.pid();
      AssociateThreadToProcessInternal(other_utid, upid, is_main_thread);

      // Swap the current element to the end of the list and move the end
      // iterator back. This works because |pending_assocs_| is not sorted. We
      // do it this way rather than modifying |pending_assocs_| directly to
      // prevent undefined behaviour caused by modifying a vector while
      // iterating through it.
      std::swap(*it, *(--end));

      // Recurse into the newly resolved thread. Some other threads might have
      // been bound to that.
      resolved_utids.emplace_back(other_utid);
    }

    // Make sure to actually erase the utids which have been resolved.
    pending_assocs_.erase(end, pending_assocs_.end());
  }  // while (!resolved_utids.empty())
}

void ProcessTracker::AssociateThreadToProcessInternal(UniqueTid utid,
                                                      UniquePid upid,
                                                      bool is_main_thread) {
  auto& thread_table = *context_->storage->mutable_thread_table();

  auto trr = thread_table[utid];
  trr.set_upid(upid);
  trr.set_is_main_thread(is_main_thread);
}

void ProcessTracker::SetMainThread(UniqueTid utid, bool is_main_thread) {
  auto& thread_table = *context_->storage->mutable_thread_table();

  auto trr = thread_table[utid];
  trr.set_is_main_thread(is_main_thread);
}

void ProcessTracker::SetPidZeroIsUpidZeroIdleProcess() {
  // Create a mapping from (t|p)id 0 -> u(t|p)id for the idle process.
  tids_.Insert(0, std::vector<UniqueTid>{swapper_utid_});
  pids_.Insert(0, swapper_upid_);

  auto swapper_id = context_->storage->InternString("swapper");
  auto utid = GetOrCreateThread(0);
  UpdateThreadName(utid, swapper_id,
                   ThreadNamePriority::kTraceProcessorConstant);
}

ArgsTracker::BoundInserter ProcessTracker::AddArgsToProcess(UniquePid upid) {
  return args_tracker_.AddArgsToProcess(upid);
}

ArgsTracker::BoundInserter ProcessTracker::AddArgsToThread(UniqueTid utid) {
  return args_tracker_.AddArgsToThread(utid);
}

void ProcessTracker::NotifyEndOfFile() {
  args_tracker_.Flush();
  tids_.Clear();
  pids_.Clear();
  pending_assocs_.clear();
  pending_parent_assocs_.clear();
  thread_name_priorities_.clear();
  trusted_pids_.clear();
  namespaced_threads_.clear();
  namespaced_processes_.clear();
}

void ProcessTracker::UpdateNamespacedProcess(int64_t pid,
                                             std::vector<int64_t> nspid) {
  namespaced_processes_[pid] = {pid, std::move(nspid), {}};
}

bool ProcessTracker::UpdateNamespacedThread(int64_t pid,
                                            int64_t tid,
                                            std::vector<int64_t> nstid) {
  // It's possible with data loss that we collect the thread namespace
  // information but not the process. In that case, just ignore the thread
  // association.
  if (namespaced_processes_.find(pid) == namespaced_processes_.end()) {
    return false;
  }
  auto& process = namespaced_processes_[pid];
  process.threads.emplace(tid);

  namespaced_threads_[tid] = {pid, tid, std::move(nstid)};
  return true;
}

}  // namespace perfetto::trace_processor
