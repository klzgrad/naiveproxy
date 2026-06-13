/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/importers/ftrace/generic_ftrace_tracker.h"

#include "perfetto/base/logging.h"
#include "perfetto/protozero/proto_utils.h"
#include "protos/perfetto/common/descriptor.pbzero.h"

#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {
using protozero::proto_utils::ProtoSchemaType;

namespace {

// We do not expect tracepoints with over 32 fields. It's more likely that the
// trace is corrupted. See also |kMaxFtraceEventFields| in ftrace_descriptors.h.
static constexpr uint32_t kMaxAllowedFields = 32;

static constexpr char kScopeFieldNamePrefix[] = "scope_";

// Track blueprints for kernel track events.
constexpr auto kThreadSliceTrackBp = tracks::SliceBlueprint(  //
    "kernel_trackevent_thread_slice",
    tracks::DimensionBlueprints(
        tracks::kThreadDimensionBlueprint,
        tracks::StringIdDimensionBlueprint("tracepoint"),
        tracks::StringIdDimensionBlueprint("name")),
    tracks::DynamicNameBlueprint());
constexpr auto kThreadCounterTrackBp = tracks::CounterBlueprint(
    "kernel_trackevent_thread_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::kThreadDimensionBlueprint,
        tracks::StringIdDimensionBlueprint("tracepoint"),
        tracks::StringIdDimensionBlueprint("name")),
    tracks::DynamicNameBlueprint());

constexpr auto kProcessSliceTrackBp = tracks::SliceBlueprint(  //
    "kernel_trackevent_process_slice",
    tracks::DimensionBlueprints(
        tracks::kProcessDimensionBlueprint,
        tracks::StringIdDimensionBlueprint("tracepoint"),
        tracks::StringIdDimensionBlueprint("name")),
    tracks::DynamicNameBlueprint());
constexpr auto kProcessCounterTrackBp = tracks::CounterBlueprint(
    "kernel_trackevent_process_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::kProcessDimensionBlueprint,
        tracks::StringIdDimensionBlueprint("tracepoint"),
        tracks::StringIdDimensionBlueprint("name")),
    tracks::DynamicNameBlueprint());

constexpr auto kCpuSliceTrackBp = tracks::SliceBlueprint(  //
    "kernel_trackevent_cpu_slice",
    tracks::DimensionBlueprints(
        tracks::kCpuDimensionBlueprint,
        tracks::StringIdDimensionBlueprint("tracepoint"),
        tracks::StringIdDimensionBlueprint("name")),
    tracks::DynamicNameBlueprint());
constexpr auto kCpuCounterTrackBp = tracks::CounterBlueprint(
    "kernel_trackevent_cpu_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::kCpuDimensionBlueprint,
        tracks::StringIdDimensionBlueprint("tracepoint"),
        tracks::StringIdDimensionBlueprint("name")),
    tracks::DynamicNameBlueprint());

constexpr auto kCustomSliceTrackBp = tracks::SliceBlueprint(  //
    "kernel_trackevent_custom_slice",
    tracks::DimensionBlueprints(
        tracks::LongDimensionBlueprint("scope"),
        tracks::StringIdDimensionBlueprint("tracepoint"),
        tracks::StringIdDimensionBlueprint("name")),
    tracks::DynamicNameBlueprint());
constexpr auto kCustomCounterTrackBp = tracks::CounterBlueprint(
    "kernel_trackevent_custom_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::LongDimensionBlueprint("scope"),
        tracks::StringIdDimensionBlueprint("tracepoint"),
        tracks::StringIdDimensionBlueprint("name")),
    tracks::DynamicNameBlueprint());

bool IsSimpleVarint(ProtoSchemaType t) {
  // not expecting fixed or zigzag encodings from our ftrace serialiser
  return t == ProtoSchemaType::kInt64 || t == ProtoSchemaType::kUint64 ||
         t == ProtoSchemaType::kInt32 || t == ProtoSchemaType::kUint32;
}

}  // namespace

GenericFtraceTracker::GenericFtraceTracker(TraceProcessorContext* context)
    : context_(context),
      track_event_type_(context->storage->InternString("track_event_type")),
      slice_name_(context->storage->InternString("slice_name")),
      track_name_(context->storage->InternString("track_name")),
      counter_value_(context->storage->InternString("counter_value")),
      scope_tgid_(context->storage->InternString("scope_tgid")),
      scope_cpu_(context->storage->InternString("scope_cpu")) {}

