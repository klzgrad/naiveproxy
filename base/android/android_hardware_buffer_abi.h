// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_ANDROID_HARDWARE_BUFFER_ABI_H_
#define BASE_ANDROID_ANDROID_HARDWARE_BUFFER_ABI_H_

// Minimal binary interface definitions for AHardwareBuffer based on
// include/android/hardware_buffer.h from the Android NDK for platform level
// 26+. This is only intended for use from the AndroidHardwareBufferCompat
// wrapper for building without NDK platform level support, it is not a
// general-use header and is not complete.
//
// TODO(crbug.com/771171): Delete this file when third_party/android_ndk/
// is updated to a version that contains the android/hardware_buffer.h file.
//
// Please refer to the API documentation for details:
// https://developer.android.com/ndk/reference/hardware__buffer_8h.html

#include <stdint.h>

// Use "C" linkage to match the original header file. This isn't strictly
// required since the file is not declaring global functions, but the types
// should remain in the global namespace for compatibility, and it's a reminder
// that forward declarations elsewhere should use "extern "C" to avoid
// namespace issues.
extern "C" {

typedef struct AHardwareBuffer AHardwareBuffer;
typedef struct ARect ARect;

enum {
  AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM = 1,
  AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM = 2,
  AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM = 3,
  AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM = 4,
  AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT = 0x16,
  AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM = 0x2b,
  AHARDWAREBUFFER_FORMAT_BLOB = 0x21,
};

enum {
  AHARDWAREBUFFER_USAGE_CPU_READ_NEVER = 0UL,
  AHARDWAREBUFFER_USAGE_CPU_READ_RARELY = 2UL,
  AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN = 3UL,
  AHARDWAREBUFFER_USAGE_CPU_READ_MASK = 0xFUL,
  AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER = 0UL << 4,
  AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY = 2UL << 4,
  AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN = 3UL << 4,
  AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK = 0xFUL << 4,
  AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE = 1UL << 8,
  AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT = 1UL << 9,
  AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT = 1UL << 14,
  AHARDWAREBUFFER_USAGE_VIDEO_ENCODE = 1UL << 16,
  AHARDWAREBUFFER_USAGE_SENSOR_DIRECT_DATA = 1UL << 23,
  AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER = 1UL << 24,
};

typedef struct AHardwareBuffer_Desc {
  uint32_t width;
  uint32_t height;
  uint32_t layers;
  uint32_t format;
  uint64_t usage;
  uint32_t stride;
  uint32_t rfu0;
  uint64_t rfu1;
} AHardwareBuffer_Desc;

using PFAHardwareBuffer_allocate = void (*)(const AHardwareBuffer_Desc* desc,
                                            AHardwareBuffer** outBuffer);
using PFAHardwareBuffer_acquire = void (*)(AHardwareBuffer* buffer);
using PFAHardwareBuffer_describe = void (*)(const AHardwareBuffer* buffer,
                                            AHardwareBuffer_Desc* outDesc);
using PFAHardwareBuffer_lock = int (*)(AHardwareBuffer* buffer,
                                       uint64_t usage,
                                       int32_t fence,
                                       const ARect* rect,
                                       void** outVirtualAddress);
using PFAHardwareBuffer_recvHandleFromUnixSocket =
    int (*)(int socketFd, AHardwareBuffer** outBuffer);
using PFAHardwareBuffer_release = void (*)(AHardwareBuffer* buffer);
using PFAHardwareBuffer_sendHandleToUnixSocket =
    int (*)(const AHardwareBuffer* buffer, int socketFd);
using PFAHardwareBuffer_unlock = int (*)(AHardwareBuffer* buffer,
                                         int32_t* fence);

}  // extern "C"

#endif  // BASE_ANDROID_ANDROID_HARDWARE_BUFFER_ABI_H_
