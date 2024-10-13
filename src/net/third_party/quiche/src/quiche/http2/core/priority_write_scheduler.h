// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_CORE_PRIORITY_WRITE_SCHEDULER_H_
#define QUICHE_HTTP2_CORE_PRIORITY_WRITE_SCHEDULER_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "quiche/http2/core/spdy_protocol.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_circular_deque.h"

namespace http2 {

namespace test {
template <typename StreamIdType>
class PriorityWriteSchedulerPeer;
}

// SpdyPriority is an integer type, so this functor can be used both as
// PriorityTypeToInt and as IntToPriorityType.
struct QUICHE_EXPORT SpdyPriorityToSpdyPriority {
  spdy::SpdyPriority operator()(spdy::SpdyPriority priority) {
    return priority;
  }
};

// PriorityWriteScheduler manages the order in which HTTP/2 or HTTP/3 streams
// are written. Each stream has a priority of type PriorityType. This includes
// an integer between 0 and 7, and optionally other information that is stored
// but otherwise ignored by this class.  Higher priority (lower integer value)
// streams are always given precedence over lower priority (higher value)
// streams, as long as the higher priority stream is not blocked.
//
// Each stream can be in one of two states: ready or not ready (for writing).
// Ready state is changed by calling the MarkStreamReady() and
// MarkStreamNotReady() methods. Only streams in the ready state can be returned
// by PopNextReadyStream(). When returned by that method, the stream's state
// changes to not ready.
//
template <typename StreamIdType, typename PriorityType = spdy::SpdyPriority,
          typename PriorityTypeToInt = SpdyPriorityToSpdyPriority,
          typename IntToPriorityType = SpdyPriorityToSpdyPriority>
class QUICHE_EXPORT PriorityWriteScheduler {
 public:
  static constexpr int kHighestPriority = 0;
  static constexpr int kLowestPriority = 7;

  static_assert(spdy::kV3HighestPriority == kHighestPriority);
  static_assert(spdy::kV3LowestPriority == kLowestPriority);

  // Registers new stream `stream_id` with the scheduler, assigning it the
  // given priority.
  //
  // Preconditions: `stream_id` should be unregistered.
  void RegisterStream(StreamIdType stream_id, PriorityType priority) {
    auto stream_info = std::make_unique<StreamInfo>(
        StreamInfo{std::move(priority), stream_id, false});
    bool inserted =
        stream_infos_.insert(std::make_pair(stream_id, std::move(stream_info)))
            .second;
    QUICHE_BUG_IF(spdy_bug_19_2, !inserted)
        << "Stream " << stream_id << " already registered";
  }

