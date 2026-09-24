/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HPROF_TYPES_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HPROF_TYPES_H_

#include <string>

namespace perfetto::trace_processor::art_hprof {
enum class HprofTag : uint8_t {
  kUtf8 = 0x01,
  kLoadClass = 0x02,
  kFrame = 0x04,
  kTrace = 0x05,
  kHeapDump = 0x0C,
  kHeapDumpSegment = 0x1C,
  kHeapDumpEnd = 0x2C
};

enum class HprofHeapRootTag : uint8_t {
  kJniGlobal = 0x01,
  kJniLocal = 0x02,
  kJavaFrame = 0x03,
  kNativeStack = 0x04,
  kStickyClass = 0x05,
  kThreadBlock = 0x06,
  kMonitorUsed = 0x07,
  kThreadObj = 0x08,
  kInternedString = 0x89,  // Android
  kFinalizing = 0x8A,      // Android
  kDebugger = 0x8B,        // Android
  kVmInternal = 0x8D,      // Android
  kJniMonitor = 0x8E,      // Android
  kUnknown = 0xFF
};

enum class HprofHeapTag : uint8_t {
  kClassDump = 0x20,
  kInstanceDump = 0x21,
  kObjArrayDump = 0x22,
  kPrimArrayDump = 0x23,
  kHeapDumpInfo = 0xFE
};

enum class FieldType : uint8_t {
  kObject = 2,
  kBoolean = 4,
  kChar = 5,
  kFloat = 6,
  kDouble = 7,
  kByte = 8,
  kShort = 9,
  kInt = 10,
  kLong = 11
};

enum class ObjectType : uint8_t {
  kClass = 0,
  kInstance = 1,
  kObjectArray = 2,
  kPrimitiveArray = 3
};

class HprofHeader {
 public:
  HprofHeader() = default;

  void SetFormat(std::string format) { format_ = std::move(format); }
  void SetIdSize(uint32_t size) { id_size_ = size; }
  void SetTimestamp(uint64_t timestamp) { timestamp_ = timestamp; }

  const std::string& GetFormat() const { return format_; }
  uint32_t GetIdSize() const { return id_size_; }
  uint64_t GetTimestamp() const { return timestamp_; }

 private:
  std::string format_;
  uint32_t id_size_ = 4;  // Default ID size
  uint64_t timestamp_ = 0;
};
}  // namespace perfetto::trace_processor::art_hprof

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HPROF_TYPES_H_
