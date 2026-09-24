/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "perfetto/heap_profile.h"
#include "src/profiling/memory/heap_profile_internal.h"

#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cinttypes>
#include <memory>
#include <type_traits>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/unix_socket.h"
#include "perfetto/ext/base/utils.h"

#include "src/profiling/memory/client.h"
#include "src/profiling/memory/client_api_factory.h"
#include "src/profiling/memory/scoped_spinlock.h"
#include "src/profiling/memory/unhooked_allocator.h"
#include "src/profiling/memory/wire_protocol.h"

struct AHeapInfo {
  // Fields set by user.
  char heap_name[HEAPPROFD_HEAP_NAME_SZ];
  void (*enabled_callback)(void*, const AHeapProfileEnableCallbackInfo*);
  void (*disabled_callback)(void*, const AHeapProfileDisableCallbackInfo*);
  void* enabled_callback_data;
  void* disabled_callback_data;

  // Internal fields.
  perfetto::profiling::Sampler sampler;
  std::atomic<bool> ready;
  std::atomic<bool> enabled;
  std::atomic<uint64_t> adaptive_sampling_shmem_threshold;
  std::atomic<uint64_t> adaptive_sampling_max_sampling_interval_bytes;
};

struct AHeapProfileEnableCallbackInfo {
  uint64_t sampling_interval;
};

struct AHeapProfileDisableCallbackInfo {};