  // Unregisters the given stream from the scheduler, which will no longer keep
  // state for it.
  //
  // Preconditions: `stream_id` should be registered.
  void UnregisterStream(StreamIdType stream_id) {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_3) << "Stream " << stream_id << " not registered";
      return;
    }
    const StreamInfo* const stream_info = it->second.get();
    if (stream_info->ready) {
      bool erased =
          Erase(&priority_infos_[PriorityTypeToInt()(stream_info->priority)]
                     .ready_list,
                stream_info);
      QUICHE_DCHECK(erased);
    }
    stream_infos_.erase(it);
  }

  // Returns true if the given stream is currently registered.
  bool StreamRegistered(StreamIdType stream_id) const {
    return stream_infos_.find(stream_id) != stream_infos_.end();
  }

  // Returns the priority of the specified stream.
  //
  // Preconditions: `stream_id` should be registered.
  PriorityType GetStreamPriority(StreamIdType stream_id) const {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_DVLOG(1) << "Stream " << stream_id << " not registered";
      return IntToPriorityType()(kLowestPriority);
    }
    return it->second->priority;
  }

  // Updates the priority of the given stream.
  //
  // Preconditions: `stream_id` should be registered.
  void UpdateStreamPriority(StreamIdType stream_id, PriorityType priority) {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      // TODO(mpw): add to stream_infos_ on demand--see b/15676312.
      QUICHE_DVLOG(1) << "Stream " << stream_id << " not registered";
      return;
    }

    StreamInfo* const stream_info = it->second.get();
    if (stream_info->priority == priority) {
      return;
    }

    // Only move `stream_info` to a different bucket if the integral priority
    // value changes.
    if (PriorityTypeToInt()(stream_info->priority) !=
            PriorityTypeToInt()(priority) &&
        stream_info->ready) {
      bool erased =
          Erase(&priority_infos_[PriorityTypeToInt()(stream_info->priority)]
                     .ready_list,
                stream_info);
      QUICHE_DCHECK(erased);
      priority_infos_[PriorityTypeToInt()(priority)].ready_list.push_back(
          stream_info);
      ++num_ready_streams_;
    }

    // But override `priority` for the stream regardless of the integral value,
    // because it might contain additional information.
    stream_info->priority = std::move(priority);
  }

  // Records time of a read/write event for the given stream.
  //
  // Preconditions: `stream_id` should be registered.
  void RecordStreamEventTime(StreamIdType stream_id, absl::Time now) {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_4) << "Stream " << stream_id << " not registered";
      return;
    }
    PriorityInfo& priority_info =
        priority_infos_[PriorityTypeToInt()(it->second->priority)];
    priority_info.last_event_time =
        std::max(priority_info.last_event_time, absl::make_optional(now));
  }

  // Returns time of the last read/write event for a stream with higher priority
  // than the priority of the given stream, or nullopt if there is no such
  // event.
  //
  // Preconditions: `stream_id` should be registered.
  std::optional<absl::Time> GetLatestEventWithPriority(
      StreamIdType stream_id) const {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_5) << "Stream " << stream_id << " not registered";
      return std::nullopt;
    }
    std::optional<absl::Time> last_event_time;
    const StreamInfo* const stream_info = it->second.get();
    for (int p = kHighestPriority;
         p < PriorityTypeToInt()(stream_info->priority); ++p) {
      last_event_time =
          std::max(last_event_time, priority_infos_[p].last_event_time);
    }
    return last_event_time;
  }

  // If the scheduler has any ready streams, returns the next scheduled
  // ready stream, in the process transitioning the stream from ready to not
  // ready.
  //
  // Preconditions: `HasReadyStreams() == true`
  StreamIdType PopNextReadyStream() {
    return std::get<0>(PopNextReadyStreamAndPriority());
  }

  // If the scheduler has any ready streams, returns the next scheduled
  // ready stream and its priority, in the process transitioning the stream from
  // ready to not ready.
  //
  // Preconditions: `HasReadyStreams() == true`
  std::tuple<StreamIdType, PriorityType> PopNextReadyStreamAndPriority() {
    for (int p = kHighestPriority; p <= kLowestPriority; ++p) {
      ReadyList& ready_list = priority_infos_[p].ready_list;
      if (!ready_list.empty()) {
        StreamInfo* const info = ready_list.front();
        ready_list.pop_front();
        --num_ready_streams_;

        QUICHE_DCHECK(stream_infos_.find(info->stream_id) !=
                      stream_infos_.end());
        info->ready = false;
        return std::make_tuple(info->stream_id, info->priority);
      }
    }
    QUICHE_BUG(spdy_bug_19_6) << "No ready streams available";
    return std::make_tuple(0, IntToPriorityType()(kLowestPriority));
  }

  // Returns true if there's another stream ahead of the given stream in the
  // scheduling queue.  This function can be called to see if the given stream
  // should yield work to another stream.
  //
  // Preconditions: `stream_id` should be registered.
  bool ShouldYield(StreamIdType stream_id) const {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_7) << "Stream " << stream_id << " not registered";
      return false;
    }

    // If there's a higher priority stream, this stream should yield.
    const StreamInfo* const stream_info = it->second.get();
    for (int p = kHighestPriority;
         p < PriorityTypeToInt()(stream_info->priority); ++p) {
      if (!priority_infos_[p].ready_list.empty()) {
        return true;
      }
    }

    // If this priority level is empty, or this stream is the next up, there's
    // no need to yield.
    const auto& ready_list =
        priority_infos_[PriorityTypeToInt()(it->second->priority)].ready_list;
    if (ready_list.empty() || ready_list.front()->stream_id == stream_id) {
      return false;
    }

    // There are other streams in this priority level which take precedence.
    // Yield.
    return true;
  }

  // Marks the stream as ready to write. If the stream was already ready, does
  // nothing. If add_to_front is true, the stream is scheduled ahead of other
  // streams of the same priority/weight, otherwise it is scheduled behind them.
  //
  // Preconditions: `stream_id` should be registered.
  void MarkStreamReady(StreamIdType stream_id, bool add_to_front) {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_8) << "Stream " << stream_id << " not registered";
      return;
    }
    StreamInfo* const stream_info = it->second.get();
    if (stream_info->ready) {
      return;
    }
    ReadyList& ready_list =
        priority_infos_[PriorityTypeToInt()(stream_info->priority)].ready_list;
    if (add_to_front) {
      ready_list.push_front(stream_info);
    } else {
      ready_list.push_back(stream_info);
    }
    ++num_ready_streams_;
    stream_info->ready = true;
  }

  // Marks the stream as not ready to write. If the stream is not registered or
  // not ready, does nothing.
  //
  // Preconditions: `stream_id` should be registered.
  void MarkStreamNotReady(StreamIdType stream_id) {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_9) << "Stream " << stream_id << " not registered";
      return;
    }
    StreamInfo* const stream_info = it->second.get();
    if (!stream_info->ready) {
      return;
    }
    bool erased = Erase(
        &priority_infos_[PriorityTypeToInt()(stream_info->priority)].ready_list,
        stream_info);
    QUICHE_DCHECK(erased);
    stream_info->ready = false;
  }

  // Returns true iff the scheduler has any ready streams.
  bool HasReadyStreams() const { return num_ready_streams_ > 0; }

  // Returns the number of streams currently marked ready.
  size_t NumReadyStreams() const { return num_ready_streams_; }

  // Returns the number of registered streams.
  size_t NumRegisteredStreams() const { return stream_infos_.size(); }

  // Returns summary of internal state, for logging/debugging.
  std::string DebugString() const {
    return absl::StrCat(
        "PriorityWriteScheduler {num_streams=", stream_infos_.size(),
        " num_ready_streams=", NumReadyStreams(), "}");
  }

  // Returns true if stream with `stream_id` is ready.
  bool IsStreamReady(StreamIdType stream_id) const {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_DLOG(INFO) << "Stream " << stream_id << " not registered";
      return false;
    }
    return it->second->ready;
  }

 private:
  friend class test::PriorityWriteSchedulerPeer<StreamIdType>;

  // State kept for all registered streams.
  // All ready streams have `ready == true` and should be present in
  // `priority_infos_[priority].ready_list`.
  struct QUICHE_EXPORT StreamInfo {
    PriorityType priority;
    StreamIdType stream_id;
    bool ready;
  };

  // O(1) size lookup, O(1) insert at front or back (amortized).
  using ReadyList = quiche::QuicheCircularDeque<StreamInfo*>;

  // State kept for each priority level.
  struct QUICHE_EXPORT PriorityInfo {
    // IDs of streams that are ready to write.
    ReadyList ready_list;
    // Time of latest write event for stream of this priority.
    std::optional<absl::Time> last_event_time;
  };

  // Use std::unique_ptr, because absl::flat_hash_map does not have pointer
  // stability, but ReadyList stores pointers to the StreamInfo objects.
  using StreamInfoMap =
      absl::flat_hash_map<StreamIdType, std::unique_ptr<StreamInfo>>;

  // Erases `info` from `ready_list`, returning true if found (and erased), or
  // false otherwise. Decrements `num_ready_streams_` if an entry is erased.
  bool Erase(ReadyList* ready_list, const StreamInfo* info) {
    auto it = std::remove(ready_list->begin(), ready_list->end(), info);
    if (it == ready_list->end()) {
      // `info` was not found.
      return false;
    }
    ready_list->pop_back();
    --num_ready_streams_;
    return true;
  }

  // Number of ready streams.
  size_t num_ready_streams_ = 0;
  // Per-priority state, including ready lists.
  PriorityInfo priority_infos_[kLowestPriority + 1];
  // StreamInfos for all registered streams.
  StreamInfoMap stream_infos_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_CORE_PRIORITY_WRITE_SCHEDULER_H_
