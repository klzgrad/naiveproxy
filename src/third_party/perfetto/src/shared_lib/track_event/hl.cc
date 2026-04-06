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

#include "perfetto/public/abi/track_event_hl_abi.h"

#include "perfetto/tracing/internal/track_event_internal.h"
#include "src/shared_lib/track_event/ds.h"
#include "src/shared_lib/track_event/serialization.h"

namespace perfetto::shlib {
namespace {

using perfetto::internal::TrackEventInternal;
// All interned string messages for track events must have this field number
// structure.
static constexpr uint32_t kInternedStringIidFieldNumber = 1;
static constexpr uint32_t kInternedStringNameFieldNumber = 2;

protos::pbzero::TrackEvent::Type EventType(int32_t type) {
  using Type = protos::pbzero::TrackEvent::Type;
  auto enum_type = static_cast<PerfettoTeType>(type);
  switch (enum_type) {
    case PERFETTO_TE_TYPE_SLICE_BEGIN:
      return Type::TYPE_SLICE_BEGIN;
    case PERFETTO_TE_TYPE_SLICE_END:
      return Type::TYPE_SLICE_END;
    case PERFETTO_TE_TYPE_INSTANT:
      return Type::TYPE_INSTANT;
    case PERFETTO_TE_TYPE_COUNTER:
      return Type::TYPE_COUNTER;
  }
  return Type::TYPE_UNSPECIFIED;
}

// Appends the fields described by `fields` to `msg`.
void AppendHlProtoFields(TrackEventIncrementalState* incr,
                         protozero::Message* msg,
                         PerfettoTeHlProtoField* const* fields) {
  for (PerfettoTeHlProtoField* const* p = fields; *p != nullptr; p++) {
    switch ((*p)->type) {
      case PERFETTO_TE_HL_PROTO_TYPE_CSTR: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldCstr*>(*p);
        msg->AppendString(field->header.id, field->str);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_CSTR_INTERNED: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldCstrInterned*>(*p);
        PERFETTO_DCHECK(field->interned_type_id != 0);
        if (field->interned_type_id) {
          const char* str = field->str;
          size_t len = strlen(field->str);
          auto res = incr->iids.FindOrAssign(
              static_cast<int32_t>(field->interned_type_id), str, len);
          if (res.newly_assigned) {
            auto* ser = incr->serialized_interned_data
                            ->BeginNestedMessage<protozero::Message>(
                                field->interned_type_id);
            ser->AppendVarInt(kInternedStringIidFieldNumber, res.iid);
            ser->AppendString(kInternedStringNameFieldNumber, field->str);
          }
          msg->AppendVarInt(field->header.id, res.iid);
        }
        // If interned_type_id is zero, this is a user error, we drop the packet
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_BYTES: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldBytes*>(*p);
        msg->AppendBytes(field->header.id, field->buf, field->len);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_NESTED: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldNested*>(*p);
        auto* nested =
            msg->BeginNestedMessage<protozero::Message>(field->header.id);
        AppendHlProtoFields(incr, nested, field->fields);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_VARINT: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldVarInt*>(*p);
        msg->AppendVarInt(field->header.id, field->value);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_FIXED64: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldFixed64*>(*p);
        msg->AppendFixed(field->header.id, field->value);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_FIXED32: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldFixed32*>(*p);
        msg->AppendFixed(field->header.id, field->value);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_DOUBLE: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldDouble*>(*p);
        msg->AppendFixed(field->header.id, field->value);
        break;
      }
      case PERFETTO_TE_HL_PROTO_TYPE_FLOAT: {
        auto field = reinterpret_cast<PerfettoTeHlProtoFieldFloat*>(*p);
        msg->AppendFixed(field->header.id, field->value);
        break;
      }
    }
  }
}

void WriteTrackEvent(TrackEventIncrementalState* incr,
                     protos::pbzero::TrackEvent* event,
                     PerfettoTeCategoryImpl* cat,
                     protos::pbzero::TrackEvent::Type type,
                     const char* name,
                     const PerfettoTeHlExtra* const* extra_data,
                     std::optional<uint64_t> track_uuid,
                     const PerfettoTeCategoryDescriptor* dynamic_cat,
                     bool use_interning) {
  if (type != protos::pbzero::TrackEvent::TYPE_UNSPECIFIED) {
    event->set_type(type);
  }

  if (!dynamic_cat && type != protos::pbzero::TrackEvent::TYPE_SLICE_END &&
      type != protos::pbzero::TrackEvent::TYPE_COUNTER) {
    uint64_t iid = cat->cat_iid;
    auto res = incr->iids.FindOrAssign(
        protos::pbzero::InternedData::kEventCategoriesFieldNumber, &iid,
        sizeof(iid));
    if (res.newly_assigned) {
      auto* ser = incr->serialized_interned_data->add_event_categories();
      ser->set_iid(iid);
      ser->set_name(cat->desc->name);
    }
    event->add_category_iids(iid);
  }

  if (type != protos::pbzero::TrackEvent::TYPE_SLICE_END) {
    if (name) {
      if (use_interning) {
        const void* str = name;
        size_t len = strlen(name);
        auto res = incr->iids.FindOrAssign(
            protos::pbzero::InternedData::kEventNamesFieldNumber, str, len);
        if (res.newly_assigned) {
          auto* ser = incr->serialized_interned_data->add_event_names();
          ser->set_iid(res.iid);
          ser->set_name(name);
        }
        event->set_name_iid(res.iid);
      } else {
        event->set_name(name);
      }
    }
  }

  if (dynamic_cat && type != protos::pbzero::TrackEvent::TYPE_SLICE_END &&
      type != protos::pbzero::TrackEvent::TYPE_COUNTER) {
    event->add_categories(dynamic_cat->name);
  }

  if (track_uuid) {
    event->set_track_uuid(*track_uuid);
  }

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_INT64 &&
        type == protos::pbzero::TrackEvent::TYPE_COUNTER) {
      event->set_counter_value(
          reinterpret_cast<const struct PerfettoTeHlExtraCounterInt64&>(extra)
              .value);
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_DOUBLE) {
      event->set_double_counter_value(
          reinterpret_cast<const struct PerfettoTeHlExtraCounterDouble&>(extra)
              .value);
    }
  }

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_BOOL ||
        extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_UINT64 ||
        extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_INT64 ||
        extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_DOUBLE ||
        extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_STRING ||
        extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_POINTER) {
      auto* dbg = event->add_debug_annotations();
      const char* arg_name = nullptr;
      if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_BOOL) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgBool&>(
                extra);
        dbg->set_bool_value(arg.value);
        arg_name = arg.name;
      } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_UINT64) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgUint64&>(
                extra);
        dbg->set_uint_value(arg.value);
        arg_name = arg.name;
      } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_INT64) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgInt64&>(
                extra);
        dbg->set_int_value(arg.value);
        arg_name = arg.name;
      } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_DOUBLE) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgDouble&>(
                extra);
        dbg->set_double_value(arg.value);
        arg_name = arg.name;
      } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_STRING) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgString&>(
                extra);
        dbg->set_string_value(arg.value);
        arg_name = arg.name;
      } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_POINTER) {
        const auto& arg =
            reinterpret_cast<const struct PerfettoTeHlExtraDebugArgPointer&>(
                extra);
        dbg->set_pointer_value(arg.value);
        arg_name = arg.name;
      }

      if (arg_name != nullptr) {
        const void* str = arg_name;
        size_t len = strlen(arg_name);
        auto res = incr->iids.FindOrAssign(
            protos::pbzero::InternedData::kDebugAnnotationNamesFieldNumber, str,
            len);
        if (res.newly_assigned) {
          auto* ser =
              incr->serialized_interned_data->add_debug_annotation_names();
          ser->set_iid(res.iid);
          ser->set_name(arg_name);
        }
        dbg->set_name_iid(res.iid);
      }
    }
  }

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_FLOW) {
      event->add_flow_ids(
          reinterpret_cast<const struct PerfettoTeHlExtraFlow&>(extra).id);
    }
  }

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_TERMINATING_FLOW) {
      event->add_terminating_flow_ids(
          reinterpret_cast<const struct PerfettoTeHlExtraFlow&>(extra).id);
    }
  }

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_PROTO_FIELDS) {
      const auto* fields =
          reinterpret_cast<const struct PerfettoTeHlExtraProtoFields&>(extra)
              .fields;
      AppendHlProtoFields(incr, event, fields);
    }
  }
}

