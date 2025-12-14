/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_CLOCK_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_CLOCK_TRACKER_H_

#include "src/trace_processor/util/clock_synchronizer.h"

#include "src/trace_processor/importers/common/metadata_tracker.h"

namespace perfetto::trace_processor {

class ClockTrackerTest;
class TraceProcessorContext;

class ClockSynchronizerListenerImpl {
 private:
  TraceProcessorContext* context_;

 public:
  explicit ClockSynchronizerListenerImpl(TraceProcessorContext* context);

  base::Status OnClockSyncCacheMiss();

  base::Status OnInvalidClockSnapshot();

  base::Status OnTraceTimeClockIdChanged(
      ClockSynchronizer<ClockSynchronizerListenerImpl>::ClockId
          trace_time_clock_id);

  base::Status OnSetTraceTimeClock(
      ClockSynchronizer<ClockSynchronizerListenerImpl>::ClockId
          trace_time_clock_id);

  // Returns true if this is a local host, false otherwise.
  bool IsLocalHost();
};

using ClockTracker = ClockSynchronizer<ClockSynchronizerListenerImpl>;

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_CLOCK_TRACKER_H_
