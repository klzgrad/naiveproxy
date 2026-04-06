/*
 * Copyright (C) 2019 The Android Open Source Project
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
#include "src/trace_processor/importers/proto/track_event_module.h"

#include <cstdint>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/android_track_event.descriptor.h"
#include "src/trace_processor/importers/proto/chrome_track_event_extension.descriptor.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/track_event.descriptor.h"
#include "src/trace_processor/importers/proto/track_event_tracker.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

using perfetto::protos::pbzero::TracePacket;

TrackEventModule::TrackEventModule(ProtoImporterModuleContext* module_context,
                                   TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      track_event_tracker_(new TrackEventTracker(context)),
      tokenizer_(module_context, context, track_event_tracker_.get()),
      parser_(context, track_event_tracker_.get()) {
  RegisterForField(TracePacket::kTrackEventRangeOfInterestFieldNumber);
  RegisterForField(TracePacket::kTrackEventFieldNumber);
  RegisterForField(TracePacket::kTrackDescriptorFieldNumber);
  RegisterForField(TracePacket::kThreadDescriptorFieldNumber);
  RegisterForField(TracePacket::kProcessDescriptorFieldNumber);

  context->descriptor_pool_->AddFromFileDescriptorSet(
      kTrackEventDescriptor.data(), kTrackEventDescriptor.size());
  context->descriptor_pool_->AddFromFileDescriptorSet(
      kChromeTrackEventExtensionDescriptor.data(),
      kChromeTrackEventExtensionDescriptor.size());
  context->descriptor_pool_->AddFromFileDescriptorSet(
      kAndroidTrackEventDescriptor.data(), kAndroidTrackEventDescriptor.size());
}

TrackEventModule::~TrackEventModule() = default;

ModuleResult TrackEventModule::TokenizePacket(
    const TracePacket::Decoder& decoder,
    TraceBlobView* packet,
    int64_t packet_timestamp,
    RefPtr<PacketSequenceStateGeneration> state,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kTrackEventRangeOfInterestFieldNumber:
      return tokenizer_.TokenizeRangeOfInterestPacket(std::move(state), decoder,
                                                      packet, packet_timestamp);
    case TracePacket::kTrackDescriptorFieldNumber:
      return tokenizer_.TokenizeTrackDescriptorPacket(std::move(state), decoder,
                                                      packet, packet_timestamp);
    case TracePacket::kTrackEventFieldNumber:
      return tokenizer_.TokenizeTrackEventPacket(std::move(state), decoder,
                                                 packet, packet_timestamp);
    case TracePacket::kThreadDescriptorFieldNumber:
      // TODO(eseckler): Remove once Chrome has switched to TrackDescriptors.
      return tokenizer_.TokenizeThreadDescriptorPacket(std::move(state),
                                                       decoder, packet);
  }
  return ModuleResult::Ignored();
}

void TrackEventModule::ParseTracePacketData(const TracePacket::Decoder& decoder,
                                            int64_t ts,
                                            const TracePacketData&,
                                            uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kTrackDescriptorFieldNumber:
      parser_.ParseTrackDescriptor(ts, decoder.track_descriptor(),
                                   decoder.trusted_packet_sequence_id());
      break;
    case TracePacket::kProcessDescriptorFieldNumber:
      // TODO(eseckler): Remove once Chrome has switched to TrackDescriptors.
      parser_.ParseProcessDescriptor(ts, decoder.process_descriptor());
      break;
    case TracePacket::kThreadDescriptorFieldNumber:
      // TODO(eseckler): Remove once Chrome has switched to TrackDescriptors.
      parser_.ParseThreadDescriptor(decoder.thread_descriptor(),
                                    /*is_sandboxed=*/false);
      break;
    case TracePacket::kTrackEventFieldNumber:
      PERFETTO_DFATAL("Wrong TracePacket number");
  }
}

void TrackEventModule::OnFirstPacketOnSequence(uint32_t packet_sequence_id) {
  track_event_tracker_->OnFirstPacketOnSequence(packet_sequence_id);
}

void TrackEventModule::ParseTrackEventData(const TracePacket::Decoder& decoder,
                                           int64_t ts,
                                           const TrackEventData& data) {
  parser_.ParseTrackEvent(ts, &data, decoder.track_event(),
                          decoder.trusted_packet_sequence_id());
}

void TrackEventModule::OnEventsFullyExtracted() {
  parser_.OnEventsFullyExtracted();
}

}  // namespace perfetto::trace_processor
