// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/platform_thread.h"

#include <pthread.h>
#include <sched.h>
#include <zircon/syscalls.h>

#include "base/threading/platform_thread_internal_posix.h"
#include "base/threading/thread_id_name_manager.h"

namespace base {

void InitThreading() {}

void TerminateOnThread() {}

size_t GetDefaultThreadStackSize(const pthread_attr_t& attributes) {
  return 0;
}

// static
void PlatformThread::SetName(const std::string& name) {
  zx_status_t status = zx_object_set_property(CurrentId(), ZX_PROP_NAME,
                                              name.data(), name.size());
  DCHECK_EQ(status, ZX_OK);

  ThreadIdNameManager::GetInstance()->SetName(name);
}

// static
bool PlatformThread::CanIncreaseCurrentThreadPriority() {
  return false;
}

// static
void PlatformThread::SetCurrentThreadPriority(ThreadPriority priority) {
  if (priority != ThreadPriority::NORMAL) {
    NOTIMPLEMENTED() << "setting ThreadPriority " << static_cast<int>(priority);
  }
}

// static
ThreadPriority PlatformThread::GetCurrentThreadPriority() {
  return ThreadPriority::NORMAL;
}

}  // namespace base
