/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_redaction/process_thread_timeline.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "perfetto/base/logging.h"

namespace perfetto::trace_redaction {
namespace {
// Limit the number of iterations to avoid an infinite loop. 10 is a generous
// number of iterations.
constexpr size_t kMaxSearchDepth = 10;

bool OrderByPid(const ProcessThreadTimeline::Event& left,
                const ProcessThreadTimeline::Event& right) {
  return left.pid < right.pid;
}

}  // namespace

void ProcessThreadTimeline::Append(const Event& event) {
  events_.push_back(event);
  mode_ = Mode::kWrite;
}

void ProcessThreadTimeline::Sort() {
  std::sort(events_.begin(), events_.end(), OrderByPid);
  mode_ = Mode::kRead;
}

const ProcessThreadTimeline::Event* ProcessThreadTimeline::GetOpeningEvent(
    uint64_t ts,
    int32_t pid) const {
  PERFETTO_DCHECK(mode_ == Mode::kRead);

  auto prev_open = QueryLeftMax(ts, pid, Event::Type::kOpen);
  auto prev_close = QueryLeftMax(ts, pid, Event::Type::kClose);

  // If there is no open event before ts, it means pid never started.
  if (!prev_open) {
    return nullptr;
  }

  // There is a close event that is strictly between the open event and ts, then
  // the pid is considered free.
  //
  //    |---------|  ^  : pid is free
  // ^  |---------|  ^  : pid is free
  //    ^---------|     : pid is active
  //    |---------^     : pid is active
  //    |----^----|     : pid is active

  // Both open and close are less than or equal to ts (QueryLeftMax).
  uint64_t close = prev_close ? prev_close->ts : 0;
  uint64_t open = prev_open->ts;

  return close > open && close < ts ? nullptr : prev_open;
}

bool ProcessThreadTimeline::PidConnectsToUid(uint64_t ts,
                                             int32_t pid,
                                             uint64_t uid) const {
  PERFETTO_DCHECK(mode_ == Mode::kRead);

  const auto* prev_open = QueryLeftMax(ts, pid, Event::Type::kOpen);
  const auto* prev_close = QueryLeftMax(ts, pid, Event::Type::kClose);

  for (size_t d = 0; d < kMaxSearchDepth; ++d) {
    // If there's no previous open event, it means this pid was never created.
    // This should not happen.
    if (!prev_open) {
      return false;
    }

    // This get tricky here. If done wrong, proc_free events will fail because
    // they'll report as disconnected when they could be connected to the
    // package. Inclusive bounds are used here. In context, if a task_newtask
    // event happens at time T, the pid exists at time T. If a proc_free event
    // happens at time T, the pid is "shutting down" at time T but still exists.
    //
    //    B         E     : B = begin
    //    .         .       E = end
    //    .         .
    //    |---------|  ^  : pid is free
    // ^  |---------|     : pid is free
    //    ^---------|     : pid is active
    //    |---------^     : pid is active
    //    |----^----|     : pid is active

    // By definition, both open and close are less than or equal to ts
    // (QueryLeftMax), so problem space is reduces.
    auto close = prev_close ? prev_close->ts : 0;
    auto open = prev_open->ts;

    if (close > open && close < ts) {
      return false;  // Close is sitting between open and ts.
    }

    // TODO(vaage): Normalize the uid values.
    if (prev_open->uid == uid) {
      return true;
    }

    if (prev_open->ppid == Event::kUnknownPid) {
      return false;  // If there is no parent, there is no way to keep
                     // searching.
    }

    auto ppid = prev_open->ppid;

    prev_open = QueryLeftMax(ts, ppid, Event::Type::kOpen);
    prev_close = QueryLeftMax(ts, ppid, Event::Type::kClose);
  }

  return false;
}

const ProcessThreadTimeline::Event* ProcessThreadTimeline::QueryLeftMax(
    uint64_t ts,
    int32_t pid,
    Event::Type type) const {
  auto fake = Event::Close(0, pid);

  // Events are sorted by pid, creating islands of data. This search is to put
  // the cursor at the start of pid's island. Each island will be small (a
  // couple of items), so searching within the islands should be cheap.
  auto it = std::lower_bound(events_.begin(), events_.end(), fake, OrderByPid);
  auto end = std::upper_bound(events_.begin(), events_.end(), fake, OrderByPid);

  const Event* best = nullptr;

  for (; it != end; ++it) {
    bool replace = false;

    if (it->type == type && it->ts <= ts) {
      replace = !best || it->ts > best->ts;
    }

    if (replace) {
      best = &(*it);
    }
  }

  return best;
}

}  // namespace perfetto::trace_redaction
