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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_GECKO_GECKO_EVENT_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_GECKO_GECKO_EVENT_H_

#include <cstdint>
#include <memory>
#include <variant>

#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/tables/profiler_tables_py.h"

namespace perfetto::trace_processor::gecko_importer {

struct alignas(8) GeckoEvent {
  struct ThreadMetadata {
    uint32_t tid;
    uint32_t pid;
    StringPool::Id name;
  };
  struct StackSample {
    uint32_t tid;
    tables::StackProfileCallsiteTable::Id callsite_id;
  };
  // Firefox marker phase. Values match the Firefox profiler's wire-format enum.
  // `dur` on `Marker` is only meaningful when phase == kInterval; for kInstant
  // the slice is emitted with dur=0; for kIntervalStart / kIntervalEnd the
  // slice is paired by name via the slice tracker.
  enum class MarkerPhase : uint8_t {
    kInstant = 0,
    kInterval = 1,
    kIntervalStart = 2,
    kIntervalEnd = 3,
  };
  struct Marker {
    uint32_t tid;
    MarkerPhase phase;
    StringPool::Id name;
    StringPool::Id category;
    int64_t dur;
    // Raw JSON bytes for the marker `data` payload (the full object/array
    // including its delimiters). Empty when the marker has no payload. The
    // bytes are flattened into args at parse time by `AddJsonValueToArgs`.
    std::unique_ptr<char[]> data_json;
    uint32_t data_json_size;
  };
  using OneOf = std::variant<ThreadMetadata, StackSample, Marker>;
  OneOf oneof;
};

}  // namespace perfetto::trace_processor::gecko_importer

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_GECKO_GECKO_EVENT_H_