GenericFtraceTracker::~GenericFtraceTracker() = default;

void GenericFtraceTracker::AddDescriptor(uint32_t pb_field_id,
                                         protozero::ConstBytes pb_descriptor) {
  if (events_.Find(pb_field_id))
    return;  // already added

  protos::pbzero::DescriptorProto::Decoder decoder(pb_descriptor);

  GenericEvent event;
  event.name = context_->storage->InternString(decoder.name());
  for (auto it = decoder.field(); it; ++it) {
    protos::pbzero::FieldDescriptorProto::Decoder field_decoder(it->data(),
                                                                it->size());

    uint32_t field_id = static_cast<uint32_t>(field_decoder.number());
    if (field_id >= kMaxAllowedFields) {
      PERFETTO_DLOG("Skipping generic descriptor with >32 fields.");
      context_->storage->IncrementStats(
          stats::ftrace_generic_descriptor_errors);
      return;
    }
    if (field_decoder.type() > static_cast<int32_t>(ProtoSchemaType::kSint64)) {
      PERFETTO_DLOG("Skipping generic descriptor with invalid field type.");
      context_->storage->IncrementStats(
          stats::ftrace_generic_descriptor_errors);
      return;
    }

    if (field_id >= event.fields.size()) {
      event.fields.resize(field_id + 1);
    }
    GenericField& field = event.fields[field_id];

    field.name = context_->storage->InternString(field_decoder.name());
    field.type = static_cast<ProtoSchemaType>(field_decoder.type());
  }
  MatchTrackEventTemplate(pb_field_id, event);
  events_.Insert(pb_field_id, std::move(event));
}

GenericFtraceTracker::GenericEvent* GenericFtraceTracker::GetEvent(
    uint32_t pb_field_id) {
  return events_.Find(pb_field_id);
}

void GenericFtraceTracker::MatchTrackEventTemplate(uint32_t pb_field_id,
                                                   const GenericEvent& event) {
  // Find whether the tracepoint field names match our convention for kernel
  // track events.
  KernelTrackEvent info = {};
  info.event_name = event.name;
  for (uint32_t field_id = 1; field_id < event.fields.size(); field_id++) {
    const GenericField& field = event.fields[field_id];

    if (field.name == track_event_type_ && IsSimpleVarint(field.type)) {
      info.slice_type_field_id = field_id;
    } else if (field.name == slice_name_ &&
               field.type == ProtoSchemaType::kString) {
      info.slice_name_field_id = field_id;
    } else if (field.name == track_name_ &&
               field.type == ProtoSchemaType::kString) {
      info.track_name_field_id = field_id;
    } else if (field.name == counter_value_ && IsSimpleVarint(field.type)) {
      info.value_field_id = field_id;
    }
    // scope fields: well-known names or a prefix.
    else if (field.name == scope_tgid_ && IsSimpleVarint(field.type)) {
      info.scope_field_id = field_id;
      info.scope_type = KernelTrackEvent::kTgid;
    } else if (field.name == scope_cpu_ && IsSimpleVarint(field.type)) {
      info.scope_field_id = field_id;
      info.scope_type = KernelTrackEvent::kCpu;
    } else if (context_->storage->GetString(field.name)
                   .StartsWith(kScopeFieldNamePrefix) &&
               IsSimpleVarint(field.type)) {
      info.scope_field_id = field_id;
      info.scope_type = KernelTrackEvent::kCustom;
    }
  }

  if (info.slice_type_field_id && info.slice_name_field_id) {
    info.kind = KernelTrackEvent::EventKind::kSlice;
  } else if (info.value_field_id) {
    info.kind = KernelTrackEvent::EventKind::kCounter;
  } else {
    // common case: tracepoint doesn't look like a kernel track event
    return;
  }
  track_event_info_.Insert(pb_field_id, info);
}

