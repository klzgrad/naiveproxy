// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/scoped_handle.h"

#include <stddef.h>

#include <unordered_map>

#include "base/debug/alias.h"
#include "base/debug/stack_trace.h"
#include "base/hash.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/synchronization/lock_impl.h"
#include "base/threading/thread_local.h"
#include "base/win/base_features.h"
#include "base/win/current_module.h"

extern "C" {
__declspec(dllexport) void* GetHandleVerifier();
typedef void* (*GetHandleVerifierFn)();
}

namespace {

struct HandleHash {
  size_t operator()(const HANDLE& handle) const {
    char buffer[sizeof(handle)];
    memcpy(buffer, &handle, sizeof(handle));
    return base::Hash(buffer, sizeof(buffer));
  }
};

struct Info {
  const void* owner;
  const void* pc1;
  const void* pc2;
  base::debug::StackTrace stack;
  DWORD thread_id;
};
typedef std::unordered_map<HANDLE, Info, HandleHash> HandleMap;

// GetLock() protects the handle map and setting g_active_verifier within this
// module.
typedef base::internal::LockImpl NativeLock;
NativeLock* GetLock() {
  static auto* native_lock = new NativeLock();
  return native_lock;
}

// Simple automatic locking using a native critical section so it supports
// recursive locking.
class AutoNativeLock {
 public:
  explicit AutoNativeLock(NativeLock& lock) : lock_(lock) {
    lock_.Lock();
  }

  ~AutoNativeLock() {
    lock_.Unlock();
  }

 private:
  NativeLock& lock_;
  DISALLOW_COPY_AND_ASSIGN(AutoNativeLock);
};

// Implements the actual object that is verifying handles for this process.
// The active instance is shared across the module boundary but there is no
// way to delete this object from the wrong side of it (or any side, actually).
class ActiveVerifier {
 public:
  explicit ActiveVerifier(bool enabled) : enabled_(enabled), lock_(GetLock()) {}

  // Retrieves the current verifier.
  static ActiveVerifier* Get();

  // The methods required by HandleTraits. They are virtual because we need to
  // forward the call execution to another module, instead of letting the
  // compiler call the version that is linked in the current module.
  virtual bool CloseHandle(HANDLE handle);
  virtual void StartTracking(HANDLE handle, const void* owner,
                             const void* pc1, const void* pc2);
  virtual void StopTracking(HANDLE handle, const void* owner,
                            const void* pc1, const void* pc2);
  virtual void Disable();
  virtual void OnHandleBeingClosed(HANDLE handle);
  virtual HMODULE GetModule() const;

 private:
  ~ActiveVerifier();  // Not implemented.

  static void InstallVerifier();

