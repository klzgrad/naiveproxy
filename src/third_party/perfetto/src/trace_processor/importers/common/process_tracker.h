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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_PROCESS_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_PROCESS_TRACKER_H_

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

// Thread names can come from different sources, and we don't always want to
// overwrite the previously set name. This enum determines the priority of
// different sources.
enum class ThreadNamePriority : uint8_t {
  kOther = 0,
  kFtrace = 1,
  kEtwTrace = 1,
  kGenericKernelTask = 1,
  kProcessTree = 2,
  kTrackDescriptorThreadType = 3,
  kTrackDescriptor = 4,

  // Priority when trace processor hardcodes a name for a process (e.g. calling
  // the idle thread "swapper" when parsing ftrace).
  // Keep this last.
  kTraceProcessorConstant = 5,
};

class ProcessTracker {
 public:
  explicit ProcessTracker(TraceProcessorContext*);
  ProcessTracker(const ProcessTracker&) = delete;
  ProcessTracker& operator=(const ProcessTracker&) = delete;
  virtual ~ProcessTracker();

  using UniqueThreadIterator = std::vector<UniqueTid>::const_iterator;
  using UniqueThreadBounds =
      std::pair<UniqueThreadIterator, UniqueThreadIterator>;

  // TODO(b/110409911): Invalidation of process and threads is yet to be
  // implemented. This will include passing timestamps into the below methods
  // to ensure the correct upid/utid is found.

  // Called when a task_newtask is observed. This force the tracker to start
  // a new UTID for the thread, which is needed for TID-recycling resolution.
  UniqueTid StartNewThread(std::optional<int64_t> timestamp, int64_t tid);

  // Returns whether a thread is considered alive by the process tracker.
  bool IsThreadAlive(UniqueTid utid);

  // Called when sched_process_exit is observed. This forces the tracker to
  // end the thread lifetime for the utid associated with the given tid.
  void EndThread(int64_t timestamp, int64_t tid);

  // Returns the thread utid or std::nullopt if it doesn't exist.
  std::optional<UniqueTid> GetThreadOrNull(int64_t tid);

  // Returns the thread utid (or creates a new entry if not present)
  UniqueTid GetOrCreateThread(int64_t tid);

  // Returns the utid of a thread whos parent matches the provided pid
  // or creates a new thread if not present. If a new thread is created,
  // it is never set as the main thread.
  UniqueTid GetOrCreateThreadWithParent(int64_t tid,
                                        UniquePid upid,
                                        bool associate_main_threads);

  // Assigns the given name to the thread if the new name has a higher priority
  // than the existing one. The thread is identified by utid.
  virtual void UpdateThreadName(UniqueTid utid,
                                StringId thread_name_id,
                                ThreadNamePriority priority);

  // Called when a thread is seen the process tree. Retrieves the matching utid
  // for the tid and the matching upid for the tgid and stores both.
  // Virtual for testing.
  virtual UniqueTid UpdateThread(int64_t tid, int64_t pid);

  // Mark whether a thread is the main thread or not.
  void SetMainThread(UniqueTid utid, bool is_main_thread);

  // Associates trusted_pid with track UUID.
  void UpdateTrustedPid(int64_t trusted_pid, uint64_t uuid);

  // Returns the trusted_pid associated with the track UUID, or std::nullopt if
  // not found.
  std::optional<int64_t> GetTrustedPid(uint64_t uuid);

  // Performs namespace-local to root-level resolution of thread or process id,
  // given tid (can be root-level or namespace-local, but we don't know
  // beforehand) and root-level pid/tgid that the thread belongs to.
  // Returns the root-level thread id for tid on successful resolution;
  // otherwise, returns std::nullopt on resolution failure, or the thread of
  // tid isn't running in a pid namespace.
  std::optional<int64_t> ResolveNamespacedTid(int64_t root_level_pid,
                                              int64_t tid);

  // Called when a task_newtask without the CLONE_THREAD flag is observed.
  // This force the tracker to start both a new UTID and a new UPID.
  // Virtual for testing.
  virtual UniquePid StartNewProcess(std::optional<int64_t> timestamp,
                                    std::optional<UniquePid> parent_upid,
                                    int64_t pid,
                                    StringId process_name,
                                    ThreadNamePriority priority);

