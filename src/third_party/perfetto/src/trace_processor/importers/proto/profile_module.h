/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PROFILE_MODULE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PROFILE_MODULE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/perf_sample_tracker.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/sorter/trace_sorter.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

// Importer module for heap and CPU sampling profile data.
// TODO(eseckler): consider moving heap profiles here as well.
class ProfileModule : public ProtoImporterModule {
 public:
  explicit ProfileModule(ProtoImporterModuleContext* module_context,
                         TraceProcessorContext* context);
  ~ProfileModule() override;

  ModuleResult TokenizePacket(const TokenizePacketArgs& args) override;

  void ParseField(const ParseFieldArgs& args) override;

  void OnEventsFullyExtracted() override;

  // chrome stack sampling: sink callback for per-sample sorter events.
  void ParseStreamingProfileSample(int64_t ts, StreamingProfileSampleEvent);

 private:
  // chrome stack sampling:
  ModuleResult TokenizeStreamingProfilePacket(
      RefPtr<PacketSequenceStateGeneration>,
      TraceBlobView* packet,
      protozero::ConstBytes streaming_profile_packet);

  // perf event profiling:
  void ParsePerfSample(int64_t ts,
                       PacketSequenceStateGeneration* sequence_state,
                       const SelectiveTracePacketDecoder& decoder,
                       const TracePacketField& field);

  // heap profiling:
  void ParseProfilePacket(int64_t ts,
                          PacketSequenceStateGeneration*,
                          protozero::ConstBytes);
  void ParseModuleSymbols(protozero::ConstBytes);
  void ParseSmapsPacket(int64_t ts, protozero::ConstBytes);
  void ParsePackedSmaps(int64_t ts, UniquePid upid, protozero::ConstBytes);

  // Identifies a heap_profile row: a (process, dump end ts, heap) triple. The
  // heap name is absent for older producers that don't emit one.
  struct SeenHeapProfile {
    UniquePid upid;
    int64_t window_end;
    std::optional<StringPool::Id> heap_name;

    bool operator==(const SeenHeapProfile& o) const {
      return upid == o.upid && window_end == o.window_end &&
             heap_name == o.heap_name;
    }

    template <typename H>
    friend H PerfettoHashValue(H h, const SeenHeapProfile& s) {
      return H::Combine(std::move(h), s.upid, s.window_end,
                        s.heap_name.has_value(),
                        s.heap_name ? s.heap_name->raw_id() : 0u);
    }
  };

  TraceProcessorContext* context_;
  const StringPool::Id chrome_source_id_;
  const StringPool::Id linux_perf_source_id_;
  PerfSampleTracker perf_sample_tracker_;
  std::unique_ptr<TraceSorter::Stream<StreamingProfileSampleEvent>>
      streaming_profile_stream_;

  // heap_profile rows already emitted, so the per-dump row is written once per
  // heap despite the dump header repeating across continued ProfilePackets.
  // Used as a set: the value is unused.
  base::FlatHashMap<SeenHeapProfile, std::nullptr_t> seen_heap_profiles_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PROFILE_MODULE_H_