namespace {

using perfetto::profiling::ScopedSpinlock;
using perfetto::profiling::UnhookedAllocator;

#if defined(__GLIBC__)
const char* getprogname() {
  return program_invocation_short_name;
}
#elif !defined(__BIONIC__)
const char* getprogname() {
  return "";
}
#endif

// Holds the active profiling client. Is empty at the start, or after we've
// started shutting down a profiling session. Hook invocations take shared_ptr
// copies (ensuring that the client stays alive until no longer needed), and do
// nothing if this primary pointer is empty.
//
// This shared_ptr itself is protected by g_client_lock. Note that shared_ptr
// handles are not thread-safe by themselves:
// https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic
//
// To avoid on-destruction re-entrancy issues, this shared_ptr needs to be
// constructed with an allocator that uses the unhooked malloc & free functions.
// See UnhookedAllocator.
//
// We initialize this storage the first time GetClientLocked is called. We
// cannot use a static initializer because that leads to ordering problems
// of the ELF's constructors.

alignas(std::shared_ptr<perfetto::profiling::Client>) char g_client_arr[sizeof(
    std::shared_ptr<perfetto::profiling::Client>)];

bool g_client_init;

std::shared_ptr<perfetto::profiling::Client>* GetClientLocked() {
  if (!g_client_init) {
    new (g_client_arr) std::shared_ptr<perfetto::profiling::Client>;
    g_client_init = true;
  }
  return reinterpret_cast<std::shared_ptr<perfetto::profiling::Client>*>(
      &g_client_arr);
}

constexpr auto kMinHeapId = 1;
constexpr auto kMaxNumHeaps = 256;

AHeapInfo g_heaps[kMaxNumHeaps] = {};

AHeapInfo& GetHeap(uint32_t id) {
  return g_heaps[id];
}

// Protects g_client, and serves as an external lock for sampling decisions (see
// perfetto::profiling::Sampler).
//
// We rely on this atomic's destruction being a nop, as it is possible for the
// hooks to attempt to acquire the spinlock after its destructor should have run
// (technically a use-after-destruct scenario).
static_assert(
    std::is_trivially_destructible<perfetto::profiling::Spinlock>::value,
    "lock must be trivially destructible.");
perfetto::profiling::Spinlock g_client_lock{};

std::atomic<uint32_t> g_next_heap_id{kMinHeapId};

// This can get called while holding the spinlock (in normal operation), or
// without holding the spinlock (from OnSpinlockTimeout).
void DisableAllHeaps() {
  bool disabled[kMaxNumHeaps] = {};
  uint32_t max_heap = g_next_heap_id.load();
  // This has to be done in two passes, in case the disabled_callback for one
  // enabled heap uses another. In that case, the callbacks for the other heap
  // would time out trying to acquire the spinlock, which we hold here.
  for (uint32_t i = kMinHeapId; i < max_heap; ++i) {
    AHeapInfo& info = GetHeap(i);
    if (!info.ready.load(std::memory_order_acquire))
      continue;
    disabled[i] = info.enabled.exchange(false, std::memory_order_acq_rel);
  }
  for (uint32_t i = kMinHeapId; i < max_heap; ++i) {
    if (!disabled[i]) {
      continue;
    }
    AHeapInfo& info = GetHeap(i);
    if (info.disabled_callback) {
      AHeapProfileDisableCallbackInfo disable_info;
      info.disabled_callback(info.disabled_callback_data, &disable_info);
    }
  }
}

#pragma GCC diagnostic push
#if PERFETTO_DCHECK_IS_ON()
#pragma GCC diagnostic ignored "-Wmissing-noreturn"
#endif

void OnSpinlockTimeout() {
  // Give up on profiling the process but leave it running.
  // The process enters into a poisoned state and will reject all
  // subsequent profiling requests.  The current session is kept
  // running but no samples are reported to it.
  PERFETTO_DFATAL_OR_ELOG(
      "Timed out on the spinlock - something is horribly wrong. "
      "Leaking heapprofd client.");
  DisableAllHeaps();
  perfetto::profiling::PoisonSpinlock(&g_client_lock);
}
#pragma GCC diagnostic pop

// Note: g_client can be reset by AHeapProfile_initSession without calling this
// function.
void ShutdownLazy(const std::shared_ptr<perfetto::profiling::Client>& client) {
  ScopedSpinlock s(&g_client_lock, ScopedSpinlock::Mode::Try);
  if (PERFETTO_UNLIKELY(!s.locked())) {
    OnSpinlockTimeout();
    return;
  }

  // other invocation already initiated shutdown
  if (*GetClientLocked() != client)
    return;

  DisableAllHeaps();
  // Clear primary shared pointer, such that later hook invocations become nops.
  GetClientLocked()->reset();
}

uint64_t MaybeToggleHeap(uint32_t heap_id,
                         perfetto::profiling::Client* client) {
  AHeapInfo& heap = GetHeap(heap_id);
  if (!heap.ready.load(std::memory_order_acquire))
    return 0;
  auto interval =
      GetHeapSamplingInterval(client->client_config(), heap.heap_name);
  // The callbacks must be called while NOT LOCKED. Because they run
  // arbitrary code, it would be very easy to build a deadlock.
  if (interval) {
    AHeapProfileEnableCallbackInfo session_info{interval};
    if (!heap.enabled.load(std::memory_order_acquire) &&
        heap.enabled_callback) {
      heap.enabled_callback(heap.enabled_callback_data, &session_info);
    }
    heap.adaptive_sampling_shmem_threshold.store(
        client->client_config().adaptive_sampling_shmem_threshold,
        std::memory_order_relaxed);
    heap.adaptive_sampling_max_sampling_interval_bytes.store(
        client->client_config().adaptive_sampling_max_sampling_interval_bytes,
        std::memory_order_relaxed);
    heap.enabled.store(true, std::memory_order_release);
    client->RecordHeapInfo(heap_id, &heap.heap_name[0], interval);
  } else if (heap.enabled.load(std::memory_order_acquire)) {
    heap.enabled.store(false, std::memory_order_release);
    if (heap.disabled_callback) {
      AHeapProfileDisableCallbackInfo info;
      heap.disabled_callback(heap.disabled_callback_data, &info);
    }
  }
  return interval;
}

// We're a library loaded into a potentially-multithreaded process, which might
// not be explicitly aware of this possibility. Deadling with forks/clones is
// extremely complicated in such situations, but we attempt to handle certain
// cases.
//
// There are two classes of forking processes to consider:
//  * well-behaved processes that fork only when their threads (if any) are at a
//    safe point, and therefore not in the middle of our hooks/client.
//  * processes that fork with other threads in an arbitrary state. Though
//    technically buggy, such processes exist in practice.
//
// This atfork handler follows a crude lowest-common-denominator approach, where
// to handle the latter class of processes, we systematically leak any |Client|
// state (present only when actively profiling at the time of fork) in the
// postfork-child path.
//
// The alternative with acquiring all relevant locks in the prefork handler, and
// releasing the state postfork handlers, poses a separate class of edge cases,
// and is not deemed to be better as a result.
//
// Notes:
// * this atfork handler fires only for the |fork| libc entrypoint, *not*
//   |clone|. See client.cc's |IsPostFork| for some best-effort detection
//   mechanisms for clone/vfork.
// * it should be possible to start a new profiling session in this child
//   process, modulo the bionic's heapprofd-loading state machine being in the
//   right state.
// * we cannot avoid leaks in all cases anyway (e.g. during shutdown sequence,
//   when only individual straggler threads hold onto the Client).
void AtForkChild() {
  PERFETTO_LOG("heapprofd_client: handling atfork.");

  // A thread (that has now disappeared across the fork) could have been holding
  // the spinlock. We're now the only thread post-fork, so we can reset the
  // spinlock, though the state it protects (the |g_client| shared_ptr) might
  // not be in a consistent state.
  g_client_lock.locked.store(false);
  g_client_lock.poisoned.store(false);

  // We must not call the disabled callbacks here, because they might require
  // locks that are being held at the fork point.
  for (uint32_t i = kMinHeapId; i < g_next_heap_id.load(); ++i) {
    AHeapInfo& info = GetHeap(i);
    info.enabled.store(false);
  }
  // Leak the existing shared_ptr contents, including the profiling |Client| if
  // profiling was active at the time of the fork.
  // Note: this code assumes that the creation of the empty shared_ptr does not
  // allocate, which should be the case for all implementations as the
  // constructor has to be noexcept.
  new (g_client_arr) std::shared_ptr<perfetto::profiling::Client>();
}

}  // namespace

