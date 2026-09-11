/*
 * Copyright (C) 2026 The Android Open Source Project
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

// Host shim for liblog's <android/log.h>. Provides the small surface used by
// JNIHelp.h and the perfetto SDK JNI sources, routed to stderr so it shows
// up under the host JUnit runner. Only on the include path when
// libperfetto_jni is built for host via tools/run_android_sdk_host_test.

#ifndef SRC_ANDROID_SDK_JNI_HOST_STUBS_ANDROID_LOG_H_
#define SRC_ANDROID_SDK_JNI_HOST_STUBS_ANDROID_LOG_H_

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum android_LogPriority {
  ANDROID_LOG_UNKNOWN = 0,
  ANDROID_LOG_DEFAULT,
  ANDROID_LOG_VERBOSE,
  ANDROID_LOG_DEBUG,
  ANDROID_LOG_INFO,
  ANDROID_LOG_WARN,
  ANDROID_LOG_ERROR,
  ANDROID_LOG_FATAL,
  ANDROID_LOG_SILENT,
} android_LogPriority;

__attribute__((format(printf, 3, 4))) static inline int
__android_log_print(int prio, const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "%d/%s: ", prio, tag ? tag : "");
  int rc = vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  if (prio == ANDROID_LOG_FATAL) {
    abort();
  }
  return rc;
}

static inline int __android_log_write(int prio,
                                      const char* tag,
                                      const char* msg) {
  fprintf(stderr, "%d/%s: %s\n", prio, tag ? tag : "", msg ? msg : "");
  if (prio == ANDROID_LOG_FATAL) {
    abort();
  }
  return 0;
}

__attribute__((noreturn, format(printf, 3, 4))) static inline void
__android_log_assert(const char* cond, const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "F/%s: assertion `%s' failed: ", tag ? tag : "",
          cond ? cond : "");
  if (fmt) {
    vfprintf(stderr, fmt, ap);
  }
  fputc('\n', stderr);
  va_end(ap);
  abort();
}

#ifdef __cplusplus
}
#endif

#endif  // SRC_ANDROID_SDK_JNI_HOST_STUBS_ANDROID_LOG_H_
