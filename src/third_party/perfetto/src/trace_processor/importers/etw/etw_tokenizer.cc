/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <optional>

#include "src/trace_processor/importers/etw/etw_tokenizer.h"

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/trace_storage.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/etw/etw_event.pbzero.h"
#include "protos/perfetto/trace/etw/etw_event_bundle.pbzero.h"

namespace perfetto {
namespace trace_processor {

using protozero::ProtoDecoder;
using protozero::proto_utils::MakeTagVarInt;
using protozero::proto_utils::ParseVarInt;

using protos::pbzero::BuiltinClock;
using protos::pbzero::EtwTraceEventBundle;

PERFETTO_ALWAYS_INLINE
base::Status EtwTokenizer::TokenizeEtwBundle(
    TraceBlobView bundle,
    RefPtr<PacketSequenceStateGeneration> state) {
  protos::pbzero::EtwTraceEventBundle::Decoder decoder(bundle.data(),
                                                       bundle.length());
  // Cpu id can either be in the etw bundle or inside the individual
  // EtwTraceEvent. If present at this level, we pass it to the TokenizeEtwEvent
  // in case the EtwTraceEvent does not contain the cpu.
  std::optional<uint32_t> bundle_cpu =
      decoder.has_cpu() ? std::make_optional(decoder.cpu()) : std::nullopt;

  for (auto it = decoder.event(); it; ++it) {
    TokenizeEtwEvent(bundle_cpu, bundle.slice(it->data(), it->size()), state);
  }
  return base::OkStatus();
}

PERFETTO_ALWAYS_INLINE
base::Status EtwTokenizer::TokenizeEtwEvent(
    std::optional<uint32_t> fallback_cpu,
    TraceBlobView event,
    RefPtr<PacketSequenceStateGeneration> state) {
  const uint8_t* data = event.data();
  const size_t length = event.length();
  ProtoDecoder decoder(data, length);

  protos::pbzero::EtwTraceEvent::Decoder etw_decoder(data, length);
  // Some ETW events lack CPU info; in that case, the bundle may
  // provide it.
  uint32_t cpu;
  if (etw_decoder.has_cpu()) {
    cpu = etw_decoder.cpu();
  } else {
    if (!fallback_cpu.has_value()) {
      return base::ErrStatus(
          "CPU field not found in EtwEvent and/or EtwEventBundle");
    }
    cpu = fallback_cpu.value();
  }

  static constexpr uint32_t kMaxCpuCount = 1024;
  if (PERFETTO_UNLIKELY(cpu >= kMaxCpuCount)) {
    return base::ErrStatus(
        "CPU %u is greater than maximum allowed of %u. This is likely because "
        "of trace corruption",
        cpu, kMaxCpuCount);
  }

  uint64_t raw_timestamp = 0;
  if (etw_decoder.has_timestamp()) {
    raw_timestamp = etw_decoder.timestamp();
  } else {
    return base::ErrStatus("Timestamp field not found in EtwEvent");
  }

  base::StatusOr<int64_t> timestamp = static_cast<int64_t>(raw_timestamp);

  // ClockTracker will increment some error stats if it failed to convert the
  // timestamp so just return.
  if (!timestamp.ok()) {
    return timestamp.status();
  }

  context_->sorter->PushEtwEvent(cpu, *timestamp, std::move(event),
                                 std::move(state));

  return base::OkStatus();
}

}  // namespace trace_processor
}  // namespace perfetto
