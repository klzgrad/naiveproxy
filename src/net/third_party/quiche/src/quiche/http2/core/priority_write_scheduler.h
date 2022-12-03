// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_CORE_PRIORITY_WRITE_SCHEDULER_H_
#define QUICHE_HTTP2_CORE_PRIORITY_WRITE_SCHEDULER_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "quiche/http2/core/write_scheduler.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace http2 {

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
template <typename StreamIdType>
class QUICHE_EXPORT_PRIVATE PriorityWriteScheduler
    : public WriteScheduler<StreamIdType> {
 public:
  using typename WriteScheduler<StreamIdType>::StreamPrecedenceType;

  // Creates scheduler with no streams.
  PriorityWriteScheduler() : PriorityWriteScheduler(spdy::kHttp2RootStreamId) {}
  explicit PriorityWriteScheduler(StreamIdType root_stream_id)
      : root_stream_id_(root_stream_id) {}

  void RegisterStream(StreamIdType stream_id,
                      const StreamPrecedenceType& precedence) override {
    // TODO(mpw): verify |precedence.is_spdy3_priority() == true| once
    //   Http2PriorityWriteScheduler enabled for HTTP/2.

    // parent_id not used here, but may as well validate it.  However,
    // parent_id may legitimately not be registered yet--see b/15676312.
    StreamIdType parent_id = precedence.parent_id();
    QUICHE_DVLOG_IF(
        1, parent_id != root_stream_id_ && !StreamRegistered(parent_id))
        << "Parent stream " << parent_id << " not registered";

    if (stream_id == root_stream_id_) {
      QUICHE_BUG(spdy_bug_19_1)
          << "Stream " << root_stream_id_ << " already registered";
      return;
    }
    auto stream_info = std::make_unique<StreamInfo>(
        StreamInfo{precedence.spdy3_priority(), stream_id, false});
    bool inserted =
        stream_infos_.insert(std::make_pair(stream_id, std::move(stream_info)))
            .second;
    QUICHE_BUG_IF(spdy_bug_19_2, !inserted)
        << "Stream " << stream_id << " already registered";
  }

  void UnregisterStream(StreamIdType stream_id) override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_3) << "Stream " << stream_id << " not registered";
      return;
    }
    const StreamInfo* const stream_info = it->second.get();
    if (stream_info->ready) {
      bool erased = Erase(&priority_infos_[stream_info->priority].ready_list,
                          stream_info);
      QUICHE_DCHECK(erased);
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
      QUICHE_DVLOG(1) << "Stream " << stream_id << " not registered";
      return StreamPrecedenceType(spdy::kV3LowestPriority);
    }
    return StreamPrecedenceType(it->second->priority);
  }

  void UpdateStreamPrecedence(StreamIdType stream_id,
                              const StreamPrecedenceType& precedence) override {
    // TODO(mpw): verify |precedence.is_spdy3_priority() == true| once
    //   Http2PriorityWriteScheduler enabled for HTTP/2.

    // parent_id not used here, but may as well validate it.  However,
    // parent_id may legitimately not be registered yet--see b/15676312.
    StreamIdType parent_id = precedence.parent_id();
    QUICHE_DVLOG_IF(
        1, parent_id != root_stream_id_ && !StreamRegistered(parent_id))
        << "Parent stream " << parent_id << " not registered";

    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      // TODO(mpw): add to stream_infos_ on demand--see b/15676312.
      QUICHE_DVLOG(1) << "Stream " << stream_id << " not registered";
      return;
    }
    StreamInfo* const stream_info = it->second.get();
    spdy::SpdyPriority new_priority = precedence.spdy3_priority();
    if (stream_info->priority == new_priority) {
      return;
    }
    if (stream_info->ready) {
      bool erased = Erase(&priority_infos_[stream_info->priority].ready_list,
                          stream_info);
      QUICHE_DCHECK(erased);
      priority_infos_[new_priority].ready_list.push_back(stream_info);
      ++num_ready_streams_;
    }
    stream_info->priority = new_priority;
  }

  std::vector<StreamIdType> GetStreamChildren(
      StreamIdType /*stream_id*/) const override {
    return std::vector<StreamIdType>();
  }

  void RecordStreamEventTime(StreamIdType stream_id,
                             int64_t now_in_usec) override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_4) << "Stream " << stream_id << " not registered";
      return;
    }
    PriorityInfo& priority_info = priority_infos_[it->second->priority];
    priority_info.last_event_time_usec =
        std::max(priority_info.last_event_time_usec, now_in_usec);
  }

  int64_t GetLatestEventWithPrecedence(StreamIdType stream_id) const override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_5) << "Stream " << stream_id << " not registered";
      return 0;
    }
    int64_t last_event_time_usec = 0;
    const StreamInfo* const stream_info = it->second.get();
    for (spdy::SpdyPriority p = spdy::kV3HighestPriority;
         p < stream_info->priority; ++p) {
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
    for (spdy::SpdyPriority p = spdy::kV3HighestPriority;
         p <= spdy::kV3LowestPriority; ++p) {
      ReadyList& ready_list = priority_infos_[p].ready_list;
      if (!ready_list.empty()) {
        StreamInfo* const info = ready_list.front();
        ready_list.pop_front();
        --num_ready_streams_;

        QUICHE_DCHECK(stream_infos_.find(info->stream_id) !=
                      stream_infos_.end());
        info->ready = false;
        return std::make_tuple(info->stream_id,
                               StreamPrecedenceType(info->priority));
      }
    }
    QUICHE_BUG(spdy_bug_19_6) << "No ready streams available";
    return std::make_tuple(0, StreamPrecedenceType(spdy::kV3LowestPriority));
  }

  bool ShouldYield(StreamIdType stream_id) const override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_7) << "Stream " << stream_id << " not registered";
      return false;
    }

    // If there's a higher priority stream, this stream should yield.
    const StreamInfo* const stream_info = it->second.get();
    for (spdy::SpdyPriority p = spdy::kV3HighestPriority;
         p < stream_info->priority; ++p) {
      if (!priority_infos_[p].ready_list.empty()) {
        return true;
      }
    }

    // If this priority level is empty, or this stream is the next up, there's
    // no need to yield.
    const auto& ready_list = priority_infos_[it->second->priority].ready_list;
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
      QUICHE_BUG(spdy_bug_19_8) << "Stream " << stream_id << " not registered";
      return;
    }
    StreamInfo* const stream_info = it->second.get();
    if (stream_info->ready) {
      return;
    }
    ReadyList& ready_list = priority_infos_[stream_info->priority].ready_list;
    if (add_to_front) {
      ready_list.push_front(stream_info);
    } else {
      ready_list.push_back(stream_info);
    }
    ++num_ready_streams_;
    stream_info->ready = true;
  }

  void MarkStreamNotReady(StreamIdType stream_id) override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_BUG(spdy_bug_19_9) << "Stream " << stream_id << " not registered";
      return;
    }
    StreamInfo* const stream_info = it->second.get();
    if (!stream_info->ready) {
      return;
    }
    bool erased =
        Erase(&priority_infos_[stream_info->priority].ready_list, stream_info);
    QUICHE_DCHECK(erased);
    stream_info->ready = false;
  }

  // Returns true iff the number of ready streams is non-zero.
  bool HasReadyStreams() const override { return num_ready_streams_ > 0; }

  // Returns the number of ready streams.
  size_t NumReadyStreams() const override { return num_ready_streams_; }

  size_t NumRegisteredStreams() const override { return stream_infos_.size(); }

  std::string DebugString() const override {
    return absl::StrCat(
        "PriorityWriteScheduler {num_streams=", stream_infos_.size(),
        " num_ready_streams=", NumReadyStreams(), "}");
  }

  // Returns true if a stream is ready.
  bool IsStreamReady(StreamIdType stream_id) const override {
    auto it = stream_infos_.find(stream_id);
    if (it == stream_infos_.end()) {
      QUICHE_DLOG(INFO) << "Stream " << stream_id << " not registered";
      return false;
    }
    return it->second->ready;
  }

 private:
  friend class test::PriorityWriteSchedulerPeer<StreamIdType>;

  // State kept for all registered streams. All ready streams have ready = true
  // and should be present in priority_infos_[priority].ready_list.
  struct QUICHE_EXPORT_PRIVATE StreamInfo {
    spdy::SpdyPriority priority;
    StreamIdType stream_id;
    bool ready;
  };

  // O(1) size lookup, O(1) insert at front or back (amortized).
  using ReadyList = quiche::QuicheCircularDeque<StreamInfo*>;

  // State kept for each priority level.
  struct QUICHE_EXPORT_PRIVATE PriorityInfo {
    // IDs of streams that are ready to write.
    ReadyList ready_list;
    // Time of latest write event for stream of this priority, in microseconds.
    int64_t last_event_time_usec = 0;
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
  PriorityInfo priority_infos_[spdy::kV3LowestPriority + 1];
  // StreamInfos for all registered streams.
  StreamInfoMap stream_infos_;
  StreamIdType root_stream_id_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_CORE_PRIORITY_WRITE_SCHEDULER_H_
