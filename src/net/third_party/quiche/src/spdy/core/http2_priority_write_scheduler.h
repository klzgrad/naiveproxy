// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_HTTP2_PRIORITY_WRITE_SCHEDULER_H_
#define QUICHE_SPDY_CORE_HTTP2_PRIORITY_WRITE_SCHEDULER_H_

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/spdy/core/spdy_intrusive_list.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"
#include "net/third_party/quiche/src/spdy/core/write_scheduler.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_bug_tracker.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_containers.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_logging.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_map_util.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_ptr_util.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_string_utils.h"

namespace spdy {

namespace test {
template <typename StreamIdType>
class Http2PriorityWriteSchedulerPeer;
}

// This data structure implements the HTTP/2 stream priority tree defined in
// section 5.3 of RFC 7540:
// http://tools.ietf.org/html/rfc7540#section-5.3
//
// Streams can be added and removed, and dependencies between them defined.
// Streams constitute a tree rooted at stream ID 0: each stream has a single
// parent stream, and 0 or more child streams.  Individual streams can be
// marked as ready to read/write, and then the whole structure can be queried
// to pick the next stream to read/write out of those that are ready.
template <typename StreamIdType>
class Http2PriorityWriteScheduler : public WriteScheduler<StreamIdType> {
 public:
  using typename WriteScheduler<StreamIdType>::StreamPrecedenceType;

  Http2PriorityWriteScheduler();
  Http2PriorityWriteScheduler(const Http2PriorityWriteScheduler&) = delete;
  Http2PriorityWriteScheduler& operator=(const Http2PriorityWriteScheduler&) =
      delete;

  // WriteScheduler methods
  void RegisterStream(StreamIdType stream_id,
                      const StreamPrecedenceType& precedence) override;
  void UnregisterStream(StreamIdType stream_id) override;
  bool StreamRegistered(StreamIdType stream_id) const override;
  StreamPrecedenceType GetStreamPrecedence(
      StreamIdType stream_id) const override;
  void UpdateStreamPrecedence(StreamIdType stream_id,
                              const StreamPrecedenceType& precedence) override;
  std::vector<StreamIdType> GetStreamChildren(
      StreamIdType stream_id) const override;
  void RecordStreamEventTime(StreamIdType stream_id,
                             int64_t now_in_usec) override;
  int64_t GetLatestEventWithPrecedence(StreamIdType stream_id) const override;
  bool ShouldYield(StreamIdType stream_id) const override;
  void MarkStreamReady(StreamIdType stream_id, bool add_to_front) override;
  void MarkStreamNotReady(StreamIdType stream_id) override;
  bool HasReadyStreams() const override;
  StreamIdType PopNextReadyStream() override;
  std::tuple<StreamIdType, StreamPrecedenceType>
  PopNextReadyStreamAndPrecedence() override;
  size_t NumReadyStreams() const override;
  bool IsStreamReady(StreamIdType stream_id) const override;
  size_t NumRegisteredStreams() const override;
  std::string DebugString() const override;

 private:
  friend class test::Http2PriorityWriteSchedulerPeer<StreamIdType>;

  struct StreamInfo;
  typedef SpdyInlinedVector<StreamInfo*, 4> StreamInfoVector;

  struct StreamInfo : public SpdyIntrusiveLink<StreamInfo> {
    // ID for this stream.
    StreamIdType id;
    // StreamInfo for parent stream.
    StreamInfo* parent = nullptr;
    // Weights can range between 1 and 256 (inclusive).
    int weight = kHttp2DefaultStreamWeight;
    // The total weight of this stream's direct descendants.
    int total_child_weights = 0;
    // Pointers to StreamInfos for children, if any.
    StreamInfoVector children;
    // Whether the stream is ready for writing. The stream is present in
    // scheduling_queue_ iff true.
    bool ready = false;
    // The scheduling priority of this stream. Streams with higher priority
    // values are scheduled first.
    // TODO(mpw): rename to avoid confusion with SPDY priorities,
    //   which this is not.
    float priority = 0;
    // Ordinal value for this stream, used to ensure round-robin scheduling:
    // among streams with the same scheduling priority, streams with lower
    // ordinal are scheduled first.
    int64_t ordinal = 0;
    // Time of latest write event for stream of this priority, in microseconds.
    int64_t last_event_time_usec = 0;