  base::debug::StackTrace creation_stack_;
  bool enabled_;
  base::ThreadLocalBoolean closing_;
  NativeLock* lock_;
  HandleMap map_;
  DISALLOW_COPY_AND_ASSIGN(ActiveVerifier);
};
ActiveVerifier* g_active_verifier = NULL;

// static
ActiveVerifier* ActiveVerifier::Get() {
  if (!g_active_verifier)
    ActiveVerifier::InstallVerifier();

  return g_active_verifier;
}

bool CloseHandleWrapper(HANDLE handle) {
  if (!::CloseHandle(handle))
    CHECK(false);  // CloseHandle failed.
  return true;
}

// Assigns the g_active_verifier global within the GetLock() lock.
// If |existing_verifier| is non-null then |enabled| is ignored.
void ThreadSafeAssignOrCreateActiveVerifier(ActiveVerifier* existing_verifier,
                                            bool enabled) {
  AutoNativeLock lock(*GetLock());
  // Another thread in this module might be trying to assign the global
  // verifier, so check that within the lock here.
  if (g_active_verifier)
    return;
  g_active_verifier =
      existing_verifier ? existing_verifier : new ActiveVerifier(enabled);
}

// static
void ActiveVerifier::InstallVerifier() {
#if BUILDFLAG(SINGLE_MODULE_MODE_HANDLE_VERIFIER)
  // Component build has one Active Verifier per module.
  ThreadSafeAssignOrCreateActiveVerifier(nullptr, true);
#else
  // If you are reading this, wondering why your process seems deadlocked, take
  // a look at your DllMain code and remove things that should not be done
  // there, like doing whatever gave you that nice windows handle you are trying
  // to store in a ScopedHandle.
  HMODULE main_module = ::GetModuleHandle(NULL);
  GetHandleVerifierFn get_handle_verifier =
      reinterpret_cast<GetHandleVerifierFn>(::GetProcAddress(
          main_module, "GetHandleVerifier"));

  // This should only happen if running in a DLL is linked with base but the
  // hosting EXE is not. In this case, create an ActiveVerifier for the current
  // module but leave it disabled.
  if (!get_handle_verifier) {
    ThreadSafeAssignOrCreateActiveVerifier(nullptr, false);
    return;
  }

  // Check if in the main module.
  if (get_handle_verifier == GetHandleVerifier) {
    ThreadSafeAssignOrCreateActiveVerifier(nullptr, true);
    return;
  }

  ActiveVerifier* main_module_verifier =
      reinterpret_cast<ActiveVerifier*>(get_handle_verifier());

  // Main module should always on-demand create a verifier.
  DCHECK(main_module_verifier);

  ThreadSafeAssignOrCreateActiveVerifier(main_module_verifier, false);
#endif
}

bool ActiveVerifier::CloseHandle(HANDLE handle) {
  if (!enabled_)
    return CloseHandleWrapper(handle);

  closing_.Set(true);
  CloseHandleWrapper(handle);
  closing_.Set(false);

  return true;
}

void ActiveVerifier::StartTracking(HANDLE handle, const void* owner,
                                   const void* pc1, const void* pc2) {
  if (!enabled_)
    return;

  // Grab the thread id before the lock.
  DWORD thread_id = GetCurrentThreadId();

  AutoNativeLock lock(*lock_);

  Info handle_info = { owner, pc1, pc2, base::debug::StackTrace(), thread_id };
  std::pair<HANDLE, Info> item(handle, handle_info);
  std::pair<HandleMap::iterator, bool> result = map_.insert(item);
  if (!result.second) {
    Info other = result.first->second;
    base::debug::Alias(&other);
    base::debug::Alias(&creation_stack_);
    CHECK(false);  // Attempt to start tracking already tracked handle.
  }
}

void ActiveVerifier::StopTracking(HANDLE handle, const void* owner,
                                  const void* pc1, const void* pc2) {
  if (!enabled_)
    return;

  AutoNativeLock lock(*lock_);
  HandleMap::iterator i = map_.find(handle);
  if (i == map_.end()) {
    base::debug::Alias(&creation_stack_);
    CHECK(false);  // Attempting to close an untracked handle.
  }

  Info other = i->second;
  if (other.owner != owner) {
    base::debug::Alias(&other);
    base::debug::Alias(&creation_stack_);
    CHECK(false);  // Attempting to close a handle not owned by opener.
  }

  map_.erase(i);
}

void ActiveVerifier::Disable() {
  enabled_ = false;
}

void ActiveVerifier::OnHandleBeingClosed(HANDLE handle) {
  if (!enabled_)
    return;

  if (closing_.Get())
    return;

  AutoNativeLock lock(*lock_);
  HandleMap::iterator i = map_.find(handle);
  if (i == map_.end())
    return;

  Info other = i->second;
  base::debug::Alias(&other);
  base::debug::Alias(&creation_stack_);
  CHECK(false);  // CloseHandle called on tracked handle.
}

HMODULE ActiveVerifier::GetModule() const {
  return CURRENT_MODULE();
}

}  // namespace

void* GetHandleVerifier() {
  return ActiveVerifier::Get();
}

namespace base {
namespace win {

// Static.
bool HandleTraits::CloseHandle(HANDLE handle) {
  return ActiveVerifier::Get()->CloseHandle(handle);
}

// Static.
void VerifierTraits::StartTracking(HANDLE handle, const void* owner,
                                   const void* pc1, const void* pc2) {
  return ActiveVerifier::Get()->StartTracking(handle, owner, pc1, pc2);
}

// Static.
void VerifierTraits::StopTracking(HANDLE handle, const void* owner,
                                  const void* pc1, const void* pc2) {
  return ActiveVerifier::Get()->StopTracking(handle, owner, pc1, pc2);
}

void DisableHandleVerifier() {
  return ActiveVerifier::Get()->Disable();
}

void OnHandleBeingClosed(HANDLE handle) {
  return ActiveVerifier::Get()->OnHandleBeingClosed(handle);
}

HMODULE GetHandleVerifierModuleForTesting() {
  return g_active_verifier->GetModule();
}

}  // namespace win
}  // namespace base
