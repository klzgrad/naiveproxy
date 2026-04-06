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

#ifndef INCLUDE_PERFETTO_PUBLIC_ABI_TRACK_EVENT_HL_ABI_H_
#define INCLUDE_PERFETTO_PUBLIC_ABI_TRACK_EVENT_HL_ABI_H_

#include <stdbool.h>
#include <stdint.h>

#include "perfetto/public/abi/track_event_abi.h"

// High level ABI to emit track events.
//
// For each tracepoint, the user must call PerfettoTeHlEmitImpl() once and pass
// it all the required data for the event. The function will iterate all enabled
// data source instances and serialize the tracing data as protobuf messages.
//
// This tries to cover the most common cases of track event. When hitting these
// we minimize binary size at the trace-event call site, but we trade off the
// ability to serialize custom protobuf messages.

#ifdef __cplusplus
extern "C" {
#endif

// The type of the proto field.
enum PerfettoTeHlProtoFieldType {
  PERFETTO_TE_HL_PROTO_TYPE_CSTR = 0,
  PERFETTO_TE_HL_PROTO_TYPE_BYTES = 1,
  PERFETTO_TE_HL_PROTO_TYPE_NESTED = 2,
  PERFETTO_TE_HL_PROTO_TYPE_VARINT = 3,
  PERFETTO_TE_HL_PROTO_TYPE_FIXED64 = 4,
  PERFETTO_TE_HL_PROTO_TYPE_FIXED32 = 5,
  PERFETTO_TE_HL_PROTO_TYPE_DOUBLE = 6,
  PERFETTO_TE_HL_PROTO_TYPE_FLOAT = 7,
  PERFETTO_TE_HL_PROTO_TYPE_CSTR_INTERNED = 8,
};

// Common header for all the proto fields.
struct PerfettoTeHlProtoField {
  enum PerfettoTeHlProtoFieldType type;
  // Proto field id.
  uint32_t id;
};

// PERFETTO_TE_HL_PROTO_TYPE_CSTR
struct PerfettoTeHlProtoFieldCstr {
  struct PerfettoTeHlProtoField header;
  // Null terminated string.
  const char* str;
};

// PERFETTO_TE_HL_PROTO_TYPE_CSTR_INTERNED
struct PerfettoTeHlProtoFieldCstrInterned {
  struct PerfettoTeHlProtoField header;
  // Null terminated string.
  const char* str;
  // The field id of the interned data message (e.g., InternedData.event_names).
  // `str` will be interned and the resulting iid will be written in the proto
  // field `header.id`. If zero, `str` is silently dropped.
  uint32_t interned_type_id;
};

// PERFETTO_TE_HL_PROTO_TYPE_BYTES
struct PerfettoTeHlProtoFieldBytes {
  struct PerfettoTeHlProtoField header;
  const void* buf;
  size_t len;
};

// PERFETTO_TE_HL_PROTO_TYPE_NESTED
struct PerfettoTeHlProtoFieldNested {
  struct PerfettoTeHlProtoField header;
  // Array of pointers to the fields. The last pointer should be NULL.
  struct PerfettoTeHlProtoField* const* fields;
};

// PERFETTO_TE_HL_PROTO_TYPE_VARINT
struct PerfettoTeHlProtoFieldVarInt {
  struct PerfettoTeHlProtoField header;
  uint64_t value;
};

// PERFETTO_TE_HL_PROTO_TYPE_FIXED64
struct PerfettoTeHlProtoFieldFixed64 {
  struct PerfettoTeHlProtoField header;
  uint64_t value;
};

// PERFETTO_TE_HL_PROTO_TYPE_FIXED32
struct PerfettoTeHlProtoFieldFixed32 {
  struct PerfettoTeHlProtoField header;
  uint32_t value;
};

// PERFETTO_TE_HL_PROTO_TYPE_DOUBLE
struct PerfettoTeHlProtoFieldDouble {
  struct PerfettoTeHlProtoField header;
  double value;
};

// PERFETTO_TE_HL_PROTO_TYPE_FLOAT
struct PerfettoTeHlProtoFieldFloat {
  struct PerfettoTeHlProtoField header;
  float value;
};

// Union over all possible proto field types.
union PerfettoTeHlProtoFieldUnion {
  struct PerfettoTeHlProtoFieldCstr field_cstr;
  struct PerfettoTeHlProtoFieldBytes field_bytes;
  struct PerfettoTeHlProtoFieldNested field_nested;
  struct PerfettoTeHlProtoFieldVarInt field_varint;
  struct PerfettoTeHlProtoFieldFixed64 field_fixed64;
  struct PerfettoTeHlProtoFieldFixed32 field_fixed32;
  struct PerfettoTeHlProtoFieldDouble field_double;
  struct PerfettoTeHlProtoFieldFloat field_float;
  struct PerfettoTeHlProtoFieldCstrInterned field_cstr_interned;
};

// The type of an event extra parameter.
enum PerfettoTeHlExtraType {
  PERFETTO_TE_HL_EXTRA_TYPE_REGISTERED_TRACK = 1,
  PERFETTO_TE_HL_EXTRA_TYPE_NAMED_TRACK = 2,
  PERFETTO_TE_HL_EXTRA_TYPE_TIMESTAMP = 3,
  PERFETTO_TE_HL_EXTRA_TYPE_DYNAMIC_CATEGORY = 4,
  PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_INT64 = 5,
  PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_DOUBLE = 6,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_BOOL = 7,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_UINT64 = 8,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_INT64 = 9,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_DOUBLE = 10,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_STRING = 11,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_POINTER = 12,
  PERFETTO_TE_HL_EXTRA_TYPE_FLOW = 13,
  PERFETTO_TE_HL_EXTRA_TYPE_TERMINATING_FLOW = 14,
  PERFETTO_TE_HL_EXTRA_TYPE_FLUSH = 15,
  PERFETTO_TE_HL_EXTRA_TYPE_NO_INTERN = 16,
  PERFETTO_TE_HL_EXTRA_TYPE_PROTO_FIELDS = 17,
  PERFETTO_TE_HL_EXTRA_TYPE_PROTO_TRACK = 18,
  PERFETTO_TE_HL_EXTRA_TYPE_NESTED_TRACKS = 19,
};

// An extra event parameter. Each type of parameter should embed this as its
// first member.
struct PerfettoTeHlExtra {
  // enum PerfettoTeHlExtraType. Identifies the exact type of this.
  uint32_t type;
};

// PERFETTO_TE_HL_EXTRA_TYPE_REGISTERED_TRACK
struct PerfettoTeHlExtraRegisteredTrack {
  struct PerfettoTeHlExtra header;
  // Pointer to a registered track.
  const struct PerfettoTeRegisteredTrackImpl* track;
};

// PERFETTO_TE_HL_EXTRA_TYPE_NAMED_TRACK
struct PerfettoTeHlExtraNamedTrack {
  struct PerfettoTeHlExtra header;
  // The name of the track
  const char* name;
  uint64_t id;
  uint64_t parent_uuid;
};

// PERFETTO_TE_HL_EXTRA_TYPE_TIMESTAMP
struct PerfettoTeHlExtraTimestamp {
  struct PerfettoTeHlExtra header;
  // The timestamp for this event.
  struct PerfettoTeTimestamp timestamp;
};

// PERFETTO_TE_HL_EXTRA_TYPE_DYNAMIC_CATEGORY
struct PerfettoTeHlExtraDynamicCategory {
  struct PerfettoTeHlExtra header;
  // Pointer to a category descriptor. The descriptor will be evaluated against
  // the configuration. If the descriptor is considered disabled, the trace
  // point will be skipped.
  const struct PerfettoTeCategoryDescriptor* desc;
};

// PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_INT64
struct PerfettoTeHlExtraCounterInt64 {
  struct PerfettoTeHlExtra header;
  // The counter value.
  int64_t value;
};

// PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_DOUBLE
struct PerfettoTeHlExtraCounterDouble {
  struct PerfettoTeHlExtra header;
  // The counter value.
  double value;
};

// Union over all possible counter types.
union PerfettoTeHlExtraCounterUnion {
  struct PerfettoTeHlExtraCounterInt64 counter_int64;
  struct PerfettoTeHlExtraCounterDouble counter_double;
};

// PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_BOOL
struct PerfettoTeHlExtraDebugArgBool {
  struct PerfettoTeHlExtra header;
  // Pointer to the name of this debug annotation.
  const char* name;
  // The value of this debug annotation.
  bool value;
};

// PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_UINT64
struct PerfettoTeHlExtraDebugArgUint64 {
  struct PerfettoTeHlExtra header;
  // Pointer to the name of this debug annotation.
  const char* name;
  // The value of this debug annotation.
  uint64_t value;
};

// PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_INT64
struct PerfettoTeHlExtraDebugArgInt64 {
  struct PerfettoTeHlExtra header;
  // Pointer to the name of this debug annotation.
  const char* name;
  // The value of this debug annotation.
  int64_t value;
};

// PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_DOUBLE
struct PerfettoTeHlExtraDebugArgDouble {
  struct PerfettoTeHlExtra header;
  // Pointer to the name of this debug annotation.
  const char* name;
  // The value of this debug annotation.
  double value;
};

// PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_STRING
struct PerfettoTeHlExtraDebugArgString {
  struct PerfettoTeHlExtra header;
  // Pointer to the name of this debug annotation.
  const char* name;
  // The value of this debug annotation.
  const char* value;
};

// PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_POINTER
struct PerfettoTeHlExtraDebugArgPointer {
  struct PerfettoTeHlExtra header;
  // Pointer to the name of this debug annotation.
  const char* name;
  // The value of this debug annotation.
  uintptr_t value;
};

// Union over all possible debug argument types.
union PerfettoTeHlExtraDebugArgUnion {
  struct PerfettoTeHlExtraDebugArgBool arg_bool;
  struct PerfettoTeHlExtraDebugArgUint64 arg_uint64;
  struct PerfettoTeHlExtraDebugArgInt64 arg_int64;
  struct PerfettoTeHlExtraDebugArgDouble arg_double;
  struct PerfettoTeHlExtraDebugArgString arg_string;
  struct PerfettoTeHlExtraDebugArgPointer arg_pointer;
};

// PERFETTO_TE_HL_EXTRA_TYPE_FLOW
// PERFETTO_TE_HL_EXTRA_TYPE_TERMINATING_FLOW
struct PerfettoTeHlExtraFlow {
  struct PerfettoTeHlExtra header;
  // Specifies that this event starts (or terminates) a flow (i.e. a link
  // between two events) identified by this id.
  uint64_t id;
};

// PERFETTO_TE_HL_EXTRA_TYPE_PROTO_FIELDS
struct PerfettoTeHlExtraProtoFields {
  struct PerfettoTeHlExtra header;
  // Array of pointers to the fields. The last pointer should be NULL.
  struct PerfettoTeHlProtoField* const* fields;
};

// PERFETTO_TE_HL_EXTRA_TYPE_PROTO_TRACK
struct PerfettoTeHlExtraProtoTrack {
  struct PerfettoTeHlExtra header;
  uint64_t uuid;
  // Array of pointers to the fields. The last pointer should be NULL.
  struct PerfettoTeHlProtoField* const* fields;
};

// The type of a nested track
enum PerfettoTeHlNestedTrackType {
  PERFETTO_TE_HL_NESTED_TRACK_TYPE_NAMED = 1,
  PERFETTO_TE_HL_NESTED_TRACK_TYPE_PROTO = 2,
  PERFETTO_TE_HL_NESTED_TRACK_TYPE_REGISTERED = 3,
  PERFETTO_TE_HL_NESTED_TRACK_TYPE_THREAD = 4,
  PERFETTO_TE_HL_NESTED_TRACK_TYPE_PROCESS = 5,
};

struct PerfettoTeHlNestedTrack {
  // enum PerfettoTeHlNestedTrackType
  uint32_t type;
};

// PERFETTO_TE_HL_NESTED_TRACK_TYPE_NAMED
struct PerfettoTeHlNestedTrackNamed {
  struct PerfettoTeHlNestedTrack header;
  const char* name;
  // Partially identifies the track, along `name` and the parent hierarchy.
  uint64_t id;
};

struct PerfettoTeHlNestedTrackProto {
  struct PerfettoTeHlNestedTrack header;
  // Partially identifies the track, along with the parent hierarchy.
  uint64_t id;
  // Array of pointers to the fields. The last pointer should be NULL.
  struct PerfettoTeHlProtoField* const* fields;
};

struct PerfettoTeHlNestedTrackRegistered {
  struct PerfettoTeHlNestedTrack header;
  // Pointer to a registered track.
  const struct PerfettoTeRegisteredTrackImpl* track;
};

// PERFETTO_TE_HL_EXTRA_TYPE_NESTED_TRACKS
struct PerfettoTeHlExtraNestedTracks {
  struct PerfettoTeHlExtra header;
  // Array of pointers to the nested tracks. The last pointer should be NULL.
  // The first pointer is the outermost track (the parent track), the (second
  // to) last pointer is the innermost track (the child track).
  struct PerfettoTeHlNestedTrack* const* tracks;
};

// Emits an event on all active instances of the track event data source.
// * `cat`: The registered category of the event, it knows on which data source
//          instances the event should be emitted. Use
//          `perfetto_te_all_categories` for dynamic categories.
// * `type`: the event type (slice begin, slice end, ...). See `enum
//           PerfettoTeType`.
// * `name`: All events (except when PERFETTO_TE_TYPE_SLICE_END) can have an
//           associated name. It can be nullptr.
// * `extra_data`: Optional parameters associated with the events. Array of
// pointers to each event. The last pointer should be NULL.
PERFETTO_SDK_EXPORT void PerfettoTeHlEmitImpl(
    struct PerfettoTeCategoryImpl* cat,
    int32_t type,
    const char* name,
    struct PerfettoTeHlExtra* const* extra_data);

#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_PERFETTO_PUBLIC_ABI_TRACK_EVENT_HL_ABI_H_
