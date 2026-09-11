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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_ETW_DISK_IO_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_ETW_DISK_IO_TRACKER_H_

#include <cstdint>
#include <unordered_map>

#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

// A class to keep track of disk I/O events recorded by Event Tracing for
// Windows (ETW).
class DiskIoTracker {
 public:
  explicit DiskIoTracker(TraceProcessorContext* context);

  void ParseDiskIo(int64_t timestamp, UniqueTid utid, protozero::ConstBytes);

 private:
  TraceProcessorContext* context_;

  // Strings interned in the constructor to improve performance.
  const StringId disk_number_arg_;
  const StringId irp_flags_arg_;
  const StringId transfer_size_arg_;
  const StringId byte_offset_arg_;
  const StringId file_object_arg_;
  const StringId irp_ptr_arg_;
  const StringId response_time_arg_;
  const StringId issuing_thread_id_arg_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_ETW_DISK_IO_TRACKER_H_
