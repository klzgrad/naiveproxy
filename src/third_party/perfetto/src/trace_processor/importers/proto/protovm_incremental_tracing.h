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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PROTOVM_INCREMENTAL_TRACING_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PROTOVM_INCREMENTAL_TRACING_H_

#include <memory>
#include <optional>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace protovm {
class Vm;
}

namespace trace_processor {

class TraceProcessorContext;

class ProtoVmIncrementalTracing {
 public:
  explicit ProtoVmIncrementalTracing(TraceProcessorContext*);
  ~ProtoVmIncrementalTracing();
  void ProcessTraceProvenancePacket(protozero::ConstBytes blob);
  void ProcessProtoVmsPacket(protozero::ConstBytes blob,
                             const TraceBlobView& packet);
  std::optional<TraceBlobView> TryProcessPatch(
      const protos::pbzero::TracePacket::Decoder& patch,
      const TraceBlobView& packet);

 private:
  TraceBlobView SerializeIncrementalState(
      const protovm::Vm& vm,
      const protos::pbzero::TracePacket::Decoder& patch) const;

  TraceProcessorContext* context_;
  base::FlatHashMap<int32_t, std::vector<uint32_t>>
      producer_id_to_sequence_ids_;
  base::FlatHashMap<uint32_t, std::vector<protovm::Vm*>> sequence_id_to_vms_;
  std::vector<std::unique_ptr<protovm::Vm>> vms_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PROTOVM_INCREMENTAL_TRACING_H_
