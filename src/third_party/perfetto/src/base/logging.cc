/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "perfetto/base/logging.h"

#include <stdarg.h>
#include <stdio.h>

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <unistd.h>  // For isatty()
#endif

#include <atomic>
#include <memory>

#include "perfetto/base/build_config.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/crash_keys.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/base/log_ring_buffer.h"

#if PERFETTO_ENABLE_LOG_RING_BUFFER() && PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <android/set_abort_message.h>
#endif

namespace perfetto {
namespace base {

namespace {
const char kReset[] = "\x1b[0m";
const char kDefault[] = "\x1b[39m";
const char kDim[] = "\x1b[2m";
const char kRed[] = "\x1b[31m";
const char kBoldGreen[] = "\x1b[1m\x1b[32m";
const char kLightGray[] = "\x1b[90m";

std::atomic<LogMessageCallback> g_log_callback{};

#if PERFETTO_BUILDFLAG(PERFETTO_STDERR_CRASH_DUMP)
// __attribute__((constructor)) causes a static initializer that automagically
// early runs this function before the main().
void PERFETTO_EXPORT_COMPONENT __attribute__((constructor))
InitDebugCrashReporter() {
  // This function is defined in debug_crash_stack_trace.cc.
  // The dynamic initializer is in logging.cc because logging.cc is included
  // in virtually any target that depends on base. Having it in
  // debug_crash_stack_trace.cc would require figuring out -Wl,whole-archive
  // which is not worth it.
  EnableStacktraceOnCrashForDebug();
}
#endif

#if PERFETTO_ENABLE_LOG_RING_BUFFER()
LogRingBuffer g_log_ring_buffer{};

// This is global to avoid allocating memory or growing too much the stack
// in MaybeSerializeLastLogsForCrashReporting(), which is called from
// arbitrary code paths hitting PERFETTO_CHECK()/FATAL().
char g_crash_buf[kLogRingBufEntries * kLogRingBufMsgLen];
#endif

}  // namespace

void SetLogMessageCallback(LogMessageCallback callback) {
  g_log_callback.store(callback, std::memory_order_relaxed);
}

void LogMessage(LogLev level,
                const char* fname,
                int line,
                const char* fmt,
                ...) {
  char stack_buf[512];
  std::unique_ptr<char[]> large_buf;
  char* log_msg = &stack_buf[0];
  size_t log_msg_len = 0;

  // By default use a stack allocated buffer because most log messages are quite
  // short. In rare cases they can be larger (e.g. --help). In those cases we
  // pay the cost of allocating the buffer on the heap.
  for (size_t max_len = sizeof(stack_buf);;) {
    va_list args;
    va_start(args, fmt);
    int res = vsnprintf(log_msg, max_len, fmt, args);
    va_end(args);

    // If for any reason the print fails, overwrite the message but still print
    // it. The code below will attach the filename and line, which is still
    // useful.
    if (res < 0) {
      snprintf(log_msg, max_len, "%s", "[printf format error]");
      break;
    }

    // if res == max_len, vsnprintf saturated the input buffer. Retry with a
    // larger buffer in that case (within reasonable limits).
    if (res < static_cast<int>(max_len) || max_len >= 128 * 1024) {
      // In case of truncation vsnprintf returns the len that "would have been
      // written if the string was longer", not the actual chars written.
      log_msg_len = std::min(static_cast<size_t>(res), max_len - 1);
      break;
    }
    max_len *= 4;
    large_buf.reset(new char[max_len]);
    log_msg = &large_buf[0];
  }

  LogMessageCallback cb = g_log_callback.load(std::memory_order_relaxed);
  if (cb) {
    cb({level, line, fname, log_msg});
    return;
  }

  const char* color = kDefault;
  switch (level) {
    case kLogDebug:
      color = kDim;
      break;
    case kLogInfo:
      color = kDefault;
      break;
    case kLogImportant:
      color = kBoldGreen;
      break;
    case kLogError:
      color = kRed;
      break;
  }

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM) && \
    !PERFETTO_BUILDFLAG(PERFETTO_CHROMIUM_BUILD)
  static const bool use_colors = isatty(STDERR_FILENO);
#else
  static const bool use_colors = false;
#endif

