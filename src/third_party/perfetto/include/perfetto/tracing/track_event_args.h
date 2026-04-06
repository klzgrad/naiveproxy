/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_TRACING_TRACK_EVENT_ARGS_H_
#define INCLUDE_PERFETTO_TRACING_TRACK_EVENT_ARGS_H_

#include "perfetto/tracing/event_context.h"
#include "perfetto/tracing/track.h"

namespace perfetto {
namespace internal {

// A helper to add |flow_id| as a non-terminating flow id to TRACE_EVENT
// inline: TRACE_EVENT(..., perfetto::Flow::ProcessScoped(42));
template <class Traits>
class FlowImpl {
 public:
  // |flow_id| which is local within a given process (e.g. atomic counter xor'ed
  // with feature-specific value). This value is xor'ed with Perfetto's internal
  // process track id to attempt to ensure that it's globally-unique.
  static PERFETTO_ALWAYS_INLINE inline FlowImpl ProcessScoped(
      uint64_t flow_id) {
    return Global(flow_id ^ Track::process_uuid);
  }

  // Same as above, but combines the flow id with an extra `named_scope`'s hash.
  static PERFETTO_ALWAYS_INLINE inline FlowImpl ProcessScoped(
      uint64_t flow_id,
      const char* named_scope) {
    return Global(flow_id, named_scope);
  }

  // Same as above, but construct an id from a pointer.
  // NOTE: After the object is destroyed, the value of |ptr| can be reused for a
  // different object (in particular if the object is allocated on a stack).
  // Please ensure that you emit a trace event with the flow id of
  // perfetto::TerminatingFlow::FromPointer(this) from the destructor of the
  // object to avoid accidental conflicts.
  static PERFETTO_ALWAYS_INLINE inline FlowImpl FromPointer(const void* ptr) {
    return ProcessScoped(reinterpret_cast<uintptr_t>(ptr));
  }

  // Same as above, but combines the flow id with an extra `named_scope`'s hash.
  static PERFETTO_ALWAYS_INLINE inline FlowImpl FromPointer(
      const void* ptr,
      const char* named_scope) {
    return ProcessScoped(reinterpret_cast<uintptr_t>(ptr), named_scope);
  }

  // Add the |flow_id|. The caller is responsible for ensuring that it's
  // globally-unique (e.g. by generating a random value). This should be used
  // only for flow events which cross the process boundary (e.g. IPCs).
  static PERFETTO_ALWAYS_INLINE inline FlowImpl Global(uint64_t flow_id) {
    return FlowImpl(flow_id);
  }

  // Same as above, but combines the flow id with an extra `named_scope`'s hash.
  static PERFETTO_ALWAYS_INLINE inline FlowImpl Global(
      uint64_t flow_id,
      const char* named_scope) {
    return FlowImpl(flow_id ^ internal::Fnv1a(named_scope));
  }

  // TODO(altimin): Remove once converting a single usage in Chromium.
  explicit constexpr FlowImpl(uint64_t flow_id) : flow_id_(flow_id) {}

  void operator()(EventContext& ctx) const {
    Traits::EmitFlowId(ctx, flow_id_);
  }

 private:
  uint64_t flow_id_;
};

struct DefaultFlowTraits {
  static void EmitFlowId(EventContext& ctx, uint64_t flow_id) {
    ctx.event()->add_flow_ids(flow_id);
  }
};

struct TerminatingFlowTraits {
  static void EmitFlowId(EventContext& ctx, uint64_t flow_id) {
    ctx.event()->add_terminating_flow_ids(flow_id);
  }
};

}  // namespace internal

using Flow = internal::FlowImpl<internal::DefaultFlowTraits>;
using TerminatingFlow = internal::FlowImpl<internal::TerminatingFlowTraits>;

}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACING_TRACK_EVENT_ARGS_H_