    // Returns true if this stream is ancestor of |other|.
    bool IsAncestorOf(const StreamInfo& other) const {
      for (const StreamInfo* p = other.parent; p != nullptr; p = p->parent) {
        if (p == this) {
          return true;
        }
      }
      return false;
    }

    // Whether this stream should be scheduled ahead of another stream.
    bool SchedulesBefore(const StreamInfo& other) const {
      return (priority != other.priority) ? priority > other.priority
                                          : ordinal < other.ordinal;
    }

    // Returns the StreamPrecedenceType for this StreamInfo.
    StreamPrecedenceType ToStreamPrecedence() const {
      StreamIdType parent_id =
          parent == nullptr ? kHttp2RootStreamId : parent->id;
      bool exclusive = parent != nullptr && parent->children.size() == 1;
      return StreamPrecedenceType(parent_id, weight, exclusive);
    }
  };

  static bool Remove(StreamInfoVector* stream_infos,
                     const StreamInfo* stream_info);

  // Returns true iff any direct or transitive parent of the given stream is
  // currently ready.
  static bool HasReadyAncestor(const StreamInfo& stream_info);

  // Returns StreamInfo for the given stream, or nullptr if it isn't
  // registered.
  const StreamInfo* FindStream(StreamIdType stream_id) const;
  StreamInfo* FindStream(StreamIdType stream_id);

  // Helpers for UpdateStreamPrecedence().
  void UpdateStreamParent(StreamInfo* stream_info,
                          StreamIdType parent_id,
                          bool exclusive);
  void UpdateStreamWeight(StreamInfo* stream_info, int weight);

  // Update all priority values in the subtree rooted at the given stream, not
  // including the stream itself. If this results in priority value changes for
  // scheduled streams, those streams are rescheduled to ensure proper ordering
  // of scheduling_queue_.
  // TODO(mpw): rename to avoid confusion with SPDY priorities.
  void UpdatePrioritiesUnder(StreamInfo* stream_info);

  // Inserts stream into scheduling_queue_ at the appropriate location given
  // its priority and ordinal. Time complexity is O(scheduling_queue.size()).
  void Schedule(StreamInfo* stream_info);

  // Removes stream from scheduling_queue_.
  void Unschedule(StreamInfo* stream_info);

  // Return true if all internal invariants hold (useful for unit tests).
  // Unless there are bugs, this should always return true.
  bool ValidateInvariantsForTests() const;

  // Returns true if the parent stream has the given stream in its children.
  bool StreamHasChild(const StreamInfo& parent_info,
                      const StreamInfo* child_info) const;

