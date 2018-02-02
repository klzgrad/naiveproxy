// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/memory_dump_request_args.h"

#include "base/logging.h"

namespace base {
namespace trace_event {

// static
const char* MemoryDumpTypeToString(const MemoryDumpType& dump_type) {
  switch (dump_type) {
    case MemoryDumpType::PERIODIC_INTERVAL:
      return "periodic_interval";
    case MemoryDumpType::EXPLICITLY_TRIGGERED:
      return "explicitly_triggered";
    case MemoryDumpType::PEAK_MEMORY_USAGE:
      return "peak_memory_usage";
    case MemoryDumpType::SUMMARY_ONLY:
      return "summary_only";
  }
  NOTREACHED();
  return "unknown";
}

MemoryDumpType StringToMemoryDumpType(const std::string& str) {
  if (str == "periodic_interval")
    return MemoryDumpType::PERIODIC_INTERVAL;
  if (str == "explicitly_triggered")
    return MemoryDumpType::EXPLICITLY_TRIGGERED;
  if (str == "peak_memory_usage")
    return MemoryDumpType::PEAK_MEMORY_USAGE;
  if (str == "summary_only")
    return MemoryDumpType::SUMMARY_ONLY;
  NOTREACHED();
  return MemoryDumpType::LAST;
}

const char* MemoryDumpLevelOfDetailToString(
    const MemoryDumpLevelOfDetail& level_of_detail) {
  switch (level_of_detail) {
    case MemoryDumpLevelOfDetail::BACKGROUND:
      return "background";
    case MemoryDumpLevelOfDetail::LIGHT:
      return "light";
    case MemoryDumpLevelOfDetail::VM_REGIONS_ONLY_FOR_HEAP_PROFILER:
      return "vm_regions_only";
    case MemoryDumpLevelOfDetail::DETAILED:
      return "detailed";
  }
  NOTREACHED();
  return "unknown";
}

MemoryDumpLevelOfDetail StringToMemoryDumpLevelOfDetail(
    const std::string& str) {
  if (str == "background")
    return MemoryDumpLevelOfDetail::BACKGROUND;
  if (str == "light")
    return MemoryDumpLevelOfDetail::LIGHT;
  if (str == "vm_regions_only")
    return MemoryDumpLevelOfDetail::VM_REGIONS_ONLY_FOR_HEAP_PROFILER;
  if (str == "detailed")
    return MemoryDumpLevelOfDetail::DETAILED;
  NOTREACHED();
  return MemoryDumpLevelOfDetail::LAST;
}

}  // namespace trace_event
}  // namespace base
