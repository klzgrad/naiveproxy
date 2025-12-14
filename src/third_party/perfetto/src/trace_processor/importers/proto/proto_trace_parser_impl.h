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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PROTO_TRACE_PARSER_IMPL_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PROTO_TRACE_PARSER_IMPL_H_

#include <cstdint>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto {

namespace protos::pbzero {
class TracePacket_Decoder;
}  // namespace protos::pbzero

namespace trace_processor {

class PacketSequenceState;
struct ProtoImporterModuleContext;
class TraceProcessorContext;

class ProtoTraceParserImpl {
 public:
  using ConstBytes = protozero::ConstBytes;
  ProtoTraceParserImpl(TraceProcessorContext*, ProtoImporterModuleContext*);
  ~ProtoTraceParserImpl();

  void ParseTrackEvent(int64_t ts, TrackEventData data);
  void ParseTracePacket(int64_t ts, TracePacketData data);
  void ParseEtwEvent(uint32_t cpu, int64_t /*ts*/, TracePacketData data);
  void ParseFtraceEvent(uint32_t cpu, int64_t /*ts*/, TracePacketData data);
  void ParseInlineSchedSwitch(uint32_t cpu,
                              int64_t /*ts*/,
                              InlineSchedSwitch data);
  void ParseInlineSchedWaking(uint32_t cpu,
                              int64_t /*ts*/,
                              InlineSchedWaking data);

 private:
  StringId GetMetatraceInternedString(uint64_t iid);

  void ParseChromeEvents(int64_t ts, ConstBytes);
  void ParseMetatraceEvent(int64_t ts, ConstBytes);

  TraceProcessorContext* context_;
  ProtoImporterModuleContext* module_context_ = nullptr;

  const StringId metatrace_id_;
  const StringId data_name_id_;
  const StringId raw_chrome_metadata_event_id_;
  const StringId raw_chrome_legacy_system_trace_event_id_;
  const StringId raw_chrome_legacy_user_trace_event_id_;
  const StringId missing_metatrace_interned_string_id_;

  base::FlatHashMap<uint64_t, StringId> metatrace_interned_strings_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PROTO_TRACE_PARSER_IMPL_H_
