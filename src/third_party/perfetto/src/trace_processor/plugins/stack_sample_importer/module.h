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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_STACK_SAMPLE_IMPORTER_MODULE_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_STACK_SAMPLE_IMPORTER_MODULE_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/profiling/stack_sample.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"

namespace perfetto::trace_processor {
class DummyMemoryMapping;
class PacketSequenceStateGeneration;
class TraceProcessorContext;
}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::stack_sample_importer {

// Parses the transport-neutral StackSample packets (see
// profiling/stack_sample.proto) into the profiler_sample table. Each packet
// sequence gets a profiler session; counter descriptors (the primary timebase
// and any followers) are interned as counter tracks scoped per their declared
// Scope (per session, or per session and cpu); the per-sample values are
// pushed as counter rows and linked to the sample via a counter set,
// mirroring how linux perf samples work.
class StackSampleModule : public ProtoImporterModule {
 public:
  StackSampleModule(ProtoImporterModuleContext* module_context,
                    TraceProcessorContext* context);

  void ParseField(const ParseFieldArgs& args) override;

 private:
  tables::ProfilerSessionTable::Id GetOrCreateSession(uint32_t seq_id,
                                                      StringId source);

  // Interns the counter track for the counter instance identified by a
  // resolved CounterDescriptor, its scope and the sample's cpu (for
  // SCOPE_CPU counters). Returns std::nullopt if the instance cannot be
  // identified (SCOPE_CPU without a cpu on the sample).
  std::optional<TrackId> InternCounterTrack(
      tables::ProfilerSessionTable::Id session_id,
      StringId source,
      const protos::pbzero::StackSample::CounterDescriptor::Decoder& desc,
      bool is_timebase,
      std::optional<uint32_t> cpu);

  // Collects the counter rows recorded at this sample: the primary weight on
  // the timebase track and any follower weights on their tracks.
  std::vector<CounterId> ParseCounterValues(
      int64_t ts,
      PacketSequenceStateGeneration* sequence_state,
      tables::ProfilerSessionTable::Id session_id,
      StringId source,
      std::optional<uint32_t> cpu,
      const protos::pbzero::StackSample::Decoder& sample,
      const std::optional<protos::pbzero::StackSampleDefaults::Decoder>&
          defaults);

  void ParseStackSample(int64_t ts,
                        uint32_t seq_id,
                        PacketSequenceStateGeneration* sequence_state,
                        protozero::ConstBytes blob);

  std::optional<CallsiteId> ResolveCallstack(
      PacketSequenceStateGeneration* sequence_state,
      std::optional<UniquePid> upid,
      const protos::pbzero::StackSample::Decoder& sample);

  std::optional<tables::ProfilerAsyncContextTable::Id> ResolveAsyncContext(
      PacketSequenceStateGeneration* sequence_state,
      uint64_t iid);

  TraceProcessorContext* const context_;

  // One profiler session per packet sequence emitting StackSample packets.
  base::FlatHashMap<uint32_t, tables::ProfilerSessionTable::Id> sessions_;

  // Lazily-created mapping that inline callstack frames are interned into.
  DummyMemoryMapping* inline_callstack_mapping_ = nullptr;
};

}  // namespace perfetto::trace_processor::stack_sample_importer

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_STACK_SAMPLE_IMPORTER_MODULE_H_
