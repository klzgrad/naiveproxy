// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/kill.h"

#include <zircon/syscalls.h>

#include "base/process/process_iterator.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/platform_thread.h"

namespace base {

bool KillProcessGroup(ProcessHandle process_group_id) {
  // |process_group_id| is really a job on Fuchsia.
  zx_status_t status = zx_task_kill(process_group_id);
  DLOG_IF(ERROR, status != ZX_OK)
      << "unable to terminate job " << process_group_id;
  return status == ZX_OK;
}

TerminationStatus GetTerminationStatus(ProcessHandle handle, int* exit_code) {
  DCHECK(exit_code);

  zx_info_process_t process_info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_PROCESS, &process_info,
                         sizeof(process_info), nullptr, nullptr);
  if (status != ZX_OK) {
    DLOG(ERROR) << "unable to get termination status for " << handle;
    *exit_code = 0;
    return TERMINATION_STATUS_NORMAL_TERMINATION;
  }
  if (!process_info.started) {
    *exit_code = 0;
    return TERMINATION_STATUS_LAUNCH_FAILED;
  }
  if (!process_info.exited) {
    *exit_code = 0;
    return TERMINATION_STATUS_STILL_RUNNING;
  }

  // TODO(fuchsia): Is there more information about types of crashes, OOM, etc.
  // available? https://crbug.com/706592.

  *exit_code = process_info.return_code;
  return process_info.return_code == 0
             ? TERMINATION_STATUS_NORMAL_TERMINATION
             : TERMINATION_STATUS_ABNORMAL_TERMINATION;
}

void EnsureProcessTerminated(Process process) {
  DCHECK(!process.is_current());

  // Wait for up to two seconds for the process to terminate, and then kill it
  // forcefully if it hasn't already exited.
  zx_signals_t signals;
  if (zx_object_wait_one(process.Handle(), ZX_TASK_TERMINATED,
                         zx_deadline_after(ZX_SEC(2)), &signals) == ZX_OK) {
    DCHECK(signals & ZX_TASK_TERMINATED);
    // If already signaled, then the process is terminated.
    return;
  }

  process.Terminate(/*exit_code=*/1, /*wait=*/false);
}

}  // namespace base
