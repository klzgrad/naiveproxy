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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_SELECTIVE_TRACE_PACKET_DECODER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_SELECTIVE_TRACE_PACKET_DECODER_H_

#include <stddef.h>
#include <stdint.h>

#include "perfetto/base/logging.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/proto/typed_proto_field.h"

namespace perfetto::trace_processor {

// The fields handed to modules by the dispatchers are TracePacket fields.
using TracePacketField = TypedProtoField;

namespace internal {

// The TracePacket metadata fields the pipeline reads by name (the dense
// allowlist for selective decoding below).
using TracePacketDenseMask = protozero::SelectiveDecodeMask<
    protos::pbzero::TracePacket::kTimestampFieldNumber,
    protos::pbzero::TracePacket::kTimestampClockIdFieldNumber,
    protos::pbzero::TracePacket::kTrustedUidFieldNumber,
    protos::pbzero::TracePacket::kTrustedPacketSequenceIdFieldNumber,
    protos::pbzero::TracePacket::kTrustedPidFieldNumber,
    protos::pbzero::TracePacket::kInternedDataFieldNumber,
    protos::pbzero::TracePacket::kSequenceFlagsFieldNumber,
    protos::pbzero::TracePacket::kIncrementalStateClearedFieldNumber,
    protos::pbzero::TracePacket::kPreviousPacketDroppedFieldNumber,
    protos::pbzero::TracePacket::kFirstPacketOnSequenceFieldNumber,
    protos::pbzero::TracePacket::kMachineIdFieldNumber>;

inline constexpr TracePacketDenseMask kTracePacketDenseMask{};

}  // namespace internal

// Hand-maintained wrapper around protozero::SelectiveTypedProtoDecoder
// for TracePacket, used throughout tokenization/parsing and the module API.
//
// The explicit set is an allowlist of the packet *metadata* fields that the
// pipeline reads by name; every other field -- including the per-packet data
// field and out-of-tree extensions (`extensions 1000 to 1999`) -- lands in
// unknown_fields(), which drives module dispatch.
//
// The allowlist must stay disjoint from module-registered field ids: an
// allowlisted field never appears in unknown_fields(), so dispatch on it
// would silently break (enforced in ProtoImporterModule::RegisterForField()).
class SelectiveTracePacketDecoder {
 public:
  using TracePacket = protos::pbzero::TracePacket;

  SelectiveTracePacketDecoder(const uint8_t* data, size_t length)
      : decoder_(data, length, internal::kTracePacketDenseMask) {}
  explicit SelectiveTracePacketDecoder(protozero::ConstBytes blob)
      : SelectiveTracePacketDecoder(blob.data, blob.size) {}

  bool has_timestamp() const {
    return decoder_.at<TracePacket::kTimestampFieldNumber>().valid();
  }
  uint64_t timestamp() const {
    return decoder_.at<TracePacket::kTimestampFieldNumber>().as_uint64();
  }

  bool has_timestamp_clock_id() const {
    return decoder_.at<TracePacket::kTimestampClockIdFieldNumber>().valid();
  }
  uint32_t timestamp_clock_id() const {
    return decoder_.at<TracePacket::kTimestampClockIdFieldNumber>().as_uint32();
  }

  bool has_trusted_uid() const {
    return decoder_.at<TracePacket::kTrustedUidFieldNumber>().valid();
  }
  int32_t trusted_uid() const {
    return decoder_.at<TracePacket::kTrustedUidFieldNumber>().as_int32();
  }

  bool has_trusted_packet_sequence_id() const {
    return decoder_.at<TracePacket::kTrustedPacketSequenceIdFieldNumber>()
        .valid();
  }
  uint32_t trusted_packet_sequence_id() const {
    return decoder_.at<TracePacket::kTrustedPacketSequenceIdFieldNumber>()
        .as_uint32();
  }

  bool has_trusted_pid() const {
    return decoder_.at<TracePacket::kTrustedPidFieldNumber>().valid();
  }
  int32_t trusted_pid() const {
    return decoder_.at<TracePacket::kTrustedPidFieldNumber>().as_int32();
  }

  bool has_interned_data() const {
    return decoder_.at<TracePacket::kInternedDataFieldNumber>().valid();
  }
  protozero::ConstBytes interned_data() const {
    return decoder_.at<TracePacket::kInternedDataFieldNumber>().as_bytes();
  }

  bool has_sequence_flags() const {
    return decoder_.at<TracePacket::kSequenceFlagsFieldNumber>().valid();
  }
  uint32_t sequence_flags() const {
    return decoder_.at<TracePacket::kSequenceFlagsFieldNumber>().as_uint32();
  }

  bool incremental_state_cleared() const {
    return decoder_.at<TracePacket::kIncrementalStateClearedFieldNumber>()
        .as_bool();
  }

  bool previous_packet_dropped() const {
    return decoder_.at<TracePacket::kPreviousPacketDroppedFieldNumber>()
        .as_bool();
  }

  bool first_packet_on_sequence() const {
    return decoder_.at<TracePacket::kFirstPacketOnSequenceFieldNumber>()
        .as_bool();
  }

  bool has_machine_id() const {
    return decoder_.at<TracePacket::kMachineIdFieldNumber>().valid();
  }
  uint32_t machine_id() const {
    return decoder_.at<TracePacket::kMachineIdFieldNumber>().as_uint32();
  }

  // All the fields not in the allowlist, in wire order, with repeated
  // occurrences preserved. Drives module dispatch.
  protozero::UnknownFieldRange unknown_fields() const {
    return decoder_.unknown_fields();
  }

  // Returns the first unknown field with the given id (invalid if absent).
  // Linear, but the number of unknown fields per packet is tiny.
  TracePacketField FindUnknownField(uint32_t id) const {
    for (const protozero::Field& f : decoder_.unknown_fields()) {
      if (f.id() == id)
        return TracePacketField(f);
    }
    return TracePacketField(protozero::Field{});
  }

  static constexpr bool ContainsField(uint32_t id) {
    return internal::kTracePacketDenseMask.contains(id);
  }

 private:
  protozero::SelectiveTypedProtoDecoder<static_cast<int>(
      internal::TracePacketDenseMask::kMaxFieldId)>
      decoder_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_SELECTIVE_TRACE_PACKET_DECODER_H_
