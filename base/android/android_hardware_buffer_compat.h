// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_ANDROID_HARDWARE_BUFFER_COMPAT_H_
#define BASE_ANDROID_ANDROID_HARDWARE_BUFFER_COMPAT_H_

#include "base/android/android_hardware_buffer_abi.h"
#include "base/base_export.h"
#include "base/lazy_instance.h"

namespace base {

// This class provides runtime support for working with AHardwareBuffer objects
// on Android O systems without requiring building for the Android O NDK level.
// Don't call GetInstance() unless IsSupportAvailable() returns true.
class BASE_EXPORT AndroidHardwareBufferCompat {
 public:
  static bool IsSupportAvailable();
  static AndroidHardwareBufferCompat GetInstance();

  void Allocate(const AHardwareBuffer_Desc* desc, AHardwareBuffer** outBuffer);
  void Acquire(AHardwareBuffer* buffer);
  void Describe(const AHardwareBuffer* buffer, AHardwareBuffer_Desc* outDesc);
  int Lock(AHardwareBuffer* buffer,
           uint64_t usage,
           int32_t fence,
           const ARect* rect,
           void** out_virtual_address);
  int RecvHandleFromUnixSocket(int socketFd, AHardwareBuffer** outBuffer);
  void Release(AHardwareBuffer* buffer);
  int SendHandleToUnixSocket(const AHardwareBuffer* buffer, int socketFd);
  int Unlock(AHardwareBuffer* buffer, int32_t* fence);

 private:
  friend struct base::LazyInstanceTraitsBase<AndroidHardwareBufferCompat>;
  AndroidHardwareBufferCompat();

  PFAHardwareBuffer_allocate allocate_;
  PFAHardwareBuffer_acquire acquire_;
  PFAHardwareBuffer_describe describe_;
  PFAHardwareBuffer_lock lock_;
  PFAHardwareBuffer_recvHandleFromUnixSocket recv_handle_;
  PFAHardwareBuffer_release release_;
  PFAHardwareBuffer_sendHandleToUnixSocket send_handle_;
  PFAHardwareBuffer_unlock unlock_;
};

}  // namespace base

#endif  // BASE_ANDROID_ANDROID_HARDWARE_BUFFER_COMPAT_H_
