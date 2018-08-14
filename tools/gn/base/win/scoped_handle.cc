// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/scoped_handle.h"
#include "base/win/windows_types.h"

#include <windows.h>

namespace base {
namespace win {

// Static.
bool HandleTraits::CloseHandle(HANDLE handle) {
  return ::CloseHandle(handle);
}

// Static.
void VerifierTraits::StartTracking(HANDLE handle,
                                   const void* owner,
                                   const void* pc1,
                                   const void* pc2) {}

// Static.
void VerifierTraits::StopTracking(HANDLE handle,
                                  const void* owner,
                                  const void* pc1,
                                  const void* pc2) {}

void DisableHandleVerifier() {}

void OnHandleBeingClosed(HANDLE handle) {}

}  // namespace win
}  // namespace base
