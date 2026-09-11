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

#include "src/trace_processor/rpc/session_lifecycle.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "perfetto/base/build_config.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <unistd.h>
#else
#include <tlhelp32.h>
#endif

namespace perfetto::trace_processor {
namespace {

int64_t NowMs() {
  return base::GetWallTimeMs().count();
}

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
DWORD GetParentProcessId() {
  DWORD pid = GetCurrentProcessId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE)
    return 0;
  PROCESSENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  DWORD parent = 0;
  if (Process32First(snap, &entry)) {
    do {
      if (entry.th32ProcessID == pid) {
        parent = entry.th32ParentProcessID;
        break;
      }
    } while (Process32Next(snap, &entry));
  }
  CloseHandle(snap);
  return parent;
}
#endif

}  // namespace

ProcessOwnerMonitor::ProcessOwnerMonitor() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  DWORD parent_pid = GetParentProcessId();
  if (parent_pid != 0) {
    parent_handle_ = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
    had_owner_at_start_ = parent_handle_ != nullptr;
  }
#else
  owner_pid_ = getppid();
  // A parent pid of 1 means we are already a child of init, i.e. there is no
  // controlling owner to reap us.
  had_owner_at_start_ = owner_pid_ > 1;
#endif
}

ProcessOwnerMonitor::~ProcessOwnerMonitor() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  if (parent_handle_)
    CloseHandle(parent_handle_);
#endif
}

bool ProcessOwnerMonitor::IsOwnerAlive() {
  if (!had_owner_at_start_)
    return false;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  return WaitForSingleObject(parent_handle_, 0) == WAIT_TIMEOUT;
#else
  // If the parent exited we are reparented (to init, pid 1, or a subreaper), so
  // getppid() no longer matches the owner captured at construction.
  return static_cast<base::PlatformProcessId>(getppid()) == owner_pid_;
#endif
}

IdleReaper::IdleReaper(base::TaskRunner* task_runner,
                       uint32_t idle_timeout_ms,
                       IdleStart idle_start,
                       std::function<void()> on_reap)
    : task_runner_(task_runner),
      idle_timeout_ms_(idle_timeout_ms),
      tick_interval_ms_(std::clamp(idle_timeout_ms, 50u, 5000u)),
      on_reap_(std::move(on_reap)) {
  switch (idle_start) {
    case IdleStart::kLastQuery:
      effective_start_ = IdleStart::kLastQuery;
      break;
    case IdleStart::kOrphaned:
      effective_start_ = IdleStart::kOrphaned;
      break;
    case IdleStart::kAuto:
      // Owner-aware: a foreground server (live owner) is reaped by its owner,
      // so only arm once orphaned. A detached server has no owner, so arm now.
      effective_start_ = owner_monitor_.had_owner_at_start()
                             ? IdleStart::kOrphaned
                             : IdleStart::kLastQuery;
      break;
  }
}

void IdleReaper::Start() {
  if (idle_timeout_ms_ == 0)
    return;
  last_activity_ms_ = NowMs();
  task_runner_->PostDelayedTask([this] { Tick(); }, tick_interval_ms_);
}

void IdleReaper::OnActivity() {
  last_activity_ms_ = NowMs();
}

void IdleReaper::Tick() {
  if (reaped_)
    return;
  // The idle clock is armed unless we're waiting for the owner to exit first.
  bool armed = effective_start_ == IdleStart::kLastQuery ||
               !owner_monitor_.IsOwnerAlive();
  if (armed && !query_in_flight_ &&
      NowMs() - last_activity_ms_ >= idle_timeout_ms_) {
    reaped_ = true;
    on_reap_();
    return;
  }
  task_runner_->PostDelayedTask([this] { Tick(); }, tick_interval_ms_);
}

}  // namespace perfetto::trace_processor
