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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_SPARSE_COUNTER_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_SPARSE_COUNTER_TRACKER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

struct alignas(8) SparseCounterEvent {
  TrackId track;
  double value;
};

// SparseCounterTracker helps efficiently write counter events into a trace by
// omitting values not necessary to recreate the same track. Consider this:
//
// A     B     C     D     E
// ^-----^-----^-----^-----^
// 1     2     2     2     3
//
// It is possible to fully represent this sequence by ommitting the point `C`.
// While the value is unchanged at point `D`, if we don't emit the point `D`,
// then it would appear as though the value increase by 1 between `B` and `E`.
//
// The SparseCounterTracker is meant to be used during the tokenization phase.
// It needs to be able to write the last value (`D` in the example above) into
// the sorter to properly sequence events.
class SparseCounterTracker {
 public:
  explicit SparseCounterTracker(TraceProcessorContext* context);
  ~SparseCounterTracker();

  // Updates the value of the track at a certain time. Time must monotonically
  // increase per track (be >= to the last time).
  void PushCounter(int64_t ts, TrackId track, double value);

 private:
  struct TrackState {
    double last_value = 0;
    int64_t last_time = 0;
    bool written = false;
  };

  std::unique_ptr<TraceSorter::Stream<SparseCounterEvent>> stream_;
  base::FlatHashMap<TrackId, TrackState> track_state_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_SPARSE_COUNTER_TRACKER_H_