__attribute__((visibility("default"))) uint64_t
AHeapProfileEnableCallbackInfo_getSamplingInterval(
    const AHeapProfileEnableCallbackInfo* session_info) {
  return session_info->sampling_interval;
}

__attribute__((visibility("default"))) AHeapInfo* AHeapInfo_create(
    const char* heap_name) {
  size_t len = strlen(heap_name);
  if (len >= sizeof(AHeapInfo::heap_name)) {
    return nullptr;
  }

  uint32_t next_id = g_next_heap_id.fetch_add(1);
  if (next_id >= kMaxNumHeaps) {
    return nullptr;
  }

  if (next_id == kMinHeapId)
    perfetto::profiling::StartHeapprofdIfStatic();

  AHeapInfo& info = GetHeap(next_id);
  perfetto::base::StringCopy(info.heap_name, heap_name, sizeof(info.heap_name));
  return &info;
}

__attribute__((visibility("default"))) AHeapInfo* AHeapInfo_setEnabledCallback(
    AHeapInfo* info,
    void (*callback)(void*, const AHeapProfileEnableCallbackInfo*),
    void* data) {
  if (info == nullptr)
    return nullptr;
  if (info->ready.load(std::memory_order_relaxed)) {
    PERFETTO_ELOG(
        "AHeapInfo_setEnabledCallback called after heap was registered. "
        "This is always a bug.");
    return nullptr;
  }
  info->enabled_callback = callback;
  info->enabled_callback_data = data;
  return info;
}

__attribute__((visibility("default"))) AHeapInfo* AHeapInfo_setDisabledCallback(
    AHeapInfo* info,
    void (*callback)(void*, const AHeapProfileDisableCallbackInfo*),
    void* data) {
  if (info == nullptr)
    return nullptr;
  if (info->ready.load(std::memory_order_relaxed)) {
    PERFETTO_ELOG(
        "AHeapInfo_setDisabledCallback called after heap was registered. "
        "This is always a bug.");
    return nullptr;
  }
  info->disabled_callback = callback;
  info->disabled_callback_data = data;
  return info;
}

