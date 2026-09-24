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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_STATE_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_STATE_TRACKER_H_

#include <stdint.h>
#include <cstdint>
#include <functional>
#include <optional>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/state_tables_py.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

class StateTracker {
 public:
  using SetArgsCallback = std::function<void(ArgsTracker::BoundInserter*)>;

  explicit StateTracker(TraceProcessorContext*);
  virtual ~StateTracker();

  // Updates the state track with a new state value and returns the row ID.
  // A null string value (kNullStringId) closes the active state track.
  virtual std::optional<tables::StateTable::Id> UpdateState(
      int64_t timestamp,
      TrackId track_id,
      StringId value_id,
      StringId category_id = kNullStringId,
      SetArgsCallback args_callback = SetArgsCallback());

 private:
  struct ActiveState {
    ActiveState(tables::StateTable::RowNumber r, StringId v)
        : row(r), value(v) {}

    tables::StateTable::RowNumber row;
    StringId value;
    // Args for this state's row, committed when the ActiveState is destroyed.
    std::optional<ArgsInserter> args_inserter;
  };

  TraceProcessorContext* const context_;
  base::FlatHashMap<TrackId, ActiveState> active_states_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_STATE_TRACKER_H_
