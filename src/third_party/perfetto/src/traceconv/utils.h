/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef SRC_TRACECONV_UTILS_H_
#define SRC_TRACECONV_UTILS_H_

#include <stdio.h>

#include <cstdarg>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "perfetto/ext/base/paged_memory.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
#include <zlib.h>
#endif

namespace perfetto {

namespace trace_processor {
class TraceProcessor;
}

namespace protos {
class TracePacket;
}

namespace trace_to_text {

// When running in Web Assembly, fflush() is a no-op and the stdio buffering
// sends progress updates to JS only when a write ends with \n.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
constexpr char kProgressChar = '\n';
#else
constexpr char kProgressChar = '\r';
#endif

// True while an in-place progress line has been printed but not yet
// terminated with a newline. TU-local: all progress printing and the
// matching EndProgressLine() live in the same translation unit.
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
namespace {
inline bool& IsProgressLineActive() {
  static bool active = false;
  return active;
}
}  // namespace
#endif

// Prints an in-place progress update to stderr: the message followed by
// kProgressChar (a '\r' so the next update overwrites it). Remembers that a
// progress line is active so the matching EndProgressLine() terminates it
// with a newline. No-op on WASM where updates already end with '\n'.
inline void ProgressLine(const char* fmt, ...) PERFETTO_PRINTF_FORMAT(1, 2);
inline void ProgressLine(const char* fmt, ...) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
  ::perfetto::base::ignore_result(fmt);
#else
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputc(kProgressChar, stderr);
  fflush(stderr);
  IsProgressLineActive() = true;
#endif
}

// Terminates the current in-place progress line so that subsequent output
// (errors, logs, results) starts on a fresh line instead of overwriting or
// merging with the progress text. No-op when no progress line is active, so
// callers can invoke it unconditionally without producing stray blank lines.
// On WASM, progress updates already end with a newline (see kProgressChar),
// so this is a no-op there too.
inline void EndProgressLine() {
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
  if (IsProgressLineActive()) {
    fprintf(stderr, "\n");
    fflush(stderr);
    IsProgressLineActive() = false;
  }
#endif
}

bool ReadTraceUnfinalized(trace_processor::TraceProcessor* tp,
                          std::istream* input);
void IngestTraceOrDie(trace_processor::TraceProcessor* tp,
                      const std::string& trace_proto);

class TraceWriter {
 public:
  TraceWriter(std::ostream* output);
  virtual ~TraceWriter();

  void Write(const std::string& s);
  virtual void Write(const char* data, size_t sz);

 private:
  std::ostream* output_;
};

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
class DeflateTraceWriter : public TraceWriter {
 public:
  DeflateTraceWriter(std::ostream* output);
  ~DeflateTraceWriter() override;

  void Write(const char* data, size_t sz) override;

 private:
  void Flush();
  void CheckEq(int actual_code, int expected_code);

  z_stream stream_{};
  base::PagedMemory buf_;
  uint8_t* const start_;
  uint8_t* const end_;
};

#else

// Fallback implementation. Will print an error and write uncompressed.
class DeflateTraceWriter : public TraceWriter {
 public:
  DeflateTraceWriter(std::ostream* output);
  ~DeflateTraceWriter() override;
};

#endif  // PERFETTO_BUILDFLAG(PERFETTO_ZLIB)

}  // namespace trace_to_text
}  // namespace perfetto

#endif  // SRC_TRACECONV_UTILS_H_