  // Same as StartNewProcess, but doesn't create a main thread associated with
  // the process.
  UniquePid StartNewProcessWithoutMainThread(
      std::optional<int64_t> timestamp,
      std::optional<UniquePid> parent_upid,
      int64_t pid,
      StringId process_name,
      ThreadNamePriority priority);

  // Associates a process with its parent thread. Used when the tid that
  // created the process is known, but not the parent process. Exclusively,
  // used by Ftrace on new task events where only the parent tid is provided.
  void AssociateCreatedProcessToParentThread(UniquePid upid,
                                             UniqueTid parent_utid);

  // Updates a process' parent. If the upid was previously associated with
  // a different parent process, then the upid process is considered reused
  // and a new upid for a new process is returned. If no new process is
  // created, the same upid is returned.
  // Virtual for testing.
  virtual UniquePid UpdateProcessWithParent(UniquePid upid,
                                            UniquePid pupid,
                                            bool associate_main_thread);

  // Set the process metadata. Called when a process is seen in a process tree.
  // Virtual for testing.
  virtual void SetProcessMetadata(UniquePid upid,
                                  base::StringView name,
                                  base::StringView cmdline);

  // Sets the process user id.
  void SetProcessUid(UniquePid upid, uint32_t uid);

  // Assigns the given name to the process identified by |upid| if it does not
  // have a name yet.
  virtual void SetProcessNameIfUnset(UniquePid upid, StringId process_name_id);

  // Sets the start timestamp to the process identified by |upid| if it doesn't
  // have a timestamp yet.
  void SetStartTsIfUnset(UniquePid upid, int64_t start_ts_nanoseconds);

  // Called on a task rename event to set the thread name and possibly process
  // name (if the tid provided is the main thread of the process).
  void UpdateThreadNameAndMaybeProcessName(UniqueTid utid,
                                           StringId thread_name,
                                           ThreadNamePriority priority);

  // Called when a process is seen in a process tree. Retrieves the UniquePid
  // for that pid or assigns a new one.
  virtual UniquePid GetOrCreateProcess(int64_t pid);

  // Same as GetOrCreateProcess, but doesn't create a new main thread associated
  // to the pid.
  UniquePid GetOrCreateProcessWithoutMainThread(int64_t pid);

  // Returns the upid for a given pid.
  std::optional<UniquePid> UpidForPidForTesting(uint32_t pid) {
    auto* it = pids_.Find(pid);
    return it ? std::make_optional(*it) : std::nullopt;
  }

  // Returns the bounds of a range that includes all UniqueTids that have the
  // requested tid.
  UniqueThreadBounds UtidsForTidForTesting(int64_t tid) {
    const auto& deque = tids_[tid];
    return std::make_pair(deque.begin(), deque.end());
  }

  // Marks the two threads as belonging to the same process, even if we don't
  // know which one yet. If one of the two threads is later mapped to a process,
  // the other will be mapped to the same process. The order of the two threads
  // is irrelevant, Associate(A, B) has the same effect of Associate(B, A). The
  // associate_main_threads boolean parameter is used to determine if a thread
  // should be marked as the main thread if the tid and pid match, when
  // resolving process associations.
  void AssociateThreads(UniqueTid, UniqueTid, bool);

  // Creates the mapping from tid 0 <-> utid 0 and pid 0 <-> upid 0. This is
  // done for Linux-based system traces (proto or ftrace format) as for these
  // traces, we always have the "swapper" (idle) process having tid/pid 0.
  void SetPidZeroIsUpidZeroIdleProcess();

  // Returns a BoundInserter to add arguments to the arg set of a process.
  // Arguments are flushed into trace storage only after the trace was loaded in
  // its entirety.
  ArgsTracker::BoundInserter AddArgsToProcess(UniquePid upid);

  // Returns a BoundInserter to add arguments to the arg set of a thread.
  // Arguments are flushed into trace storage only after the trace was loaded in
  // its entirety.
  ArgsTracker::BoundInserter AddArgsToThread(UniqueTid utid);

  // Called when the trace was fully loaded.
  void NotifyEndOfFile();

  // Tracks the namespace-local pids for a process running in a pid namespace.
  void UpdateNamespacedProcess(int64_t pid, std::vector<int64_t> nspid);

  // Tracks the namespace-local thread ids for a thread running in a pid
  // namespace.
  // Returns false if the corresponding process was not found (likely due to
  // data loss).
  PERFETTO_WARN_UNUSED_RESULT bool
  UpdateNamespacedThread(int64_t pid, int64_t tid, std::vector<int64_t> nstid);

