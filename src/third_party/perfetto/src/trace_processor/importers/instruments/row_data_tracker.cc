/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/instruments/row_data_tracker.h"

#include "perfetto/base/status.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_TP_INSTRUMENTS)
#error \
    "This file should not be built when enable_perfetto_trace_processor_mac_instruments=false"
#endif

namespace perfetto::trace_processor::instruments_importer {

RowDataTracker::RowDataTracker() {}
RowDataTracker::~RowDataTracker() = default;

IdPtr<Thread> RowDataTracker::NewThread() {
  ThreadId id = static_cast<ThreadId>(threads_.size());
  Thread* ptr = &threads_.emplace_back();
  // Always add 1 to ids, so that they're non-zero.
  return {id + 1, ptr};
}
Thread* RowDataTracker::GetThread(ThreadId id) {
  // Ids are 1-based and come (transitively) from the parsed trace, so a
  // malformed file can supply kNullId or an out-of-range id. Fail closed rather
  // than indexing out of bounds (see GetBinary for the same pattern).
  if (id == kNullId || id > threads_.size()) {
    return nullptr;
  }
  return &threads_[id - 1];
}

IdPtr<Process> RowDataTracker::NewProcess() {
  ProcessId id = static_cast<ProcessId>(processes_.size());
  Process* ptr = &processes_.emplace_back();
  // Always add 1 to ids, so that they're non-zero.
  return {id + 1, ptr};
}
Process* RowDataTracker::GetProcess(ProcessId id) {
  // Ids are 1-based and come (transitively) from the parsed trace, so a
  // malformed file can supply kNullId or an out-of-range id. Fail closed rather
  // than indexing out of bounds (see GetBinary for the same pattern).
  if (id == kNullId || id > processes_.size()) {
    return nullptr;
  }
  return &processes_[id - 1];
}

IdPtr<Frame> RowDataTracker::NewFrame() {
  BacktraceFrameId id = static_cast<BacktraceFrameId>(frames_.size());
  Frame* ptr = &frames_.emplace_back();
  // Always add 1 to ids, so that they're non-zero.
  return {id + 1, ptr};
}
Frame* RowDataTracker::GetFrame(BacktraceFrameId id) {
  // Ids are 1-based and come (transitively) from the parsed trace, so a
  // malformed file can supply kNullId or an out-of-range id. Fail closed rather
  // than indexing out of bounds (see GetBinary for the same pattern).
  if (id == kNullId || id > frames_.size()) {
    return nullptr;
  }
  return &frames_[id - 1];
}

IdPtr<Backtrace> RowDataTracker::NewBacktrace() {
  BacktraceId id = static_cast<BacktraceId>(backtraces_.size());
  Backtrace* ptr = &backtraces_.emplace_back();
  // Always add 1 to ids, so that they're non-zero.
  return {id + 1, ptr};
}
Backtrace* RowDataTracker::GetBacktrace(BacktraceId id) {
  // Ids are 1-based and come (transitively) from the parsed trace, so a
  // malformed file can supply kNullId or an out-of-range id. Fail closed rather
  // than indexing out of bounds (see GetBinary for the same pattern).
  if (id == kNullId || id > backtraces_.size()) {
    return nullptr;
  }
  return &backtraces_[id - 1];
}

IdPtr<Binary> RowDataTracker::NewBinary() {
  BinaryId id = static_cast<BinaryId>(binaries_.size());
  Binary* ptr = &binaries_.emplace_back();
  // Always add 1 to ids, so that they're non-zero.
  return {id + 1, ptr};
}
Binary* RowDataTracker::GetBinary(BinaryId id) {
  // Frames are allowed to have null binaries; an out-of-range id comes from a
  // malformed trace, so fail closed rather than indexing out of bounds.
  if (id == kNullId || id > binaries_.size())
    return nullptr;
  return &binaries_[id - 1];
}

}  // namespace perfetto::trace_processor::instruments_importer
