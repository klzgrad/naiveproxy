// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process.h"

#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "base/debug/activity_tracker.h"
#include "base/fuchsia/default_job.h"
#include "base/strings/stringprintf.h"

namespace base {

Process::Process(ProcessHandle handle)
    : process_(handle), is_current_process_(false) {
  CHECK_NE(handle, zx_process_self());
}

Process::~Process() {
  Close();
}

Process::Process(Process&& other)
    : process_(std::move(other.process_)),
      is_current_process_(other.is_current_process_) {
  other.is_current_process_ = false;
}

Process& Process::operator=(Process&& other) {
  process_ = std::move(other.process_);
  is_current_process_ = other.is_current_process_;
  other.is_current_process_ = false;
  return *this;
}

// static
Process Process::Current() {
  Process process;
  process.is_current_process_ = true;
  return process;
}

// static
Process Process::Open(ProcessId pid) {
  if (pid == GetCurrentProcId())
    return Current();

  // While a process with object id |pid| might exist, the job returned by
  // zx_job_default() might not contain it, so this call can fail.
  ScopedZxHandle handle;
  zx_status_t status = zx_object_get_child(
      GetDefaultJob(), pid, ZX_RIGHT_SAME_RIGHTS, handle.receive());
  if (status != ZX_OK) {
    DLOG(ERROR) << "zx_object_get_child failed: " << status;
    return Process();
  }
  return Process(handle.release());
}

// static
Process Process::OpenWithExtraPrivileges(ProcessId pid) {
  // No privileges to set.
  return Open(pid);
}

// static
Process Process::DeprecatedGetProcessFromHandle(ProcessHandle handle) {
  DCHECK_NE(handle, GetCurrentProcessHandle());
  ScopedZxHandle out;
  if (zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, out.receive()) !=
      ZX_OK) {
    DLOG(ERROR) << "zx_handle_duplicate failed: " << handle;
    return Process();
  }

  return Process(out.release());
}

// static
bool Process::CanBackgroundProcesses() {
  return false;
}

// static
void Process::TerminateCurrentProcessImmediately(int exit_code) {
  _exit(exit_code);
}

bool Process::IsValid() const {
  return process_.is_valid() || is_current();
}

ProcessHandle Process::Handle() const {
  return is_current_process_ ? zx_process_self() : process_.get();
}

Process Process::Duplicate() const {
  if (is_current())
    return Current();

  if (!IsValid())
    return Process();

  ScopedZxHandle out;
  if (zx_handle_duplicate(process_.get(), ZX_RIGHT_SAME_RIGHTS,
                          out.receive()) != ZX_OK) {
    DLOG(ERROR) << "zx_handle_duplicate failed: " << process_.get();
    return Process();
  }

  return Process(out.release());
}

ProcessId Process::Pid() const {
  DCHECK(IsValid());
  return GetProcId(Handle());
}

bool Process::is_current() const {
  return is_current_process_;
}

void Process::Close() {
  is_current_process_ = false;
  process_.reset();
}

bool Process::Terminate(int exit_code, bool wait) const {
  // exit_code isn't supportable. https://crbug.com/753490.
  zx_status_t status = zx_task_kill(Handle());
  // TODO(scottmg): Put these LOG/CHECK back to DLOG/DCHECK after
  // https://crbug.com/750756 is diagnosed.
  if (status == ZX_OK && wait) {
    zx_signals_t signals;
    status = zx_object_wait_one(Handle(), ZX_TASK_TERMINATED,
                                zx_deadline_after(ZX_SEC(60)), &signals);
    if (status != ZX_OK) {
      LOG(ERROR) << "Error waiting for process exit: " << status;
    } else {
      CHECK(signals & ZX_TASK_TERMINATED);
    }
  } else if (status != ZX_OK) {
    LOG(ERROR) << "Unable to terminate process: " << status;
  }

  return status >= 0;
}

bool Process::WaitForExit(int* exit_code) const {
  return WaitForExitWithTimeout(TimeDelta::Max(), exit_code);
}

bool Process::WaitForExitWithTimeout(TimeDelta timeout, int* exit_code) const {
  if (is_current_process_)
    return false;

  // Record the event that this thread is blocking upon (for hang diagnosis).
  base::debug::ScopedProcessWaitActivity process_activity(this);

  zx_time_t deadline = timeout == TimeDelta::Max()
                           ? ZX_TIME_INFINITE
                           : (TimeTicks::Now() + timeout).ToZxTime();
  // TODO(scottmg): https://crbug.com/755282
  const bool kOnBot = getenv("CHROME_HEADLESS") != nullptr;
  if (kOnBot) {
    LOG(ERROR) << base::StringPrintf(
        "going to wait for process %x (deadline=%zu, now=%zu)", process_.get(),
        deadline, TimeTicks::Now().ToZxTime());
  }
  zx_signals_t signals_observed = 0;
  zx_status_t status = zx_object_wait_one(process_.get(), ZX_TASK_TERMINATED,
                                          deadline, &signals_observed);

  // TODO(scottmg): Make these LOGs into DLOGs after https://crbug.com/750756 is
  // fixed.
  if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
    LOG(ERROR) << "zx_object_wait_one failed, status=" << status;
    return false;
  }
  if (status == ZX_ERR_TIMED_OUT) {
    zx_time_t now = TimeTicks::Now().ToZxTime();
    LOG(ERROR) << "zx_object_wait_one timed out, signals=" << signals_observed
               << ", deadline=" << deadline << ", now=" << now
               << ", delta=" << (now - deadline);
    return false;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(process_.get(), ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), nullptr, nullptr);
  if (status != ZX_OK) {
    LOG(ERROR) << "zx_object_get_info failed, status=" << status;
    if (exit_code)
      *exit_code = -1;
    return false;
  }

  if (exit_code)
    *exit_code = proc_info.return_code;

  return true;
}

void Process::Exited(int exit_code) const {}

bool Process::IsProcessBackgrounded() const {
  // See SetProcessBackgrounded().
  DCHECK(IsValid());
  return false;
}

bool Process::SetProcessBackgrounded(bool value) {
  // No process priorities on Fuchsia. TODO(fuchsia): See MG-783, and update
  // this later if priorities are implemented.
  return false;
}

int Process::GetPriority() const {
  DCHECK(IsValid());
  // No process priorities on Fuchsia.
  return 0;
}

}  // namespace base
