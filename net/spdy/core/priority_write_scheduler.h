// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CORE_PRIORITY_WRITE_SCHEDULER_H_
#define NET_SPDY_CORE_PRIORITY_WRITE_SCHEDULER_H_

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/logging.h"
#include "net/spdy/core/spdy_bug_tracker.h"
#include "net/spdy/core/spdy_protocol.h"
#include "net/spdy/core/write_scheduler.h"

namespace net {

namespace test {
template <typename StreamIdType>
class PriorityWriteSchedulerPeer;
}

// WriteScheduler implementation that manages the order in which streams are
// written using the SPDY priority scheme described at:
// https://www.chromium.org/spdy/spdy-protocol/spdy-protocol-draft3-1#TOC-2.3.3-Stream-priority
//
// Internally, PriorityWriteScheduler consists of 8 PriorityInfo objects, one
// for each priority value.  Each PriorityInfo contains a list of streams of
// that priority that are ready to write, as well as a timestamp of the last
// I/O event that occurred for a stream of that priority.
//
// DO NOT USE. Deprecated.
template <typename StreamIdType>
class PriorityWriteScheduler : public WriteScheduler<StreamIdType> {
 public:
  using typename WriteScheduler<StreamIdType>::StreamPrecedenceType;

  // Creates scheduler with no streams.
  PriorityWriteScheduler() = default;

  void RegisterStream(StreamIdType stream_id,
                      const StreamPrecedenceType& precedence) override {
    SPDY_BUG_IF(!precedence.is_spdy3_priority()) << "Expected SPDY priority";

    // parent_id not used here, but may as well validate it.  However,
    // parent_id may legitimately not be registered yet--see b/15676312.
    StreamIdType parent_id = precedence.parent_id();
    DVLOG_IF(1, parent_id != kHttp2RootStreamId && !StreamRegistered(parent_id))
        << "Parent stream " << parent_id << " not registered";

    if (stream_id == kHttp2RootStreamId) {
      SPDY_BUG << "Stream " << kHttp2RootStreamId << " already registered";
      return;
    }
    StreamInfo stream_info = {precedence.spdy3_priority(), stream_id, false};
    bool inserted =
        stream_infos_.insert(std::make_pair(stream_id, stream_info)).second;
    SPDY_BUG_IF(!inserted) << "Stream " << stream_id << " already registered";
  }

