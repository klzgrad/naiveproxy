/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_RPC_SESSION_LIFECYCLE_H_
#define SRC_TRACE_PROCESSOR_RPC_SESSION_LIFECYCLE_H_

#include <cstdint>
#include <functional>

#include "perfetto/base/build_config.h"
#include "perfetto/base/proc_utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <windows.h>
#endif

namespace perfetto::base {
class TaskRunner;
}

namespace perfetto::trace_processor {

// When the server's idle clock applies. Maps to the --idle-start flag.
enum class IdleStart {
  // Owner-aware: behave as kOrphaned if launched with a live controlling parent
  // (foreground), else as kLastQuery (detached / already orphaned).
  kAuto,
  // Armed only once the owning parent has exited (the dev-server model).
  kOrphaned,
  // Always armed; reaps <idle-timeout> after the last request.
  kLastQuery,
};

// Tracks whether the process's owning parent (captured at construction) is
// still alive. This is the one OS-specific primitive the lifecycle logic needs:
// "is my owner alive?".
class ProcessOwnerMonitor {
 public:
  ProcessOwnerMonitor();
  ~ProcessOwnerMonitor();
  ProcessOwnerMonitor(const ProcessOwnerMonitor&) = delete;
  ProcessOwnerMonitor& operator=(const ProcessOwnerMonitor&) = delete;

  // True if the owning parent captured at construction is still alive.
  bool IsOwnerAlive();

  // True if there was a live, non-init owning parent at construction (i.e. we
  // were launched in the foreground by a real parent, rather than detached or
  // already reparented to init).
  bool had_owner_at_start() const { return had_owner_at_start_; }

 private:
  bool had_owner_at_start_ = false;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  HANDLE parent_handle_ = nullptr;
#else
  base::PlatformProcessId owner_pid_ = 0;
#endif
};

// Reaps an idle server. Ticks on the task runner; when the idle clock is armed
// (per IdleStart and owner liveness) and no request has arrived within
// idle_timeout_ms, it invokes on_reap (typically: unlink socket + quit).
class IdleReaper {
 public:
  // |idle_timeout_ms| == 0 disables reaping entirely (Start() is a no-op).
  IdleReaper(base::TaskRunner* task_runner,
             uint32_t idle_timeout_ms,
             IdleStart idle_start,
             std::function<void()> on_reap);

  // Begins periodic ticking. Call once after the server is ready.
  void Start();

  // Resets the idle clock. Call at the end of handling each request.
  void OnActivity();

  // True while a request is being processed; reaping is suppressed.
  void set_query_in_flight(bool v) { query_in_flight_ = v; }

 private:
  void Tick();

  base::TaskRunner* const task_runner_;
  const uint32_t idle_timeout_ms_;
  const uint32_t tick_interval_ms_;
  ProcessOwnerMonitor owner_monitor_;
  IdleStart effective_start_;
  std::function<void()> on_reap_;
  int64_t last_activity_ms_ = 0;
  bool query_in_flight_ = false;
  bool reaped_ = false;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_RPC_SESSION_LIFECYCLE_H_
