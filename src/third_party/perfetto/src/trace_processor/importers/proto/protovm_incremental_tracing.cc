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

#include "src/trace_processor/importers/proto/protovm_incremental_tracing.h"

#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/perfetto/trace_provenance.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/protovm/vm.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/proto/blob_packet_writer.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

ProtoVmIncrementalTracing::ProtoVmIncrementalTracing(
    TraceProcessorContext* context)
    : context_(context) {}

ProtoVmIncrementalTracing::~ProtoVmIncrementalTracing() = default;

void ProtoVmIncrementalTracing::ProcessTraceProvenancePacket(
    protozero::ConstBytes blob) {
  protos::pbzero::TraceProvenance::Decoder trace_provenance(blob);
  for (auto it_buf = trace_provenance.buffers(); it_buf; ++it_buf) {
    protos::pbzero::TraceProvenance::Buffer::Decoder buffer(*it_buf);
    for (auto it_seq = buffer.sequences(); it_seq; ++it_seq) {
      protos::pbzero::TraceProvenance::Sequence::Decoder sequence(*it_seq);
      producer_id_to_sequence_ids_[sequence.producer_id()].push_back(
          sequence.id());
    }
  }
}

void ProtoVmIncrementalTracing::ProcessProtoVmsPacket(
    protozero::ConstBytes blob,
    const TraceBlobView& packet) {
  protos::pbzero::TracePacket::ProtoVms::Decoder decoder(blob);
  for (auto it = decoder.instance(); it; ++it) {
    protos::pbzero::TracePacket::ProtoVms::Instance::Decoder instance(*it);
    protozero::ConstBytes state = instance.has_state()
                                      ? instance.state()
                                      : protozero::ConstBytes{nullptr, 0};
    vms_.push_back(std::make_unique<protovm::Vm>(
        instance.program(), 1024 * instance.memory_limit_kb(), state));
    protovm::Vm* vm = vms_.back().get();
    for (auto producer_id = instance.producer_id(); producer_id;
         ++producer_id) {
      auto* sequence_ids = producer_id_to_sequence_ids_.Find(*producer_id);
      if (!sequence_ids) {
        context_->import_logs_tracker->RecordTokenizationError(
            stats::protovm_registration_error, packet.offset());
        continue;
      }
      for (auto sequence_id : *sequence_ids) {
        sequence_id_to_vms_[sequence_id].push_back(vm);
      }
    }
  }
}

std::optional<TraceBlobView> ProtoVmIncrementalTracing::TryProcessPatch(
    const protos::pbzero::TracePacket::Decoder& patch,
    const TraceBlobView& packet) {
  if (PERFETTO_UNLIKELY(!patch.has_trusted_packet_sequence_id())) {
    return std::nullopt;
  }
  std::vector<protovm::Vm*>* vms =
      sequence_id_to_vms_.Find(patch.trusted_packet_sequence_id());
  if (!vms) {
    return std::nullopt;
  }
  for (auto* vm : *vms) {
    auto status =
        vm->ApplyPatch(protozero::ConstBytes{packet.data(), packet.size()});
    if (status.IsOk()) {
      return SerializeIncrementalState(*vm, patch);
    }
    if (status.IsAbort()) {
      context_->import_logs_tracker->RecordTokenizationError(
          stats::protovm_abort, packet.offset());
    }
  }
  return std::nullopt;
}

TraceBlobView ProtoVmIncrementalTracing::SerializeIncrementalState(
    const protovm::Vm& vm,
    const protos::pbzero::TracePacket::Decoder& patch) const {
  return context_->blob_packet_writer->WritePacket(
      [&](protos::pbzero::TracePacket* proto) {
        vm.SerializeIncrementalState(proto);
        proto->set_trusted_uid(patch.trusted_uid());
        proto->set_trusted_pid(patch.trusted_pid());
        proto->set_trusted_packet_sequence_id(
            patch.trusted_packet_sequence_id());
        if (patch.has_machine_id()) {
          proto->set_machine_id(patch.machine_id());
        }
      });
}

}  // namespace perfetto::trace_processor
