// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_MEMORY_DUMP_PROVIDER_H_
#define BASE_TRACE_EVENT_MEMORY_DUMP_PROVIDER_H_

#include "base/base_export.h"
#include "base/macros.h"
#include "base/process/process_handle.h"
#include "base/trace_event/memory_dump_request_args.h"

namespace base {
namespace trace_event {

class ProcessMemoryDump;

// The contract interface that memory dump providers must implement.
class BASE_EXPORT MemoryDumpProvider {
 public:
  // Optional arguments for MemoryDumpManager::RegisterDumpProvider().
  struct Options {
    Options()
        : dumps_on_single_thread_task_runner(false),
          is_fast_polling_supported(false),
          supports_heap_profiling(false) {}

    // |dumps_on_single_thread_task_runner| is true if the dump provider runs on
    // a SingleThreadTaskRunner, which is usually the case. It is faster to run
    // all providers that run on the same thread together without thread hops.
    bool dumps_on_single_thread_task_runner;

    // Set to true if the dump provider implementation supports high frequency
    // polling. Only providers running without task runner affinity are
    // supported.
    bool is_fast_polling_supported;

    // Set to true when the dump provider supports heap profiling. MDM sends
    // OnHeapProfiling() notifications only if this is set to true.
    bool supports_heap_profiling;
  };

  virtual ~MemoryDumpProvider() = default;

  // Called by the MemoryDumpManager when generating memory dumps.
  // The |args| specify if the embedder should generate light/heavy dumps on
  // dump requests. The embedder should return true if the |pmd| was
  // successfully populated, false if something went wrong and the dump should
  // be considered invalid.
  // (Note, the MemoryDumpManager has a fail-safe logic which will disable the
  // MemoryDumpProvider for the entire trace session if it fails consistently).
  virtual bool OnMemoryDump(const MemoryDumpArgs& args,
                            ProcessMemoryDump* pmd) = 0;

  // Called by the MemoryDumpManager when an allocator should start or stop
  // collecting extensive allocation data, if supported. Called only when
  // |supports_heap_profiling| is set to true.
  virtual void OnHeapProfilingEnabled(bool enabled) {}

  // Quickly record the total memory usage in |memory_total|. This method will
  // be called only when the dump provider registration has
  // |is_fast_polling_supported| set to true. This method is used for polling at
  // high frequency for detecting peaks. See comment on
  // |is_fast_polling_supported| option if you need to override this method.
  virtual void PollFastMemoryTotal(uint64_t* memory_total) {}

  // Indicates that fast memory polling is not going to be used in the near
  // future and the MDP can tear down any resource kept around for fast memory
  // polling.
  virtual void SuspendFastMemoryPolling() {}

 protected:
  MemoryDumpProvider() = default;

  DISALLOW_COPY_AND_ASSIGN(MemoryDumpProvider);
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_MEMORY_DUMP_PROVIDER_H_
