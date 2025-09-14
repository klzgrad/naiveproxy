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

#include "src/trace_processor/importers/proto/pixel_modem_module.h"

#include "perfetto/base/build_config.h"
#include "perfetto/ext/base/string_writer.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_processor/importers/common/machine_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"

#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/pixel_modem_parser.h"
#include "src/trace_processor/sorter/trace_sorter.h"

#include "protos/perfetto/common/android_energy_consumer_descriptor.pbzero.h"
#include "protos/perfetto/trace/android/pixel_modem_events.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace trace_processor {

using perfetto::protos::pbzero::TracePacket;

PixelModemModule::PixelModemModule(TraceProcessorContext* context)
    : context_(context), parser_(context) {
  RegisterForField(TracePacket::kPixelModemEventsFieldNumber, context);
  RegisterForField(TracePacket::kPixelModemTokenDatabaseFieldNumber, context);
}

ModuleResult PixelModemModule::TokenizePacket(
    const protos::pbzero::TracePacket_Decoder& decoder,
    TraceBlobView* /* packet */,
    int64_t packet_timestamp,
    RefPtr<PacketSequenceStateGeneration> state,
    uint32_t field_id) {
  // The database packet does not have a timestamp so needs to be handled at
  // the tokenization phase.
  if (field_id == TracePacket::kPixelModemTokenDatabaseFieldNumber) {
    auto db = decoder.pixel_modem_token_database();
    protos::pbzero::PixelModemTokenDatabase::Decoder database(db);

    base::Status status = parser_.SetDatabase(database.database());
    if (status.ok()) {
      return ModuleResult::Handled();
    } else {
      return ModuleResult::Error(status.message());
    }
  }

  if (field_id != TracePacket::kPixelModemEventsFieldNumber) {
    return ModuleResult::Ignored();
  }

  // Pigweed events are similar to ftrace in that they have many events, each
  // with their own timestamp, packed inside a single TracePacket. This means
  // that, similar to ftrace, we need to unpack them and individually sort them.

  // However, as these events are not perf sensitive, it's not worth adding
  // a lot of machinery to shepherd these events through the sorting queues
  // in a special way. Therefore, we just forge new packets and sort them as if
  // they came from the underlying trace.
  auto events = decoder.pixel_modem_events();
  protos::pbzero::PixelModemEvents::Decoder evt(events.data, events.size);

  // To reduce overhead we store events and timestamps in parallel lists.
  // We also store timestamps within a packet as deltas.
  auto ts_it = evt.event_time_nanos();
  int64_t ts = 0;
  for (auto it = evt.events(); it && ts_it; ++it, ++ts_it) {
    protozero::ConstBytes event_bytes = *it;
    ts += *ts_it;
    if (ts < 0) {
      context_->storage->IncrementStats(stats::pixel_modem_negative_timestamp);
      continue;
    }

    protozero::HeapBuffered<protos::pbzero::TracePacket> data_packet;
    // Keep the original timestamp to later extract as an arg; the sorter does
    // not read this.
    data_packet->set_timestamp(static_cast<uint64_t>(packet_timestamp));
    data_packet->set_pixel_modem_events()->add_events(event_bytes);
    std::vector<uint8_t> vec = data_packet.SerializeAsArray();
    TraceBlob blob = TraceBlob::CopyFrom(vec.data(), vec.size());
    context_->sorter->PushTracePacket(ts, state, TraceBlobView(std::move(blob)),
                                      context_->machine_id());
  }

  return ModuleResult::Handled();
}

void PixelModemModule::ParseTracePacketData(const TracePacket::Decoder& decoder,
                                            int64_t ts,
                                            const TracePacketData&,
                                            uint32_t field_id) {
  if (field_id != TracePacket::kPixelModemEventsFieldNumber) {
    return;
  }

  auto events = decoder.pixel_modem_events();
  protos::pbzero::PixelModemEvents::Decoder evt(events.data, events.size);
  auto it = evt.events();

  // We guarantee above there will be exactly one event.
  parser_.ParseEvent(ts, decoder.timestamp(), *it);
}

}  // namespace trace_processor
}  // namespace perfetto