uint64_t EmitNamedTrack(uint64_t parent_uuid,
                        const char* name,
                        uint64_t id,
                        perfetto::shlib::TrackEventIncrementalState* incr_state,
                        perfetto::TraceWriterBase* trace_writer) {
  uint64_t uuid = parent_uuid;
  uuid ^= PerfettoFnv1a(name, strlen(name));
  uuid ^= id;
  if (incr_state->seen_track_uuids.insert(uuid).second) {
    auto packet = trace_writer->NewTracePacket();
    auto* track_descriptor = packet->set_track_descriptor();
    track_descriptor->set_uuid(uuid);
    if (parent_uuid) {
      track_descriptor->set_parent_uuid(parent_uuid);
    }
    track_descriptor->set_name(name);
  }
  return uuid;
}

uint64_t EmitRegisteredTrack(
    const PerfettoTeRegisteredTrackImpl* registered_track,
    perfetto::shlib::TrackEventIncrementalState* incr_state,
    perfetto::TraceWriterBase* trace_writer) {
  if (incr_state->seen_track_uuids.insert(registered_track->uuid).second) {
    auto packet = trace_writer->NewTracePacket();
    auto* track_descriptor = packet->set_track_descriptor();
    track_descriptor->AppendRawProtoBytes(registered_track->descriptor,
                                          registered_track->descriptor_size);
  }
  return registered_track->uuid;
}

