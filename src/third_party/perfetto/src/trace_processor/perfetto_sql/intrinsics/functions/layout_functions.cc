// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/trace_processor/perfetto_sql/intrinsics/functions/layout_functions.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_window_function.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

namespace {

constexpr char kFunctionName[] = "INTERNAL_LAYOUT";

// A helper class for tracking which depths are available at a given time
// and which slices are occupying each depths.
class SlicePacker {
 public:
  SlicePacker() = default;

  // |dur| can be 0 for instant events and -1 for slices which do not end.
  base::Status AddSlice(int64_t ts, int64_t dur) {
    if (last_call_ == LastCall::kAddSlice) {
      return base::ErrStatus(R"(
Incorrect window clause (observed two consecutive calls to "step" function).
The window clause should be "rows between unbounded preceding and current row".
)");
    }
    last_call_ = LastCall::kAddSlice;
    if (ts < last_seen_ts_) {
      return base::ErrStatus(R"(
Passed slices are in incorrect order: %s requires timestamps to be sorted.
Please specify "ORDER BY ts" in the window clause.
)",
                             kFunctionName);
    }
    last_seen_ts_ = ts;
    ProcessPrecedingEvents(ts);
    // If the event is instant, do not mark this depth as occupied as it
    // becomes immediately available again.
    bool is_busy = dur != 0;
    size_t depth = SelectAvailableDepth(is_busy);
    // If the slice has an end and is not an instant, schedule this depth
    // to be marked available again when it ends.
    if (dur != 0) {
      int64_t ts_end =
          dur == -1 ? std::numeric_limits<int64_t>::max() : ts + dur;
      slice_ends_.push({ts_end, depth});
    }
    last_depth_ = depth;
    return base::OkStatus();
  }

  size_t GetLastDepth() {
    last_call_ = LastCall::kQuery;
    return last_depth_;
  }

 private:
  struct SliceEnd {
    int64_t ts;
    size_t depth;
  };

  struct SliceEndGreater {
    bool operator()(const SliceEnd& lhs, const SliceEnd& rhs) {
      return lhs.ts > rhs.ts;
    }
  };

  void ProcessPrecedingEvents(int64_t ts) {
    while (!slice_ends_.empty() && slice_ends_.top().ts <= ts) {
      is_depth_busy_[slice_ends_.top().depth] = false;
      slice_ends_.pop();
    }
  }

  size_t SelectAvailableDepth(bool new_state) {
    for (size_t i = 0; i < is_depth_busy_.size(); ++i) {
      if (!is_depth_busy_[i]) {
        is_depth_busy_[i] = new_state;
        return i;
      }
    }
    size_t depth = is_depth_busy_.size();
    is_depth_busy_.push_back(new_state);
    return depth;
  }

  enum class LastCall {
    kAddSlice,
    kQuery,
  };
  // The first call will be "add slice" and the calls are expected to
  // interleave, so set initial value to "query".
  LastCall last_call_ = LastCall::kQuery;

  int64_t last_seen_ts_ = 0;
  std::vector<bool> is_depth_busy_;
  // A list of currently open slices, ordered by end timestamp (ascending).
  std::priority_queue<SliceEnd, std::vector<SliceEnd>, SliceEndGreater>
      slice_ends_;
  size_t last_depth_ = 0;
};

base::StatusOr<SlicePacker*> GetOrCreateAggregationContext(
    sqlite3_context* ctx) {
  auto** packer = static_cast<SlicePacker**>(
      sqlite3_aggregate_context(ctx, sizeof(SlicePacker*)));
  if (!packer) {
    return base::ErrStatus("Failed to allocate aggregate context");
  }

  if (!*packer) {
    *packer = new SlicePacker();
  }
  return *packer;
}

base::Status StepStatus(sqlite3_context* ctx,
                        size_t argc,
                        sqlite3_value** argv) {
  base::StatusOr<SlicePacker*> slice_packer =
      GetOrCreateAggregationContext(ctx);
  RETURN_IF_ERROR(slice_packer.status());

  ASSIGN_OR_RETURN(SqlValue ts, sqlite::utils::ExtractArgument(
                                    argc, argv, "ts", 0, SqlValue::kLong));
  if (ts.AsLong() < 0) {
    return base::ErrStatus("ts cannot be negative.");
  }

  ASSIGN_OR_RETURN(SqlValue dur, sqlite::utils::ExtractArgument(
                                     argc, argv, "dur", 1, SqlValue::kLong));
  if (dur.AsLong() < -1) {
    return base::ErrStatus("dur cannot be < -1.");
  }

  return slice_packer.value()->AddSlice(ts.AsLong(), dur.AsLong());
}

struct InternalLayout : public sqlite::WindowFunction {
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_CHECK(argc >= 0);

    base::Status status = StepStatus(ctx, static_cast<size_t>(argc), argv);
    if (!status.ok()) {
      return sqlite::utils::SetError(ctx, kFunctionName, status);
    }
  }

  static void Inverse(sqlite3_context* ctx, int, sqlite3_value**) {
    sqlite::utils::SetError(ctx, kFunctionName, base::ErrStatus(R"(
The inverse step is not supported: the window clause should be
"BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW".
)"));
  }

  static void Value(sqlite3_context* ctx) {
    base::StatusOr<SlicePacker*> slice_packer =
        GetOrCreateAggregationContext(ctx);
    if (!slice_packer.ok()) {
      return sqlite::utils::SetError(ctx, kFunctionName, slice_packer.status());
    }
    return sqlite::result::Long(
        ctx, static_cast<int64_t>(slice_packer.value()->GetLastDepth()));
  }

  static void Final(sqlite3_context* ctx) {
    auto** slice_packer = static_cast<SlicePacker**>(
        sqlite3_aggregate_context(ctx, sizeof(SlicePacker*)));
    if (!slice_packer || !*slice_packer) {
      return;
    }
    sqlite::result::Long(ctx,
                         static_cast<int64_t>((*slice_packer)->GetLastDepth()));
    delete *slice_packer;
  }
};

}  // namespace

base::Status RegisterLayoutFunctions(PerfettoSqlEngine& engine) {
  return engine.RegisterWindowFunction<InternalLayout>(kFunctionName, 2,
                                                       nullptr);
}

}  // namespace perfetto::trace_processor
