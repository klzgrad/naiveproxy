/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_FTRACE_GENERIC_FTRACE_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_FTRACE_GENERIC_FTRACE_TRACKER_H_

#include <cstdint>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/protozero/field.h"

#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/destructible.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

// The latest scheme for serialising generic (i.e. unknown by perfetto at
// compile-time) ftrace events:
// * FtraceEvent proto has a range of field ids reserved for
//   dynamically-generated extensions.
// * FtraceEventBundle proto is populated with a mapping from the field id to a
//   serialised proto descriptor for that event (derived from the tracefs
//   /format file), which stays consistent for all bundles within a trace.
//
// This tracker is used to carry state from the ftrace tokenizer to the parser:
// the earlier submits the descriptors found in "bundle" protos, and the latter
// looks up descriptors when parsing the event payload into the |ftrace_event|
// (aka raw) table.
//
// For more info, see https://github.com/google/perfetto/commit/4c92085.
//
// TODO(rsavitski): consider:
// * deduping struct declarations with compile-time ftrace_descriptors.cc.
// * using base::SmallVec for GenericField (needs a resize() method).
class GenericFtraceTracker {
 public:
  static constexpr uint32_t kGenericEvtProtoMinPbFieldId = 65536;

  struct GenericField {
    StringId name = kNullStringId;
    protozero::proto_utils::ProtoSchemaType type =
        protozero::proto_utils::ProtoSchemaType::kUnknown;
  };

  struct GenericEvent {
    StringId name = kNullStringId;
    // keyed by proto field id of the tracepoint field (0th slot empty)
    std::vector<GenericField> fields;
  };

  explicit GenericFtraceTracker(TraceProcessorContext* context);
  GenericFtraceTracker(const GenericFtraceTracker&) = delete;
  GenericFtraceTracker& operator=(const GenericFtraceTracker&) = delete;
  ~GenericFtraceTracker();

  // Returns true if a proto field id inside |FtraceEvent| proto should be
  // parsed using a descriptor from this tracker.
  static bool IsGenericFtraceEvent(uint32_t pb_field_id) {
    return pb_field_id >= kGenericEvtProtoMinPbFieldId;
  }

  // Validate and intern the descriptor seen in the ftrace bundle.
  void AddDescriptor(uint32_t pb_field_id, protozero::ConstBytes pb_descriptor);

  // Look up the descriptor. Can return nullptr, but it likely implies a
  // malformed trace.
  GenericEvent* GetEvent(uint32_t pb_field_id);

 private:
  TraceProcessorContext* const context_;
  // keyed by proto field id inside the FtraceEvent proto
  base::FlatHashMap<uint32_t, GenericEvent> events_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_FTRACE_GENERIC_FTRACE_TRACKER_H_
