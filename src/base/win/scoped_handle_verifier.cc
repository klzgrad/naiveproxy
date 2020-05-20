// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/scoped_handle_verifier.h"

#include <windows.h>

#include <stddef.h>

#include <unordered_map>

#include "base/debug/alias.h"
#include "base/debug/stack_trace.h"
#include "base/synchronization/lock_impl.h"
#include "base/win/base_win_buildflags.h"
#include "base/win/current_module.h"

extern "C" {
__declspec(dllexport) void* GetHandleVerifier();

void* GetHandleVerifier() {
  return base::win::internal::ScopedHandleVerifier::Get();
}
}  // extern C

namespace {

base::win::internal::ScopedHandleVerifier* g_active_verifier = nullptr;
using GetHandleVerifierFn = void* (*)();
using HandleMap =
    std::unordered_map<HANDLE,
                       base::win::internal::ScopedHandleVerifierInfo,
                       base::win::internal::HandleHash>;
using NativeLock = base::internal::LockImpl;

NativeLock* GetLock() {
  static auto* native_lock = new NativeLock();
  return native_lock;
}

// Simple automatic locking using a native critical section so it supports
// recursive locking.
class AutoNativeLock {
 public:
  explicit AutoNativeLock(NativeLock& lock) : lock_(lock) { lock_.Lock(); }

  ~AutoNativeLock() { lock_.Unlock(); }

 private:
  NativeLock& lock_;
  DISALLOW_COPY_AND_ASSIGN(AutoNativeLock);
};

}  // namespace

namespace base {
namespace win {
namespace internal {

ScopedHandleVerifier::ScopedHandleVerifier(bool enabled)
    : enabled_(enabled), lock_(GetLock()) {}

// static
ScopedHandleVerifier* ScopedHandleVerifier::Get() {
  if (!g_active_verifier)
    ScopedHandleVerifier::InstallVerifier();

  return g_active_verifier;
}

bool CloseHandleWrapper(HANDLE handle) {
  if (!::CloseHandle(handle))
    CHECK(false);  // CloseHandle failed.
  return true;
}

// Assigns the g_active_verifier global within the GetLock() lock.
// If |existing_verifier| is non-null then |enabled| is ignored.
void ThreadSafeAssignOrCreateScopedHandleVerifier(
    ScopedHandleVerifier* existing_verifier,
    bool enabled) {
  AutoNativeLock lock(*GetLock());
  // Another thread in this module might be trying to assign the global
  // verifier, so check that within the lock here.
  if (g_active_verifier)
    return;
  g_active_verifier =
      existing_verifier ? existing_verifier : new ScopedHandleVerifier(enabled);
}

// static
void ScopedHandleVerifier::InstallVerifier() {
#if BUILDFLAG(SINGLE_MODULE_MODE_HANDLE_VERIFIER)
  // Component build has one Active Verifier per module.
  ThreadSafeAssignOrCreateScopedHandleVerifier(nullptr, true);
#else
  // If you are reading this, wondering why your process seems deadlocked, take
  // a look at your DllMain code and remove things that should not be done
  // there, like doing whatever gave you that nice windows handle you are trying
  // to store in a ScopedHandle.
  HMODULE main_module = ::GetModuleHandle(NULL);
  GetHandleVerifierFn get_handle_verifier =
      reinterpret_cast<GetHandleVerifierFn>(
          ::GetProcAddress(main_module, "GetHandleVerifier"));

  // This should only happen if running in a DLL is linked with base but the
  // hosting EXE is not. In this case, create an ScopedHandleVerifier for the
  // current
  // module but leave it disabled.
  if (!get_handle_verifier) {
    ThreadSafeAssignOrCreateScopedHandleVerifier(nullptr, false);
    return;
  }

  // Check if in the main module.
  if (get_handle_verifier == GetHandleVerifier) {
    ThreadSafeAssignOrCreateScopedHandleVerifier(nullptr, true);
    return;
  }

  ScopedHandleVerifier* main_module_verifier =
      reinterpret_cast<ScopedHandleVerifier*>(get_handle_verifier());

  // Main module should always on-demand create a verifier.
  DCHECK(main_module_verifier);

  ThreadSafeAssignOrCreateScopedHandleVerifier(main_module_verifier, false);
#endif
}

bool ScopedHandleVerifier::CloseHandle(HANDLE handle) {
  if (!enabled_)
    return CloseHandleWrapper(handle);

  closing_.Set(true);
  CloseHandleWrapper(handle);
  closing_.Set(false);

  return true;
}

// static
NativeLock* ScopedHandleVerifier::GetLock() {
  return ::GetLock();
}

void ScopedHandleVerifier::StartTracking(HANDLE handle,
                                         const void* owner,
                                         const void* pc1,
                                         const void* pc2) {
  if (!enabled_)
    return;

  // Grab the thread id before the lock.
  DWORD thread_id = GetCurrentThreadId();

  AutoNativeLock lock(*lock_);

  ScopedHandleVerifierInfo handle_info = {owner, pc1, pc2,
                                          base::debug::StackTrace(), thread_id};
  std::pair<HANDLE, ScopedHandleVerifierInfo> item(handle, handle_info);
  std::pair<HandleMap::iterator, bool> result = map_.insert(item);
  if (!result.second) {
    ScopedHandleVerifierInfo other = result.first->second;
    base::debug::Alias(&other);
    auto creation_stack = creation_stack_;
    base::debug::Alias(&creation_stack);
    CHECK(false);  // Attempt to start tracking already tracked handle.
  }
}

void ScopedHandleVerifier::StopTracking(HANDLE handle,
                                        const void* owner,
                                        const void* pc1,
                                        const void* pc2) {
  if (!enabled_)
    return;

  AutoNativeLock lock(*lock_);
  HandleMap::iterator i = map_.find(handle);
  if (i == map_.end()) {
    auto creation_stack = creation_stack_;
    base::debug::Alias(&creation_stack);
    CHECK(false);  // Attempting to close an untracked handle.
  }

  ScopedHandleVerifierInfo other = i->second;
  if (other.owner != owner) {
    base::debug::Alias(&other);
    auto creation_stack = creation_stack_;
    base::debug::Alias(&creation_stack);
    CHECK(false);  // Attempting to close a handle not owned by opener.
  }

  map_.erase(i);
}

void ScopedHandleVerifier::Disable() {
  enabled_ = false;
}

void ScopedHandleVerifier::OnHandleBeingClosed(HANDLE handle) {
  if (!enabled_)
    return;

  if (closing_.Get())
    return;

  AutoNativeLock lock(*lock_);
  HandleMap::iterator i = map_.find(handle);
  if (i == map_.end())
    return;

  ScopedHandleVerifierInfo other = i->second;
  base::debug::Alias(&other);
  auto creation_stack = creation_stack_;
  base::debug::Alias(&creation_stack);
  CHECK(false);  // CloseHandle called on tracked handle.
}

HMODULE ScopedHandleVerifier::GetModule() const {
  return CURRENT_MODULE();
}

HMODULE GetHandleVerifierModuleForTesting() {
  return g_active_verifier->GetModule();
}

}  // namespace internal
}  // namespace win
}  // namespace base