uint64_t EmitProtoTrack(uint64_t uuid,
                        PerfettoTeHlProtoField* const* fields,
                        perfetto::shlib::TrackEventIncrementalState* incr_state,
                        perfetto::TraceWriterBase* trace_writer) {
  if (incr_state->seen_track_uuids.insert(uuid).second) {
    auto packet = trace_writer->NewTracePacket();
    auto* track_descriptor = packet->set_track_descriptor();
    track_descriptor->set_uuid(uuid);
    AppendHlProtoFields(incr_state, track_descriptor, fields);
  }
  return uuid;
}

uint64_t EmitProtoTrackWithParentUuid(
    uint64_t uuid,
    uint64_t parent_uuid,
    PerfettoTeHlProtoField* const* fields,
    perfetto::shlib::TrackEventIncrementalState* incr_state,
    perfetto::TraceWriterBase* trace_writer) {
  if (incr_state->seen_track_uuids.insert(uuid).second) {
    auto packet = trace_writer->NewTracePacket();
    auto* track_descriptor = packet->set_track_descriptor();
    track_descriptor->set_uuid(uuid);
    track_descriptor->set_parent_uuid(parent_uuid);
    AppendHlProtoFields(incr_state, track_descriptor, fields);
  }
  return uuid;
}

// If the category `dyn_cat` is enabled on the data source instance pointed by
// `ii`, returns immediately. Otherwise, advances `ii` to a data source instance
// where `dyn_cat` is enabled. If there's no data source instance where
// `dyn_cat` is enabled, `ii->instance` will be nullptr.
void AdvanceToFirstEnabledDynamicCategory(
    internal::DataSourceType::InstancesIterator* ii,
    internal::DataSourceThreadLocalState* tls_state,
    struct PerfettoTeCategoryImpl* cat,
    const PerfettoTeCategoryDescriptor& dyn_cat) {
  internal::DataSourceType* ds = shlib::TrackEvent::GetType();
  for (; ii->instance; ds->NextIteration</*Traits=*/shlib::TracePointTraits>(
           ii, tls_state, {cat})) {
    auto* incr_state = static_cast<TrackEventIncrementalState*>(
        ds->GetIncrementalState(ii->instance, ii->i));
    if (shlib::TrackEvent::IsDynamicCategoryEnabled(ii->i, incr_state,
                                                    dyn_cat)) {
      break;
    }
  }
}

