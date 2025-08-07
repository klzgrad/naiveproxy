/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "perfetto/tracing/track_event_legacy.h"
#include "perfetto/ext/base/hash.h"

#include "perfetto/tracing/track.h"

namespace perfetto {
namespace legacy {

template <>
ThreadTrack ConvertThreadId(const PerfettoLegacyCurrentThreadId&) {
  // Because of the short-circuit in PERFETTO_INTERNAL_LEGACY_EVENT, we should
  // never get here.
  PERFETTO_DCHECK(false);
  return ThreadTrack::Current();
}

}  // namespace legacy

namespace internal {

void LegacyTraceId::Write(protos::pbzero::TrackEvent::LegacyEvent* event,
                          uint32_t event_flags) const {
  // Legacy flow events always use bind_id.
  if (event_flags &
      (legacy::kTraceEventFlagFlowOut | legacy::kTraceEventFlagFlowIn)) {
    // Flow bind_ids don't have scopes, so we need to mangle in-process ones to
    // avoid collisions.
    if (id_flags_ & legacy::kTraceEventFlagHasLocalId) {
      event->set_bind_id(raw_id_ ^ ProcessTrack::Current().uuid);
    } else {
      event->set_bind_id(raw_id_);
    }
    return;
  }

  uint32_t scope_flags = id_flags_ & (legacy::kTraceEventFlagHasId |
                                      legacy::kTraceEventFlagHasLocalId |
                                      legacy::kTraceEventFlagHasGlobalId);
  uint64_t id = raw_id_;
  if (scope_ && scope_flags != legacy::kTraceEventFlagHasGlobalId) {
    id = base::Hasher::Combine(id, scope_);
  }

  switch (scope_flags) {
    case legacy::kTraceEventFlagHasId:
      event->set_unscoped_id(id);
      break;
    case legacy::kTraceEventFlagHasLocalId:
      event->set_local_id(id);
      break;
    case legacy::kTraceEventFlagHasGlobalId:
      event->set_global_id(id);
      break;
  }
  if (scope_)
    event->set_id_scope(scope_);
}

}  // namespace internal
}  // namespace perfetto