  void UnregisterStream(StreamIdType stream_id) override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      SPDY_BUG << "Stream " << stream_id << " not registered";
      return;
    }
    StreamInfo& stream_info = it->second;
    if (stream_info.ready) {
      bool erased =
          Erase(&priority_infos_[stream_info.priority].ready_list, stream_info);
      DCHECK(erased);
    }
    stream_infos_.erase(it);
  }

  bool StreamRegistered(StreamIdType stream_id) const override {
    return stream_infos_.find(stream_id) != stream_infos_.end();
  }

  StreamPrecedenceType GetStreamPrecedence(
      StreamIdType stream_id) const override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      DVLOG(1) << "Stream " << stream_id << " not registered";
      return StreamPrecedenceType(kV3LowestPriority);
    }
    return StreamPrecedenceType(it->second.priority);
  }

  void UpdateStreamPrecedence(StreamIdType stream_id,
                              const StreamPrecedenceType& precedence) override {
    SPDY_BUG_IF(!precedence.is_spdy3_priority()) << "Expected SPDY priority";

    // parent_id not used here, but may as well validate it.  However,
    // parent_id may legitimately not be registered yet--see b/15676312.
    StreamIdType parent_id = precedence.parent_id();
    DVLOG_IF(1, parent_id != kHttp2RootStreamId && !StreamRegistered(parent_id))
        << "Parent stream " << parent_id << " not registered";

    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      // TODO(mpw): add to stream_infos_ on demand--see b/15676312.
      DVLOG(1) << "Stream " << stream_id << " not registered";
      return;
    }
    StreamInfo& stream_info = it->second;
    SpdyPriority new_priority = precedence.spdy3_priority();
    if (stream_info.priority == new_priority) {
      return;
    }
    if (stream_info.ready) {
      bool erased =
          Erase(&priority_infos_[stream_info.priority].ready_list, stream_info);
      DCHECK(erased);
      priority_infos_[new_priority].ready_list.push_back(&stream_info);
      ++num_ready_streams_;
    }
    stream_info.priority = new_priority;
  }

  std::vector<StreamIdType> GetStreamChildren(
      StreamIdType stream_id) const override {
    return std::vector<StreamIdType>();
  }

  void RecordStreamEventTime(StreamIdType stream_id,
                             int64_t now_in_usec) override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      SPDY_BUG << "Stream " << stream_id << " not registered";
      return;
    }
    PriorityInfo& priority_info = priority_infos_[it->second.priority];
    priority_info.last_event_time_usec =
        std::max(priority_info.last_event_time_usec, now_in_usec);
  }

  int64_t GetLatestEventWithPrecedence(StreamIdType stream_id) const override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      SPDY_BUG << "Stream " << stream_id << " not registered";
      return 0;
    }
    int64_t last_event_time_usec = 0;
    const StreamInfo& stream_info = it->second;
    for (SpdyPriority p = kV3HighestPriority; p < stream_info.priority; ++p) {
      last_event_time_usec = std::max(last_event_time_usec,
                                      priority_infos_[p].last_event_time_usec);
    }
    return last_event_time_usec;
  }

  StreamIdType PopNextReadyStream() override {
    return std::get<0>(PopNextReadyStreamAndPrecedence());
  }

  // Returns the next ready stream and its precedence.
  std::tuple<StreamIdType, StreamPrecedenceType>
  PopNextReadyStreamAndPrecedence() override {
    for (SpdyPriority p = kV3HighestPriority; p <= kV3LowestPriority; ++p) {
      ReadyList& ready_list = priority_infos_[p].ready_list;
      if (!ready_list.empty()) {
        StreamInfo* info = ready_list.front();
        ready_list.pop_front();
        --num_ready_streams_;

        DCHECK(stream_infos_.find(info->stream_id) != stream_infos_.end());
        info->ready = false;
        return std::make_tuple(info->stream_id,
                               StreamPrecedenceType(info->priority));
      }
    }
    SPDY_BUG << "No ready streams available";
    return std::make_tuple(0, StreamPrecedenceType(kV3LowestPriority));
  }

  bool ShouldYield(StreamIdType stream_id) const override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      SPDY_BUG << "Stream " << stream_id << " not registered";
      return false;
    }

    // If there's a higher priority stream, this stream should yield.
    const StreamInfo& stream_info = it->second;
    for (SpdyPriority p = kV3HighestPriority; p < stream_info.priority; ++p) {
      if (!priority_infos_[p].ready_list.empty()) {
        return true;
      }
    }

    // If this priority level is empty, or this stream is the next up, there's
    // no need to yield.
    const auto& ready_list = priority_infos_[it->second.priority].ready_list;
    if (ready_list.empty() || ready_list.front()->stream_id == stream_id) {
      return false;
    }

    // There are other streams in this priority level which take precedence.
    // Yield.
    return true;
  }

  void MarkStreamReady(StreamIdType stream_id, bool add_to_front) override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      SPDY_BUG << "Stream " << stream_id << " not registered";
      return;
    }
    StreamInfo& stream_info = it->second;
    if (stream_info.ready) {
      return;
    }
    ReadyList& ready_list = priority_infos_[stream_info.priority].ready_list;
    if (add_to_front) {
      ready_list.push_front(&stream_info);
    } else {
      ready_list.push_back(&stream_info);
    }
    ++num_ready_streams_;
    stream_info.ready = true;
  }

  void MarkStreamNotReady(StreamIdType stream_id) override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      SPDY_BUG << "Stream " << stream_id << " not registered";
      return;
    }
    StreamInfo& stream_info = it->second;
    if (!stream_info.ready) {
      return;
    }
    bool erased =
        Erase(&priority_infos_[stream_info.priority].ready_list, stream_info);
    DCHECK(erased);
    stream_info.ready = false;
  }

  // Returns true iff the number of ready streams is non-zero.
  bool HasReadyStreams() const override { return num_ready_streams_ > 0; }

  // Returns the number of ready streams.
  size_t NumReadyStreams() const override { return num_ready_streams_; }

 private:
  friend class test::PriorityWriteSchedulerPeer<StreamIdType>;

  // State kept for all registered streams. All ready streams have ready = true
  // and should be present in priority_infos_[priority].ready_list.
  struct StreamInfo {
    SpdyPriority priority;
    StreamIdType stream_id;
    bool ready;
  };

  // 0(1) size lookup, Amortized 0(1) insert at front or back.
  using ReadyList = base::circular_deque<StreamInfo*>;

  // State kept for each priority level.
  struct PriorityInfo {
    // IDs of streams that are ready to write.
    ReadyList ready_list;
    // Time of latest write event for stream of this priority, in microseconds.
    int64_t last_event_time_usec = 0;
  };

  typedef std::unordered_map<StreamIdType, StreamInfo> StreamInfoMap;

  // Erases first occurrence (which should be the only one) of |info| in
  // |ready_list|, returning true if found (and erased), or false otherwise.
  // Decrements |num_ready_streams_| if an entry is erased.
  bool Erase(ReadyList* ready_list, const StreamInfo& info) {
    auto it = std::find(ready_list->begin(), ready_list->end(), &info);
    if (it == ready_list->end()) {
      return false;
    }
    ready_list->erase(it);
    --num_ready_streams_;
    return true;
  }

  // Number of ready streams.
  size_t num_ready_streams_ = 0;
  // Per-priority state, including ready lists.
  PriorityInfo priority_infos_[kV3LowestPriority + 1];
  // StreamInfos for all registered streams.
  StreamInfoMap stream_infos_;
};

}  // namespace net

#endif  // NET_SPDY_CORE_PRIORITY_WRITE_SCHEDULER_H_