__attribute__((visibility("default"))) uint32_t
AHeapProfile_registerHeap(AHeapInfo* info) {
  if (info == nullptr)
    return 0;
  info->ready.store(true, std::memory_order_release);
  auto heap_id = static_cast<uint32_t>(info - &g_heaps[0]);
  std::shared_ptr<perfetto::profiling::Client> client;
  {
    ScopedSpinlock s(&g_client_lock, ScopedSpinlock::Mode::Try);
    if (PERFETTO_UNLIKELY(!s.locked())) {
      OnSpinlockTimeout();
      return 0;
    }

    client = *GetClientLocked();
  }

  // Enable the heap immediately if there's a matching ongoing session.
  if (client) {
    uint64_t interval = MaybeToggleHeap(heap_id, client.get());
    if (interval) {
      ScopedSpinlock s(&g_client_lock, ScopedSpinlock::Mode::Try);
      if (PERFETTO_UNLIKELY(!s.locked())) {
        OnSpinlockTimeout();
        return 0;
      }
      info->sampler.SetSamplingInterval(interval);
    }
  }
  return heap_id;
}

__attribute__((visibility("default"))) bool
AHeapProfile_reportAllocation(uint32_t heap_id, uint64_t id, uint64_t size) {
  AHeapInfo& heap = GetHeap(heap_id);
  if (!heap.enabled.load(std::memory_order_acquire)) {
    return false;
  }
  size_t sampled_alloc_sz = 0;
  std::shared_ptr<perfetto::profiling::Client> client;
  {
    ScopedSpinlock s(&g_client_lock, ScopedSpinlock::Mode::Try);
    if (PERFETTO_UNLIKELY(!s.locked())) {
      OnSpinlockTimeout();
      return false;
    }

    auto* g_client_ptr = GetClientLocked();
    if (!*g_client_ptr)  // no active client (most likely shutting down)
      return false;
    auto& client_ptr = *g_client_ptr;

    if (s.blocked_us()) {
      client_ptr->AddClientSpinlockBlockedUs(s.blocked_us());
    }

    sampled_alloc_sz = heap.sampler.SampleSize(static_cast<size_t>(size));
    if (sampled_alloc_sz == 0)  // not sampling
      return false;
    if (client_ptr->write_avail() <
        client_ptr->adaptive_sampling_shmem_threshold()) {
      bool should_increment = true;
      if (client_ptr->adaptive_sampling_max_sampling_interval_bytes() != 0) {
        should_increment =
            heap.sampler.sampling_interval() <
            client_ptr->adaptive_sampling_max_sampling_interval_bytes();
      }
      if (should_increment) {
        uint64_t new_interval = 2 * heap.sampler.sampling_interval();
        heap.sampler.SetSamplingInterval(2 * heap.sampler.sampling_interval());
        client_ptr->RecordHeapInfo(heap_id, "", new_interval);
      }
    }

    client = client_ptr;  // owning copy
  }  // unlock

  if (!client->RecordMalloc(heap_id, sampled_alloc_sz, size, id)) {
    ShutdownLazy(client);
    return false;
  }
  return true;
}

__attribute__((visibility("default"))) bool
AHeapProfile_reportSample(uint32_t heap_id, uint64_t id, uint64_t size) {
  const AHeapInfo& heap = GetHeap(heap_id);
  if (!heap.enabled.load(std::memory_order_acquire)) {
    return false;
  }
  std::shared_ptr<perfetto::profiling::Client> client;
  {
    ScopedSpinlock s(&g_client_lock, ScopedSpinlock::Mode::Try);
    if (PERFETTO_UNLIKELY(!s.locked())) {
      OnSpinlockTimeout();
      return false;
    }

    auto* g_client_ptr = GetClientLocked();
    if (!*g_client_ptr)  // no active client (most likely shutting down)
      return false;

    if (s.blocked_us()) {
      (*g_client_ptr)->AddClientSpinlockBlockedUs(s.blocked_us());
    }

    client = *g_client_ptr;  // owning copy
  }  // unlock

  if (!client->RecordMalloc(heap_id, size, size, id)) {
    ShutdownLazy(client);
    return false;
  }
  return true;
}

