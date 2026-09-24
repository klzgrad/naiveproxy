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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_SELECTIVE_TRACK_EVENT_DECODER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_SELECTIVE_TRACK_EVENT_DECODER_H_

#include <stddef.h>
#include <stdint.h>

#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"
#include "src/trace_processor/importers/proto/typed_proto_field.h"

namespace perfetto::trace_processor {

// The fields handed to TrackEvent extension plugins are TrackEvent fields.
using TrackEventField = TypedProtoField;

namespace internal {

// The TrackEvent fields the parser reads by name (the dense allowlist for
// selective decoding below). Every other field -- in particular the out-of-tree
// extensions (`extensions 1000 to 9999`) -- lands in unknown_fields().
using TrackEventDenseMask = protozero::SelectiveDecodeMask<
    protos::pbzero::TrackEvent::kTypeFieldNumber,
    protos::pbzero::TrackEvent::kTrackUuidFieldNumber,
    protos::pbzero::TrackEvent::kCategoryIidsFieldNumber,
    protos::pbzero::TrackEvent::kCategoriesFieldNumber,
    protos::pbzero::TrackEvent::kNameIidFieldNumber,
    protos::pbzero::TrackEvent::kNameFieldNumber,
    protos::pbzero::TrackEvent::kCounterValueFieldNumber,
    protos::pbzero::TrackEvent::kDoubleCounterValueFieldNumber,
    protos::pbzero::TrackEvent::kExtraCounterTrackUuidsFieldNumber,
    protos::pbzero::TrackEvent::kExtraCounterValuesFieldNumber,
    protos::pbzero::TrackEvent::kExtraDoubleCounterTrackUuidsFieldNumber,
    protos::pbzero::TrackEvent::kExtraDoubleCounterValuesFieldNumber,
    protos::pbzero::TrackEvent::kFlowIdsFieldNumber,
    protos::pbzero::TrackEvent::kTerminatingFlowIdsFieldNumber,
    protos::pbzero::TrackEvent::kCorrelationIdFieldNumber,
    protos::pbzero::TrackEvent::kCorrelationIdStrFieldNumber,
    protos::pbzero::TrackEvent::kCorrelationIdStrIidFieldNumber,
    protos::pbzero::TrackEvent::kCallstackFieldNumber,
    protos::pbzero::TrackEvent::kCallstackIidFieldNumber,
    protos::pbzero::TrackEvent::kDebugAnnotationsFieldNumber,
    protos::pbzero::TrackEvent::kTaskExecutionFieldNumber,
    protos::pbzero::TrackEvent::kLogMessageFieldNumber,
    protos::pbzero::TrackEvent::kCcSchedulerStateFieldNumber,
    protos::pbzero::TrackEvent::kChromeUserEventFieldNumber,
    protos::pbzero::TrackEvent::kChromeKeyedServiceFieldNumber,
    protos::pbzero::TrackEvent::kChromeLegacyIpcFieldNumber,
    protos::pbzero::TrackEvent::kChromeHistogramSampleFieldNumber,
    protos::pbzero::TrackEvent::kChromeLatencyInfoFieldNumber,
    protos::pbzero::TrackEvent::kChromeApplicationStateInfoFieldNumber,
    protos::pbzero::TrackEvent::kChromeRendererSchedulerStateFieldNumber,
    protos::pbzero::TrackEvent::kChromeWindowHandleEventInfoFieldNumber,
    protos::pbzero::TrackEvent::kChromeActiveProcessesFieldNumber,
    protos::pbzero::TrackEvent::kScreenshotFieldNumber,
    protos::pbzero::TrackEvent::kSourceLocationFieldNumber,
    protos::pbzero::TrackEvent::kSourceLocationIidFieldNumber,
    protos::pbzero::TrackEvent::kChromeMessagePumpFieldNumber,
    protos::pbzero::TrackEvent::kChromeMojoEventInfoFieldNumber,
    protos::pbzero::TrackEvent::kLegacyEventFieldNumber>;

inline constexpr TrackEventDenseMask kTrackEventDenseMask{};

}  // namespace internal

// Hand-maintained wrapper around protozero::SelectiveTypedProtoDecoder for
// TrackEvent, mirroring SelectiveTracePacketDecoder. The allowlist is the set
// of in-tree TrackEvent fields; every other field -- in particular out-of-tree
// extensions (`extensions 1000 to 9999`) -- lands in unknown_fields(), which
// drives TrackEvent extension plugin dispatch.
//
// The parser still reads in-tree fields via the generated TrackEvent::Decoder;
// this wrapper exists to enumerate extension fields cheaply and in wire order.
class SelectiveTrackEventDecoder {
 public:
  using TrackEvent = protos::pbzero::TrackEvent;

  SelectiveTrackEventDecoder(const uint8_t* data, size_t length)
      : decoder_(data, length, internal::kTrackEventDenseMask) {}
  explicit SelectiveTrackEventDecoder(protozero::ConstBytes blob)
      : SelectiveTrackEventDecoder(blob.data, blob.size) {}

  // All the fields not in the allowlist, in wire order, with repeated
  // occurrences preserved. Drives extension plugin dispatch.
  protozero::UnknownFieldRange unknown_fields() const {
    return decoder_.unknown_fields();
  }

  // Returns the first unknown field with the given id (invalid if absent).
  // Linear, but the number of unknown fields per event is tiny.
  TrackEventField FindUnknownField(uint32_t id) const {
    for (const protozero::Field& f : decoder_.unknown_fields()) {
      if (f.id() == id)
        return TrackEventField(f);
    }
    return TrackEventField(protozero::Field{});
  }

  static constexpr bool ContainsField(uint32_t id) {
    return internal::kTrackEventDenseMask.contains(id);
  }

 private:
  protozero::SelectiveTypedProtoDecoder<static_cast<int>(
      internal::TrackEventDenseMask::kMaxFieldId)>
      decoder_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_SELECTIVE_TRACK_EVENT_DECODER_H_