  // Formats file.cc:line as a space-padded fixed width string. If the file name
  // |fname| is too long, truncate it on the left-hand side.
  StackString<10> line_str("%d", line);

  // 24 will be the width of the file.cc:line column in the log event.
  static constexpr size_t kMaxNameAndLine = 24;
  size_t fname_len = strlen(fname);
  size_t fname_max = kMaxNameAndLine - line_str.len() - 2;  // 2 = ':' + '\0'.
  size_t fname_offset = fname_len <= fname_max ? 0 : fname_len - fname_max;
  StackString<kMaxNameAndLine> file_and_line(
      "%*s:%s", static_cast<int>(fname_max), &fname[fname_offset],
      line_str.c_str());

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  // Logcat has already timestamping, don't re-emit it.
  __android_log_print(int{ANDROID_LOG_DEBUG} + level, "perfetto", "%s %s",
                      file_and_line.c_str(), log_msg);
#endif

  // When printing on stderr, print also the timestamp. We don't really care
  // about the actual time. We just need some reference clock that can be used
  // to correlated events across different processes (e.g. traced and
  // traced_probes). The wall time % 1000 is good enough.
  uint32_t t_ms = static_cast<uint32_t>(GetWallTimeMs().count());
  uint32_t t_sec = t_ms / 1000;
  t_ms -= t_sec * 1000;
  t_sec = t_sec % 1000;
  StackString<32> timestamp("[%03u.%03u] ", t_sec, t_ms);

  if (use_colors) {
    fprintf(stderr, "%s%s%s%s %s%s%s\n", kLightGray, timestamp.c_str(),
            file_and_line.c_str(), kReset, color, log_msg, kReset);
  } else {
    fprintf(stderr, "%s%s %s\n", timestamp.c_str(), file_and_line.c_str(),
            log_msg);
  }

#if PERFETTO_ENABLE_LOG_RING_BUFFER()
  // Append the message to the ring buffer for crash reporting postmortems.
  StringView timestamp_sv = timestamp.string_view();
  StringView file_and_line_sv = file_and_line.string_view();
  StringView log_msg_sv(log_msg, static_cast<size_t>(log_msg_len));
  g_log_ring_buffer.Append(timestamp_sv, file_and_line_sv, log_msg_sv);
#else
  ignore_result(log_msg_len);
#endif
}

#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {
  // Keep this function minimal. This is called from the watchdog thread, often
  // when the system is thrashing.

  // This is racy because two threads could hit a CHECK/FATAL at the same time.
  // But if that happens we have bigger problems, not worth designing around it.
  // The behaviour is still defined in the race case (the string attached to
  // the crash report will contain a mixture of log strings).
  size_t wr = 0;
  wr += SerializeCrashKeys(&g_crash_buf[wr], sizeof(g_crash_buf) - wr);
  wr += g_log_ring_buffer.Read(&g_crash_buf[wr], sizeof(g_crash_buf) - wr);

  // Read() null-terminates the string properly. This is just to avoid UB when
  // two threads race on each other (T1 writes a shorter string, T2
  // overwrites the \0 writing a longer string. T1 continues here before T2
  // finishes writing the longer string with the \0 -> boom.
  g_crash_buf[sizeof(g_crash_buf) - 1] = '\0';

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  // android_set_abort_message() will cause debuggerd to report the message
  // in the tombstone and in the crash log in logcat.
  // NOTE: android_set_abort_message() can be called only once. This should
  // be called only when we are sure we are about to crash.
  android_set_abort_message(g_crash_buf);
#else
  // Print out the message on stderr on Linux/Mac/Win.
  fputs("\n-----BEGIN PERFETTO PRE-CRASH LOG-----\n", stderr);
  fputs(g_crash_buf, stderr);
  fputs("\n-----END PERFETTO PRE-CRASH LOG-----\n", stderr);
#endif
}
#endif  // PERFETTO_ENABLE_LOG_RING_BUFFER

}  // namespace base
}  // namespace perfetto