  // The UniqueTid of the swapper thread, is 0 for the default machine and is
  // > 0 for remote machines.
  UniqueTid swapper_utid() const { return swapper_utid_; }

 private:
  // Returns the utid of a thread having |tid| and |pid| as the parent process.
  // pid == std::nullopt matches all processes.
  // Returns std::nullopt if such a thread doesn't exist.
  std::optional<uint32_t> GetThreadOrNull(int64_t tid,
                                          std::optional<int64_t> pid);

  // Returns the utid of a thread whos parent matches the provided pid
  // or creates a new thread if not present. If a new thread is created,
  // |is_main_thread| determines if it is marked as the main thread.
  UniqueTid GetOrCreateThreadWithParentInternal(int64_t tid,
                                                UniquePid upid,
                                                bool is_main_thread,
                                                bool associate_main_threads);

  // Called whenever we discover that the passed thread belongs to the passed
  // process. The |pending_assocs_| vector is scanned to see if there are any
  // other threads associated to the passed thread. The associate_main_threads
  // boolean parameter is used to determine if a thread should be marked as the
  // main thread if the tid and pid match, when resolving parent process.
  void ResolvePendingAssociations(UniqueTid, UniquePid, bool);

  // Associates the passed pid as the parent process of the passed thread.
  // The is_main_thread arguments specifies whether the thread is the process'
  // main thread.
  void AssociateThreadToProcessInternal(UniqueTid, UniquePid, bool);

  // Starts a new process
  UniquePid StartNewProcessInternal(std::optional<int64_t> timestamp,
                                    std::optional<UniquePid> parent_upid,
                                    int64_t pid,
                                    StringId process_name,
                                    ThreadNamePriority priority,
                                    bool associate_main_thread);

  UniquePid GetOrCreateProcessInternal(int64_t pid, bool associate_main_thread);

  TraceProcessorContext* const context_;

  ArgsTracker args_tracker_;

  // Mapping for tid to the vector of possible UniqueTids.
  // TODO(lalitm): this is a one-many mapping because this code was written
  // before global sorting was a thing so multiple threads could be "active"
  // simultaneously. This is no longer the case so this should be removed
  // (though it seems like there are subtle things which break in Chrome if this
  // changes).
  base::FlatHashMap<int64_t /* tid */, std::vector<UniqueTid>> tids_;

  // Mapping of the most recently seen pid to the associated upid.
  base::FlatHashMap<int64_t /* pid (aka tgid) */, UniquePid> pids_;

  // Pending thread associations. The meaning of a pair<ThreadA, ThreadB> in
  // this vector is: we know that A and B belong to the same process, but we
  // don't know yet which process. A and A are idempotent, as in, pair<A,B> is
  // equivalent to pair<B,A>.
  std::vector<std::pair<UniqueTid, UniqueTid>> pending_assocs_;

  // Pending parent process associations. The meaning of pair<ThreadA, ProcessB>
  // in this vector is: we know that A created process B but we don't know the
  // process of A. That is, we don't know the parent *process* of B.
  std::vector<std::pair<UniqueTid, UniquePid>> pending_parent_assocs_;

  // A mapping from utid to the priority of a thread name source.
  std::vector<ThreadNamePriority> thread_name_priorities_;

  // A mapping from track UUIDs to trusted pids.
  std::unordered_map<uint64_t, int64_t> trusted_pids_;

  struct NamespacedThread {
    int64_t pid;                 // Root-level pid.
    int64_t tid;                 // Root-level tid.
    std::vector<int64_t> nstid;  // Namespace-local tids.
  };
  // Keeps track of pid-namespaced threads, keyed by root-level thread ids.
  std::unordered_map<int64_t /* tid */, NamespacedThread> namespaced_threads_;

  struct NamespacedProcess {
    int64_t pid;                          // Root-level pid.
    std::vector<int64_t> nspid;           // Namespace-local pids.
    std::unordered_set<int64_t> threads;  // Root-level thread IDs.
  };
  // Keeps track pid-namespaced processes, keyed by root-level pids.
  std::unordered_map<int64_t /* pid (aka tgid) */, NamespacedProcess>
      namespaced_processes_;

  UniquePid swapper_upid_ = 0;
  UniqueTid swapper_utid_ = 0;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_PROCESS_TRACKER_H_