void InstanceOp(internal::DataSourceType* ds,
                internal::DataSourceType::InstancesIterator* ii,
                internal::DataSourceThreadLocalState* tls_state,
                struct PerfettoTeCategoryImpl* cat,
                protos::pbzero::TrackEvent::Type type,
                const char* name,
                struct PerfettoTeHlExtra* const* extra_data) {
  if (!ii->instance) {
    return;
  }

  std::variant<std::monostate, const PerfettoTeRegisteredTrackImpl*,
               const PerfettoTeHlExtraNamedTrack*,
               const PerfettoTeHlExtraProtoTrack*,
               const PerfettoTeHlExtraNestedTracks*>
      track;
  std::optional<uint64_t> track_uuid;

  const struct PerfettoTeHlExtraTimestamp* custom_timestamp = nullptr;
  const struct PerfettoTeCategoryDescriptor* dynamic_cat = nullptr;
  std::optional<int64_t> int_counter;
  std::optional<double> double_counter;
  bool use_interning = true;
  bool flush = false;

  for (const auto* it = extra_data; *it != nullptr; it++) {
    const struct PerfettoTeHlExtra& extra = **it;
    if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_REGISTERED_TRACK) {
      const auto& cast =
          reinterpret_cast<const struct PerfettoTeHlExtraRegisteredTrack&>(
              extra);
      track = cast.track;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_NAMED_TRACK) {
      track =
          &reinterpret_cast<const struct PerfettoTeHlExtraNamedTrack&>(extra);
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_PROTO_TRACK) {
      track =
          &reinterpret_cast<const struct PerfettoTeHlExtraProtoTrack&>(extra);
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_NESTED_TRACKS) {
      auto* nested =
          &reinterpret_cast<const struct PerfettoTeHlExtraNestedTracks&>(extra);
      track = nested;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_TIMESTAMP) {
      custom_timestamp =
          &reinterpret_cast<const struct PerfettoTeHlExtraTimestamp&>(extra);
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_DYNAMIC_CATEGORY) {
      dynamic_cat =
          reinterpret_cast<const struct PerfettoTeHlExtraDynamicCategory&>(
              extra)
              .desc;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_INT64) {
      int_counter =
          reinterpret_cast<const struct PerfettoTeHlExtraCounterInt64&>(extra)
              .value;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_DOUBLE) {
      double_counter =
          reinterpret_cast<const struct PerfettoTeHlExtraCounterInt64&>(extra)
              .value;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_NO_INTERN) {
      use_interning = false;
    } else if (extra.type == PERFETTO_TE_HL_EXTRA_TYPE_FLUSH) {
      flush = true;
    }
  }

  TraceTimestamp ts;
  if (custom_timestamp) {
    ts.clock_id = custom_timestamp->timestamp.clock_id;
    ts.value = custom_timestamp->timestamp.value;
  } else {
    ts = TrackEventInternal::GetTraceTime();
  }

  if (PERFETTO_UNLIKELY(dynamic_cat)) {
    AdvanceToFirstEnabledDynamicCategory(ii, tls_state, cat, *dynamic_cat);
    if (!ii->instance) {
      return;
    }
  }

  perfetto::TraceWriterBase* trace_writer = ii->instance->trace_writer.get();

  const auto& track_event_tls = *static_cast<TrackEventTlsState*>(
      ii->instance->data_source_custom_tls.get());

  auto* incr_state = static_cast<TrackEventIncrementalState*>(
      ds->GetIncrementalState(ii->instance, ii->i));
  ResetIncrementalStateIfRequired(trace_writer, incr_state, track_event_tls,
                                  ts);

  if (std::holds_alternative<const PerfettoTeRegisteredTrackImpl*>(track)) {
    auto* registered_track =
        std::get<const PerfettoTeRegisteredTrackImpl*>(track);
    track_uuid =
        EmitRegisteredTrack(registered_track, incr_state, trace_writer);
  } else if (std::holds_alternative<const PerfettoTeHlExtraNamedTrack*>(
                 track)) {
    auto* named_track = std::get<const PerfettoTeHlExtraNamedTrack*>(track);
    track_uuid = EmitNamedTrack(named_track->parent_uuid, named_track->name,
                                named_track->id, incr_state, trace_writer);
  } else if (std::holds_alternative<const PerfettoTeHlExtraProtoTrack*>(
                 track)) {
    auto* proto_track = std::get<const PerfettoTeHlExtraProtoTrack*>(track);
    track_uuid = EmitProtoTrack(proto_track->uuid, proto_track->fields,
                                incr_state, trace_writer);
  } else if (std::holds_alternative<const PerfettoTeHlExtraNestedTracks*>(
                 track)) {
    auto* nested = std::get<const PerfettoTeHlExtraNestedTracks*>(track);

    uint64_t uuid = 0;

    for (PerfettoTeHlNestedTrack* const* tp = nested->tracks; *tp != nullptr;
         tp++) {
      auto track_type =
          static_cast<enum PerfettoTeHlNestedTrackType>((*tp)->type);

      switch (track_type) {
        case PERFETTO_TE_HL_NESTED_TRACK_TYPE_NAMED: {
          auto* named_track =
              reinterpret_cast<PerfettoTeHlNestedTrackNamed*>(*tp);
          uuid = EmitNamedTrack(uuid, named_track->name, named_track->id,
                                incr_state, trace_writer);
        } break;
        case PERFETTO_TE_HL_NESTED_TRACK_TYPE_PROCESS: {
          uuid = perfetto_te_process_track_uuid;
        } break;
        case PERFETTO_TE_HL_NESTED_TRACK_TYPE_THREAD: {
          uuid = perfetto_te_process_track_uuid ^
                 static_cast<uint64_t>(perfetto::base::GetThreadId());
        } break;
        case PERFETTO_TE_HL_NESTED_TRACK_TYPE_PROTO: {
          auto* proto_track =
              reinterpret_cast<PerfettoTeHlNestedTrackProto*>(*tp);
          uuid = EmitProtoTrackWithParentUuid(proto_track->id ^ uuid, uuid,
                                              proto_track->fields, incr_state,
                                              trace_writer);
        } break;
        case PERFETTO_TE_HL_NESTED_TRACK_TYPE_REGISTERED: {
          auto* registered_track =
              reinterpret_cast<PerfettoTeHlNestedTrackRegistered*>(*tp);
          uuid = EmitRegisteredTrack(registered_track->track, incr_state,
                                     trace_writer);
        } break;
      }
    }
    track_uuid = uuid;
  }

  {
    auto packet = NewTracePacketInternal(
        trace_writer, incr_state, track_event_tls, ts,
        protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
    auto* track_event = packet->set_track_event();
    WriteTrackEvent(incr_state, track_event, cat, type, name, extra_data,
                    track_uuid, dynamic_cat, use_interning);
    track_event->Finalize();

    if (!incr_state->serialized_interned_data.empty()) {
      auto ranges = incr_state->serialized_interned_data.GetRanges();
      packet->AppendScatteredBytes(
          protos::pbzero::TracePacket::kInternedDataFieldNumber, ranges.data(),
          ranges.size());
      incr_state->serialized_interned_data.Reset();
    }
  }

  if (PERFETTO_UNLIKELY(flush)) {
    trace_writer->Flush();
  }
}