__attribute__((visibility("default"))) void AHeapProfile_reportFree(
    uint32_t heap_id,
    uint64_t id) {
  const AHeapInfo& heap = GetHeap(heap_id);
  if (!heap.enabled.load(std::memory_order_acquire)) {
    return;
  }
  std::shared_ptr<perfetto::profiling::Client> client;
  {
    ScopedSpinlock s(&g_client_lock, ScopedSpinlock::Mode::Try);
    if (PERFETTO_UNLIKELY(!s.locked())) {
      OnSpinlockTimeout();
      return;
    }

    client = *GetClientLocked();  // owning copy (or empty)
    if (!client)
      return;

    if (s.blocked_us()) {
      client->AddClientSpinlockBlockedUs(s.blocked_us());
    }
  }

  if (!client->RecordFree(heap_id, id))
    ShutdownLazy(client);
}

__attribute__((visibility("default"))) bool AHeapProfile_initSession(
    void* (*malloc_fn)(size_t),
    void (*free_fn)(void*)) {
  static bool first_init = true;
  // Install an atfork handler to deal with *some* cases of the host forking.
  // The handler will be unpatched automatically if we're dlclosed.
  if (first_init && pthread_atfork(/*prepare=*/nullptr, /*parent=*/nullptr,
                                   &AtForkChild) != 0) {
    PERFETTO_PLOG("%s: pthread_atfork failed, not installing hooks.",
                  getprogname());
    return false;
  }
  first_init = false;

  // TODO(fmayer): Check other destructions of client and make a decision
  // whether we want to ban heap objects in the client or not.
  std::shared_ptr<perfetto::profiling::Client> old_client;
  {
    ScopedSpinlock s(&g_client_lock, ScopedSpinlock::Mode::Try);
    if (PERFETTO_UNLIKELY(!s.locked())) {
      OnSpinlockTimeout();
      return false;
    }

    auto* g_client_ptr = GetClientLocked();
    if (*g_client_ptr && (*g_client_ptr)->IsConnected()) {
      PERFETTO_LOG("%s: Rejecting concurrent profiling initialization.",
                   getprogname());
      return true;  // success as we're in a valid state
    }
    old_client = *g_client_ptr;
    g_client_ptr->reset();
  }

  old_client.reset();

  // The dispatch table never changes, so let the custom allocator retain the
  // function pointers directly.
  UnhookedAllocator<perfetto::profiling::Client> unhooked_allocator(malloc_fn,
                                                                    free_fn);

  // These factory functions use heap objects, so we need to run them without
  // the spinlock held.
  std::shared_ptr<perfetto::profiling::Client> client =
      perfetto::profiling::ConstructClient(unhooked_allocator);

  if (!client) {
    PERFETTO_LOG("%s: heapprofd_client not initialized, not installing hooks.",
                 getprogname());
    return false;
  }

  uint32_t max_heap = g_next_heap_id.load();
  bool heaps_enabled[kMaxNumHeaps] = {};

  PERFETTO_LOG("%s: heapprofd_client initialized.", getprogname());
  {
    ScopedSpinlock s(&g_client_lock, ScopedSpinlock::Mode::Try);
    if (PERFETTO_UNLIKELY(!s.locked())) {
      OnSpinlockTimeout();
      return false;
    }

    // This needs to happen under the lock for mutual exclusion regarding the
    // random engine.
    for (uint32_t i = kMinHeapId; i < max_heap; ++i) {
      AHeapInfo& heap = GetHeap(i);
      if (!heap.ready.load(std::memory_order_acquire)) {
        continue;
      }
      const uint64_t interval =
          GetHeapSamplingInterval(client->client_config(), heap.heap_name);
      if (interval) {
        heaps_enabled[i] = true;
        heap.sampler.SetSamplingInterval(interval);
      }
    }

    // This cannot have been set in the meantime. There are never two concurrent
    // calls to this function, as Bionic uses atomics to guard against that.
    PERFETTO_DCHECK(*GetClientLocked() == nullptr);
    *GetClientLocked() = client;
  }

  // We want to run MaybeToggleHeap last to make sure we never enable a heap
  // but subsequently return `false` from this function, which indicates to the
  // caller that we did not enable anything.
  //
  // For startup profiles, `false` is used by Bionic to signal it can unload
  // the library again.
  for (uint32_t i = kMinHeapId; i < max_heap; ++i) {
    if (!heaps_enabled[i]) {
      continue;
    }
    auto interval = MaybeToggleHeap(i, client.get());
    PERFETTO_DCHECK(interval > 0);
  }

  return true;
}
