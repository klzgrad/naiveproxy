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

#include "src/trace_processor/importers/common/sparse_counter_tracker.h"

#include <functional>

#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {
namespace {

class SparseCounterSink
    : public TraceSorter::Sink<SparseCounterEvent, SparseCounterSink> {
 public:
  explicit SparseCounterSink(TraceProcessorContext* context)
      : context_(context) {}
  ~SparseCounterSink() override;
  void Parse(int64_t ts, SparseCounterEvent event) {
    context_->event_tracker->PushCounter(ts, event.value, event.track);
  }

 private:
  TraceProcessorContext* context_;
};

SparseCounterSink::~SparseCounterSink() = default;

}  // namespace

SparseCounterTracker::SparseCounterTracker(TraceProcessorContext* context)
    : stream_(context->sorter->CreateStream(
          std::make_unique<SparseCounterSink>(context))) {}

SparseCounterTracker::~SparseCounterTracker() = default;

void SparseCounterTracker::PushCounter(int64_t ts,
                                       TrackId track,
                                       double value) {
  auto [state, inserted] = track_state_.Insert(track, TrackState{});
  bool different = !std::equal_to<double>()(state->last_value, value);

  // If the value changed, write the previous value if it wasn't written.
  if (different && !inserted && !state->written) {
    stream_->Push(state->last_time,
                  SparseCounterEvent{track, state->last_value});
  }

  state->written = false;
  if (inserted || different) {
    stream_->Push(ts, SparseCounterEvent{track, value});
    state->written = true;
  }

  state->last_value = value;
  state->last_time = ts;
}

}  // namespace perfetto::trace_processor