void TeHlEmit(struct PerfettoTeCategoryImpl* cat,
              int32_t type,
              const char* name,
              struct PerfettoTeHlExtra* const* extra_data) {
  uint32_t cached_instances =
      perfetto::shlib::TracePointTraits::GetActiveInstances({cat})->load(
          std::memory_order_relaxed);
  if (!cached_instances) {
    return;
  }

  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  perfetto::internal::DataSourceThreadLocalState*& tls_state =
      *perfetto::shlib::TrackEvent::GetTlsState();

  if (!ds->TracePrologue<perfetto::shlib::TrackEventDataSourceTraits,
                         perfetto::shlib::TracePointTraits>(
          &tls_state, &cached_instances, {cat})) {
    return;
  }

  for (perfetto::internal::DataSourceType::InstancesIterator ii =
           ds->BeginIteration<perfetto::shlib::TracePointTraits>(
               cached_instances, tls_state, {cat});
       ii.instance;
       ds->NextIteration</*Traits=*/perfetto::shlib::TracePointTraits>(
           &ii, tls_state, {cat})) {
    perfetto::shlib::InstanceOp(ds, &ii, tls_state, cat,
                                perfetto::shlib::EventType(type), name,
                                extra_data);
  }
  ds->TraceEpilogue(tls_state);
}

}  // namespace
}  // namespace perfetto::shlib

void PerfettoTeHlEmitImpl(struct PerfettoTeCategoryImpl* cat,
                          int32_t type,
                          const char* name,
                          struct PerfettoTeHlExtra* const* extra_data) {
  perfetto::shlib::TeHlEmit(cat, type, name, extra_data);
}
