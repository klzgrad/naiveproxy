// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/stack_sampler.h"

#include <pthread.h>

#include "base/threading/platform_thread.h"
#include "build/build_config.h"

namespace base {

std::unique_ptr<StackSampler> StackSampler::Create(
    PlatformThreadId thread_id,
    ModuleCache* module_cache,
    StackSamplerTestDelegate* test_delegate) {
  return nullptr;
}

size_t StackSampler::GetStackBufferSize() {
  size_t stack_size = PlatformThread::GetDefaultThreadStackSize();

  pthread_attr_t attr;
  if (stack_size == 0 && pthread_attr_init(&attr) == 0) {
    if (pthread_attr_getstacksize(&attr, &stack_size) != 0)
      stack_size = 0;
    pthread_attr_destroy(&attr);
  }

// If we can't get stack limit from pthreads then use default value.
#if defined(OS_ANDROID)
  // 1MB is default thread limit set by Android at art/runtime/thread_pool.h.
  constexpr size_t kDefaultStackLimit = 1 << 20;
#else
  // Maximum limits under NPTL implementation.
  constexpr size_t kDefaultStackLimit = 4 * (1 << 20);
#endif
  return stack_size > 0 ? stack_size : kDefaultStackLimit;
}

}  // namespace base