void GenericFtraceTracker::MaybeParseAsTrackEvent(
    uint32_t pb_field_id,
    int64_t ts,
    uint32_t tid,
    protozero::ProtoDecoder& decoder) {
  auto* maybe_info = track_event_info_.Find(pb_field_id);
  if (!maybe_info)
    return;  // doesn't need trackevent handling

  // Track name: default = tracepoint's name. Or taken from payload.
  const KernelTrackEvent& info = *maybe_info;
  StringId track_name = info.event_name;
  if (info.track_name_field_id) {
    protozero::Field track_name_fld =
        decoder.FindField(info.track_name_field_id);
    if (!track_name_fld.valid()) {
      return context_->storage->IncrementStats(
          stats::kernel_trackevent_format_error);
    }
    track_name = context_->storage->InternString(track_name_fld.as_string());
  }

  // Track lookup: default to thread-scoped events, with an optional field that
  // overrides the scoping. Note: track name is an additional scoping dimension.
  TrackId track_id = kInvalidTrackId;
  switch (info.scope_type) {
    case KernelTrackEvent::kTid: {
      UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);

      const auto& track_kind = (info.kind == KernelTrackEvent::kSlice)
                                   ? kThreadSliceTrackBp
                                   : kThreadCounterTrackBp;
      track_id = context_->track_tracker->InternTrack(
          track_kind, tracks::Dimensions(utid, info.event_name, track_name),
          tracks::DynamicName(track_name));
      break;
    }
    case KernelTrackEvent::kTgid: {
      protozero::Field scope_tgid = decoder.FindField(info.scope_field_id);
      if (!scope_tgid.valid()) {
        return context_->storage->IncrementStats(
            stats::kernel_trackevent_format_error);
      }

      // Trusting that this is a real tgid, but *not* assuming that the
      // emitting thread is inside the tgid.
      UniquePid upid =
          context_->process_tracker->GetOrCreateProcess(scope_tgid.as_int64());

      const auto& track_kind = (info.kind == KernelTrackEvent::kSlice)
                                   ? kProcessSliceTrackBp
                                   : kProcessCounterTrackBp;
      track_id = context_->track_tracker->InternTrack(
          track_kind, tracks::Dimensions(upid, info.event_name, track_name),
          tracks::DynamicName(track_name));
      break;
    }
    case KernelTrackEvent::kCpu: {
      protozero::Field scope_cpu = decoder.FindField(info.scope_field_id);
      if (!scope_cpu.valid()) {
        return context_->storage->IncrementStats(
            stats::kernel_trackevent_format_error);
      }

      // Trusting that this is a real cpu number.
      const auto& track_kind = (info.kind == KernelTrackEvent::kSlice)
                                   ? kCpuSliceTrackBp
                                   : kCpuCounterTrackBp;
      track_id = context_->track_tracker->InternTrack(
          track_kind,
          tracks::Dimensions(scope_cpu.as_uint32(), info.event_name,
                             track_name),
          tracks::DynamicName(track_name));
      break;
    }
    case KernelTrackEvent::kCustom:
      protozero::Field scope = decoder.FindField(info.scope_field_id);
      if (!scope.valid()) {
        return context_->storage->IncrementStats(
            stats::kernel_trackevent_format_error);
      }

      const auto& track_kind = (info.kind == KernelTrackEvent::kSlice)
                                   ? kCustomSliceTrackBp
                                   : kCustomCounterTrackBp;
      track_id = context_->track_tracker->InternTrack(
          track_kind,
          tracks::Dimensions(scope.as_int64(), info.event_name, track_name),
          tracks::DynamicName(track_name));
      break;
  }
  PERFETTO_DCHECK(track_id != kInvalidTrackId);

  // Insert the slice/counter data.
  if (info.kind == KernelTrackEvent::kSlice) {
    protozero::Field slice_type = decoder.FindField(info.slice_type_field_id);
    protozero::Field slice_name = decoder.FindField(info.slice_name_field_id);
    if (!slice_type.valid() || !slice_name.valid()) {
      return context_->storage->IncrementStats(
          stats::kernel_trackevent_format_error);
    }

    switch (static_cast<char>(slice_type.as_int64())) {
      case 'B': {  // begin
        context_->slice_tracker->Begin(
            ts, track_id, kNullStringId,
            context_->storage->InternString(slice_name.as_string()));
        break;
      }
      case 'E': {  // end
        context_->slice_tracker->End(ts, track_id);
        break;
      }
      case 'I': {  // instant
        context_->slice_tracker->Scoped(
            ts, track_id, kNullStringId,
            context_->storage->InternString(slice_name.as_string()),
            /*duration=*/0);
        break;
      }
      default: {
        return context_->storage->IncrementStats(
            stats::kernel_trackevent_format_error);
      }
    }
  } else if (info.kind == KernelTrackEvent::kCounter) {
    protozero::Field value = decoder.FindField(info.value_field_id);
    if (!value.valid()) {
      return context_->storage->IncrementStats(
          stats::kernel_trackevent_format_error);
    }
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(value.as_int64()), track_id);
  }
}

}  // namespace perfetto::trace_processor
