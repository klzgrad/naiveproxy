// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/heap_profiler_serialization_state.h"

namespace base {
namespace trace_event {

HeapProfilerSerializationState::HeapProfilerSerializationState()
    : heap_profiler_breakdown_threshold_bytes_(0) {}
HeapProfilerSerializationState::~HeapProfilerSerializationState() = default;

void HeapProfilerSerializationState::SetStackFrameDeduplicator(
    std::unique_ptr<StackFrameDeduplicator> stack_frame_deduplicator) {
  DCHECK(!stack_frame_deduplicator_);
  stack_frame_deduplicator_ = std::move(stack_frame_deduplicator);
}

void HeapProfilerSerializationState::SetTypeNameDeduplicator(
    std::unique_ptr<TypeNameDeduplicator> type_name_deduplicator) {
  DCHECK(!type_name_deduplicator_);
  type_name_deduplicator_ = std::move(type_name_deduplicator);
}

}  // namespace trace_event
}  // namespace base