  // Pointee owned by all_stream_infos_.
  StreamInfo* root_stream_info_;
  // Maps from stream IDs to StreamInfo objects.
  SpdySmallMap<StreamIdType, std::unique_ptr<StreamInfo>, 10> all_stream_infos_;
  // Queue containing all ready streams, ordered with streams of higher
  // priority before streams of lower priority, and, among streams of equal
  // priority, streams with lower ordinal before those with higher
  // ordinal. Note that not all streams in scheduling_queue_ are eligible to be
  // picked as the next stream: some may have ancestor stream(s) that are ready
  // and unblocked. In these situations the occluded child streams are left in
  // the queue, to reduce churn.
  SpdyIntrusiveList<StreamInfo> scheduling_queue_;
  // Ordinal value to assign to next node inserted into scheduling_queue_ when
  // |add_to_front == true|. Decremented after each assignment.
  int64_t head_ordinal_ = -1;
  // Ordinal value to assign to next node inserted into scheduling_queue_ when
  // |add_to_front == false|. Incremented after each assignment.
  int64_t tail_ordinal_ = 0;
};

template <typename StreamIdType>
Http2PriorityWriteScheduler<StreamIdType>::Http2PriorityWriteScheduler() {
  auto root_stream_info = SpdyMakeUnique<StreamInfo>();
  root_stream_info_ = root_stream_info.get();
  root_stream_info->id = kHttp2RootStreamId;
  root_stream_info->weight = kHttp2DefaultStreamWeight;
  root_stream_info->parent = nullptr;
  root_stream_info->priority = 1.0;
  root_stream_info->ready = false;
  all_stream_infos_[kHttp2RootStreamId] = std::move(root_stream_info);
}

template <typename StreamIdType>
bool Http2PriorityWriteScheduler<StreamIdType>::StreamRegistered(
    StreamIdType stream_id) const {
  return SpdyContainsKey(all_stream_infos_, stream_id);
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::RegisterStream(
    StreamIdType stream_id,
    const StreamPrecedenceType& precedence) {
  // TODO(mpw): uncomment the SPDY_BUG_IF below once all tests
  //   (e.g. SpdyClientDispatcher) modified to pass StreamPrecedence instances
  //   appropriate for protocol version under test.
  //
  // SPDY_BUG_IF(precedence.is_spdy3_priority())
  //     << "Expected HTTP/2 stream dependency";

  if (StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " already registered";
    return;
  }

  StreamInfo* parent = FindStream(precedence.parent_id());
  if (parent == nullptr) {
    // parent_id may legitimately not be registered yet--see b/15676312.
    SPDY_VLOG(1) << "Parent stream " << precedence.parent_id()
                 << " not registered";
    parent = root_stream_info_;
  }

  auto new_stream_info = SpdyMakeUnique<StreamInfo>();
  StreamInfo* new_stream_info_ptr = new_stream_info.get();
  new_stream_info_ptr->id = stream_id;
  new_stream_info_ptr->weight = precedence.weight();
  new_stream_info_ptr->parent = parent;
  all_stream_infos_[stream_id] = std::move(new_stream_info);
  if (precedence.is_exclusive()) {
    // Move the parent's current children below the new stream.
    using std::swap;
    swap(new_stream_info_ptr->children, parent->children);
    new_stream_info_ptr->total_child_weights = parent->total_child_weights;
    // Update each child's parent.
    for (StreamInfo* child : new_stream_info_ptr->children) {
      child->parent = new_stream_info_ptr;
    }
    // Clear parent's old child data.
    DCHECK(parent->children.empty());
    parent->total_child_weights = 0;
  }
  // Add new stream to parent.
  parent->children.push_back(new_stream_info_ptr);
  parent->total_child_weights += precedence.weight();

  // Update all priorities under parent, since addition of a stream affects
  // sibling priorities as well.
  UpdatePrioritiesUnder(parent);

  // Stream starts with ready == false, so no need to schedule it yet.
  DCHECK(!new_stream_info_ptr->ready);
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::UnregisterStream(
    StreamIdType stream_id) {
  if (stream_id == kHttp2RootStreamId) {
    SPDY_BUG << "Cannot unregister root stream";
    return;
  }
  // Remove the stream from table.
  auto it = all_stream_infos_.find(stream_id);
  if (it == all_stream_infos_.end()) {
    SPDY_BUG << "Stream " << stream_id << " not registered";
    return;
  }
  std::unique_ptr<StreamInfo> stream_info(std::move(it->second));
  all_stream_infos_.erase(it);
  // If ready (and hence scheduled), unschedule.
  if (stream_info->ready) {
    Unschedule(stream_info.get());
  }

  StreamInfo* parent = stream_info->parent;
  // Remove the stream from parent's child list.
  Remove(&parent->children, stream_info.get());
  parent->total_child_weights -= stream_info->weight;

  // Move the stream's children to the parent's child list.
  // Update each child's parent and weight.
  for (StreamInfo* child : stream_info->children) {
    child->parent = parent;
    parent->children.push_back(child);
    // Divide the removed stream's weight among its children, rounding to the
    // nearest valid weight.
    float float_weight = stream_info->weight *
                         static_cast<float>(child->weight) /
                         static_cast<float>(stream_info->total_child_weights);
    int new_weight = floor(float_weight + 0.5);
    if (new_weight == 0) {
      new_weight = 1;
    }
    child->weight = new_weight;
    parent->total_child_weights += child->weight;
  }
  UpdatePrioritiesUnder(parent);
}

template <typename StreamIdType>
typename Http2PriorityWriteScheduler<StreamIdType>::StreamPrecedenceType
Http2PriorityWriteScheduler<StreamIdType>::GetStreamPrecedence(
    StreamIdType stream_id) const {
  const StreamInfo* stream_info = FindStream(stream_id);
  if (stream_info == nullptr) {
    // Unknown streams tolerated due to b/15676312. However, return lowest
    // weight.
    SPDY_VLOG(1) << "Stream " << stream_id << " not registered";
    return StreamPrecedenceType(kHttp2RootStreamId, kHttp2MinStreamWeight,
                                false);
  }
  return stream_info->ToStreamPrecedence();
}

template <typename StreamIdType>
std::vector<StreamIdType>
Http2PriorityWriteScheduler<StreamIdType>::GetStreamChildren(
    StreamIdType stream_id) const {
  std::vector<StreamIdType> child_vec;
  const StreamInfo* stream_info = FindStream(stream_id);
  if (stream_info == nullptr) {
    SPDY_BUG << "Stream " << stream_id << " not registered";
  } else {
    child_vec.reserve(stream_info->children.size());
    for (StreamInfo* child : stream_info->children) {
      child_vec.push_back(child->id);
    }
  }
  return child_vec;
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::UpdateStreamPrecedence(
    StreamIdType stream_id,
    const StreamPrecedenceType& precedence) {
  // TODO(mpw): uncomment the SPDY_BUG_IF below once all tests
  //   (e.g. SpdyClientDispatcher) modified to pass StreamPrecedence instances
  //   appropriate for protocol version under test.
  //
  // SPDY_BUG_IF(precedence.is_spdy3_priority())
  //     << "Expected HTTP/2 stream dependency";
  if (stream_id == kHttp2RootStreamId) {
    SPDY_BUG << "Cannot set precedence of root stream";
    return;
  }

  StreamInfo* stream_info = FindStream(stream_id);
  if (stream_info == nullptr) {
    // TODO(mpw): add to all_stream_infos_ on demand--see b/15676312.
    SPDY_VLOG(1) << "Stream " << stream_id << " not registered";
    return;
  }
  UpdateStreamParent(stream_info, precedence.parent_id(),
                     precedence.is_exclusive());
  UpdateStreamWeight(stream_info, precedence.weight());
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::UpdateStreamWeight(
    StreamInfo* stream_info,
    int weight) {
  if (weight == stream_info->weight) {
    return;
  }
  if (stream_info->parent != nullptr) {
    stream_info->parent->total_child_weights += (weight - stream_info->weight);
  }
  stream_info->weight = weight;

  // Change in weight also affects sibling priorities.
  UpdatePrioritiesUnder(stream_info->parent);
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::UpdateStreamParent(
    StreamInfo* stream_info,
    StreamIdType parent_id,
    bool exclusive) {
  if (stream_info->id == parent_id) {
    SPDY_BUG << "Cannot set stream to be its own parent";
    return;
  }
  StreamInfo* new_parent = FindStream(parent_id);
  if (new_parent == nullptr) {
    // parent_id may legitimately not be registered yet--see b/15676312.
    SPDY_VLOG(1) << "Parent stream " << parent_id << " not registered";
    return;
  }

  if (stream_info->parent == new_parent &&
      (!exclusive || new_parent->children.size() == 1u)) {
    // If the new parent is already the stream's parent, and exclusivity (if
    // specified) is already satisfied, we are done.
    return;
  }

  // Next, check to see if the new parent is currently a descendant
  // of the stream.
  StreamInfo* last = new_parent->parent;
  bool cycle_exists = false;
  while (last != nullptr) {
    if (last == stream_info) {
      cycle_exists = true;
      break;
    }
    last = last->parent;
  }

  if (cycle_exists) {
    // The new parent moves to the level of the current stream.
    UpdateStreamParent(new_parent, stream_info->parent->id, false);
  }

  // Remove stream from old parent's child list.
  StreamInfo* old_parent = stream_info->parent;
  Remove(&old_parent->children, stream_info);
  old_parent->total_child_weights -= stream_info->weight;
  UpdatePrioritiesUnder(old_parent);

  if (exclusive) {
    // Move the new parent's current children below the current stream.
    for (StreamInfo* child : new_parent->children) {
      child->parent = stream_info;
      stream_info->children.push_back(child);
    }
    stream_info->total_child_weights += new_parent->total_child_weights;
    // Clear new parent's old child data.
    new_parent->children.clear();
    new_parent->total_child_weights = 0;
  }

  // Make the change.
  stream_info->parent = new_parent;
  new_parent->children.push_back(stream_info);
  new_parent->total_child_weights += stream_info->weight;
  UpdatePrioritiesUnder(new_parent);
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::RecordStreamEventTime(
    StreamIdType stream_id,
    int64_t now_in_usec) {
  if (stream_id == kHttp2RootStreamId) {
    SPDY_BUG << "Cannot record event time for root stream";
    return;
  }
  StreamInfo* stream_info = FindStream(stream_id);
  if (stream_info == nullptr) {
    SPDY_BUG << "Stream " << stream_id << " not registered";
    return;
  }
  stream_info->last_event_time_usec = now_in_usec;
}

// O(n) in the number of streams, which isn't great. However, this method will
// soon be superseded by
// Http2WeightedWriteScheduler::GetLatestEventWithPrecedence(), for which an
// efficient implementation is straightforward. Also, this method is only
// called when calculating idle timeouts, so performance isn't key.
template <typename StreamIdType>
int64_t Http2PriorityWriteScheduler<StreamIdType>::GetLatestEventWithPrecedence(
    StreamIdType stream_id) const {
  if (stream_id == kHttp2RootStreamId) {
    SPDY_BUG << "Invalid argument: root stream";
    return 0;
  }
  const StreamInfo* stream_info = FindStream(stream_id);
  if (stream_info == nullptr) {
    SPDY_BUG << "Stream " << stream_id << " not registered";
    return 0;
  }
  int64_t last_event_time_usec = 0;
  for (const auto& kv : all_stream_infos_) {
    const StreamInfo& other = *kv.second;
    if (other.priority > stream_info->priority) {
      last_event_time_usec =
          std::max(last_event_time_usec, other.last_event_time_usec);
    }
  }
  return last_event_time_usec;
}

// Worst-case time complexity of O(n*d), where n is scheduling queue length and
// d is tree depth. In practice, should be much shorter, since loop terminates
// at first writable stream or |stream_id| (whichever is first).
template <typename StreamIdType>
bool Http2PriorityWriteScheduler<StreamIdType>::ShouldYield(
    StreamIdType stream_id) const {
  if (stream_id == kHttp2RootStreamId) {
    SPDY_BUG << "Invalid argument: root stream";
    return false;
  }
  const StreamInfo* stream_info = FindStream(stream_id);
  if (stream_info == nullptr) {
    SPDY_BUG << "Stream " << stream_id << " not registered";
    return false;
  }
  if (HasReadyAncestor(*stream_info)) {
    return true;
  }
  for (const StreamInfo& scheduled : scheduling_queue_) {
    if (HasReadyAncestor(scheduled)) {
      // Skip streams which cannot be scheduled.
      continue;
    }
    if (stream_info->IsAncestorOf(scheduled)) {
      // Do not yield to descendants.
      return false;
    }
    // Yield to streams with higher priorities.
    return scheduled.SchedulesBefore(*stream_info);
  }
  return false;
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::MarkStreamReady(
    StreamIdType stream_id,
    bool add_to_front) {
  if (stream_id == kHttp2RootStreamId) {
    SPDY_BUG << "Cannot mark root stream ready";
    return;
  }
  StreamInfo* stream_info = FindStream(stream_id);
  if (stream_info == nullptr) {
    SPDY_BUG << "Stream " << stream_id << " not registered";
    return;
  }
  if (stream_info->ready) {
    return;
  }
  stream_info->ordinal = add_to_front ? head_ordinal_-- : tail_ordinal_++;
  Schedule(stream_info);
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::MarkStreamNotReady(
    StreamIdType stream_id) {
  if (stream_id == kHttp2RootStreamId) {
    SPDY_BUG << "Cannot mark root stream unready";
    return;
  }
  StreamInfo* stream_info = FindStream(stream_id);
  if (stream_info == nullptr) {
    SPDY_BUG << "Stream " << stream_id << " not registered";
    return;
  }
  if (!stream_info->ready) {
    return;
  }
  Unschedule(stream_info);
}

template <typename StreamIdType>
bool Http2PriorityWriteScheduler<StreamIdType>::Remove(
    StreamInfoVector* stream_infos,
    const StreamInfo* stream_info) {
  for (typename StreamInfoVector::iterator it = stream_infos->begin();
       it != stream_infos->end(); ++it) {
    if (*it == stream_info) {
      stream_infos->erase(it);
      return true;
    }
  }
  return false;
}

template <typename StreamIdType>
bool Http2PriorityWriteScheduler<StreamIdType>::HasReadyAncestor(
    const StreamInfo& stream_info) {
  for (const StreamInfo* parent = stream_info.parent; parent != nullptr;
       parent = parent->parent) {
    if (parent->ready) {
      return true;
    }
  }
  return false;
}

template <typename StreamIdType>
const typename Http2PriorityWriteScheduler<StreamIdType>::StreamInfo*
Http2PriorityWriteScheduler<StreamIdType>::FindStream(
    StreamIdType stream_id) const {
  auto it = all_stream_infos_.find(stream_id);
  return it == all_stream_infos_.end() ? nullptr : it->second.get();
}

template <typename StreamIdType>
typename Http2PriorityWriteScheduler<StreamIdType>::StreamInfo*
Http2PriorityWriteScheduler<StreamIdType>::FindStream(StreamIdType stream_id) {
  auto it = all_stream_infos_.find(stream_id);
  return it == all_stream_infos_.end() ? nullptr : it->second.get();
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::UpdatePrioritiesUnder(
    StreamInfo* stream_info) {
  for (StreamInfo* child : stream_info->children) {
    child->priority = stream_info->priority *
                      (static_cast<float>(child->weight) /
                       static_cast<float>(stream_info->total_child_weights));
    if (child->ready) {
      // Reposition in scheduling_queue_. Use post-order for scheduling, to
      // benefit from the fact that children have priority <= parent priority.
      Unschedule(child);
      UpdatePrioritiesUnder(child);
      Schedule(child);
    } else {
      UpdatePrioritiesUnder(child);
    }
  }
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::Schedule(
    StreamInfo* stream_info) {
  DCHECK(!stream_info->ready);
  for (StreamInfo& s : scheduling_queue_) {
    if (stream_info->SchedulesBefore(s)) {
      scheduling_queue_.insert(&s, stream_info);
      stream_info->ready = true;
      return;
    }
  }
  scheduling_queue_.push_back(stream_info);
  stream_info->ready = true;
}

template <typename StreamIdType>
void Http2PriorityWriteScheduler<StreamIdType>::Unschedule(
    StreamInfo* stream_info) {
  DCHECK(stream_info->ready);
  scheduling_queue_.erase(stream_info);
  stream_info->ready = false;
}

template <typename StreamIdType>
bool Http2PriorityWriteScheduler<StreamIdType>::StreamHasChild(
    const StreamInfo& parent_info,
    const StreamInfo* child_info) const {
  auto found = std::find(parent_info.children.begin(),
                         parent_info.children.end(), child_info);
  return found != parent_info.children.end();
}

template <typename StreamIdType>
bool Http2PriorityWriteScheduler<StreamIdType>::HasReadyStreams() const {
  return !scheduling_queue_.empty();
}

template <typename StreamIdType>
StreamIdType Http2PriorityWriteScheduler<StreamIdType>::PopNextReadyStream() {
  return std::get<0>(PopNextReadyStreamAndPrecedence());
}

template <typename StreamIdType>
std::tuple<
    StreamIdType,
    typename Http2PriorityWriteScheduler<StreamIdType>::StreamPrecedenceType>
Http2PriorityWriteScheduler<StreamIdType>::PopNextReadyStreamAndPrecedence() {
  for (StreamInfo& stream_info : scheduling_queue_) {
    if (!HasReadyAncestor(stream_info)) {
      Unschedule(&stream_info);
      return std::make_tuple(stream_info.id, stream_info.ToStreamPrecedence());
    }
  }
  SPDY_BUG << "No ready streams";
  return std::make_tuple(
      kHttp2RootStreamId,
      StreamPrecedenceType(kHttp2RootStreamId, kHttp2MinStreamWeight, false));
}

template <typename StreamIdType>
size_t Http2PriorityWriteScheduler<StreamIdType>::NumReadyStreams() const {
  return scheduling_queue_.size();
}

template <typename StreamIdType>
bool Http2PriorityWriteScheduler<StreamIdType>::IsStreamReady(
    StreamIdType stream_id) const {
  if (stream_id == kHttp2RootStreamId) {
    SPDY_BUG << "Try to check whether root stream is ready";
    return false;
  }
  const StreamInfo* stream_info = FindStream(stream_id);
  if (stream_info == nullptr) {
    SPDY_BUG << "Stream " << stream_id << " not registered";
    return false;
  }
  return stream_info->ready;
}

template <typename StreamIdType>
size_t Http2PriorityWriteScheduler<StreamIdType>::NumRegisteredStreams() const {
  return all_stream_infos_.size();
}

template <typename StreamIdType>
std::string Http2PriorityWriteScheduler<StreamIdType>::DebugString() const {
  return SpdyStrCat("Http2PriorityWriteScheduler {num_registered_streams=",
                    NumRegisteredStreams(),
                    " num_ready_streams=", NumReadyStreams(), "}");
}

template <typename StreamIdType>
bool Http2PriorityWriteScheduler<StreamIdType>::ValidateInvariantsForTests()
    const {
  size_t total_streams = 0;
  size_t streams_visited = 0;
  // Iterate through all streams in the map.
  for (const auto& kv : all_stream_infos_) {
    ++total_streams;
    ++streams_visited;
    StreamIdType stream_id = kv.first;
    const StreamInfo& stream_info = *kv.second;

    // Verify each StreamInfo mapped under the proper stream ID.
    if (stream_id != stream_info.id) {
      SPDY_DLOG(INFO) << "Stream ID " << stream_id
                      << " maps to StreamInfo with ID " << stream_info.id;
      return false;
    }

    // All streams except the root should have a parent, and should appear in
    // the children of that parent.
    if (stream_info.id != kHttp2RootStreamId &&
        !StreamHasChild(*stream_info.parent, &stream_info)) {
      SPDY_DLOG(INFO) << "Parent stream " << stream_info.parent->id
                      << " is not registered, or does not list stream "
                      << stream_info.id << " as its child.";
      return false;
    }

    if (!stream_info.children.empty()) {
      int total_child_weights = 0;
      // Iterate through the stream's children.
      for (StreamInfo* child : stream_info.children) {
        ++streams_visited;
        // Each stream in the list should exist and should have this stream
        // set as its parent.
        if (!StreamRegistered(child->id) || child->parent != &stream_info) {
          SPDY_DLOG(INFO) << "Child stream " << child->id
                          << " is not registered, "
                          << "or does not list " << stream_info.id
                          << " as its parent.";
          return false;
        }
        total_child_weights += child->weight;
      }
      // Verify that total_child_weights is correct.
      if (total_child_weights != stream_info.total_child_weights) {
        SPDY_DLOG(INFO) << "Child weight totals do not agree. For stream "
                        << stream_info.id << " total_child_weights has value "
                        << stream_info.total_child_weights << ", expected "
                        << total_child_weights;
        return false;
      }
    }
  }

  // Make sure NumRegisteredStreams() reflects the total number of streams the
  // map contains.
  if (total_streams != NumRegisteredStreams()) {
    SPDY_DLOG(INFO) << "Map contains incorrect number of streams.";
    return false;
  }
  // Validate the validation function; we should have visited each stream twice
  // (except for the root)
  DCHECK(streams_visited == 2 * NumRegisteredStreams() - 1);
  return true;
}

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_HTTP2_PRIORITY_WRITE_SCHEDULER_H_
