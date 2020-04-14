// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_LIFO_WRITE_SCHEDULER_H_
#define QUICHE_SPDY_CORE_LIFO_WRITE_SCHEDULER_H_

#include <map>
#include <set>
#include <string>

#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/spdy/core/write_scheduler.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_containers.h"

namespace spdy {

namespace test {

template <typename StreamIdType>
class LifoWriteSchedulerPeer;

}  // namespace test

// Create a write scheduler where the stream added last will have the highest
// priority.
template <typename StreamIdType>
class LifoWriteScheduler : public WriteScheduler<StreamIdType> {
 public:
  using typename WriteScheduler<StreamIdType>::StreamPrecedenceType;

  LifoWriteScheduler() = default;

  void RegisterStream(StreamIdType stream_id,
                      const StreamPrecedenceType& /*precedence*/) override;

  void UnregisterStream(StreamIdType stream_id) override;

  bool StreamRegistered(StreamIdType stream_id) const override {
    return registered_streams_.find(stream_id) != registered_streams_.end();
  }

  // Stream precedence is not supported by this scheduler.
  StreamPrecedenceType GetStreamPrecedence(
      StreamIdType /*stream_id*/) const override {
    return StreamPrecedenceType(kV3LowestPriority);
  }

  void UpdateStreamPrecedence(
      StreamIdType /*stream_id*/,
      const StreamPrecedenceType& /*precedence*/) override {}

  std::vector<StreamIdType> GetStreamChildren(
      StreamIdType /*stream_id*/) const override {
    return std::vector<StreamIdType>();
  }

  void RecordStreamEventTime(StreamIdType stream_id,
                             int64_t now_in_usec) override;

  int64_t GetLatestEventWithPrecedence(StreamIdType stream_id) const override;

  StreamIdType PopNextReadyStream() override;

  std::tuple<StreamIdType, StreamPrecedenceType>
  PopNextReadyStreamAndPrecedence() override {
    return std::make_tuple(PopNextReadyStream(),
                           StreamPrecedenceType(kV3LowestPriority));
  }

  bool ShouldYield(StreamIdType stream_id) const override {
    return !ready_streams_.empty() && stream_id < *ready_streams_.rbegin();
  }

  void MarkStreamReady(StreamIdType stream_id, bool /*add_to_front*/) override;

  void MarkStreamNotReady(StreamIdType stream_id) override;

  bool HasReadyStreams() const override { return !ready_streams_.empty(); }
  size_t NumReadyStreams() const override { return ready_streams_.size(); }
  bool IsStreamReady(StreamIdType stream_id) const override;
  size_t NumRegisteredStreams() const override;
  std::string DebugString() const override;

 private:
  friend class test::LifoWriteSchedulerPeer<StreamIdType>;

  std::set<StreamIdType> ready_streams_;
  std::map<StreamIdType, int64_t> registered_streams_;
};

template <typename StreamIdType>
void LifoWriteScheduler<StreamIdType>::RegisterStream(
    StreamIdType stream_id,
    const StreamPrecedenceType& /*precedence*/) {
  if (StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " already registered";
    return;
  }
  registered_streams_.emplace_hint(registered_streams_.end(), stream_id, 0);
}

template <typename StreamIdType>
void LifoWriteScheduler<StreamIdType>::UnregisterStream(
    StreamIdType stream_id) {
  if (!StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " is not registered";
    return;
  }
  registered_streams_.erase(stream_id);
  ready_streams_.erase(stream_id);
}

template <typename StreamIdType>
void LifoWriteScheduler<StreamIdType>::RecordStreamEventTime(
    StreamIdType stream_id,
    int64_t now_in_usec) {
  auto it = registered_streams_.find(stream_id);
  if (it != registered_streams_.end()) {
    it->second = now_in_usec;
  } else {
    SPDY_BUG << "Stream " << stream_id << " is not registered";
  }
}

template <typename StreamIdType>
int64_t LifoWriteScheduler<StreamIdType>::GetLatestEventWithPrecedence(
    StreamIdType stream_id) const {
  if (!StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " is not registered";
    return 0;
  }
  int64_t latest_event_time_us = 0;
  for (auto it = registered_streams_.rbegin(); it != registered_streams_.rend();
       ++it) {
    if (stream_id < it->first) {
      if (it->second > latest_event_time_us) {
        latest_event_time_us = it->second;
      }
    } else {
      break;
    }
  }
  return latest_event_time_us;
}

template <typename StreamIdType>
StreamIdType LifoWriteScheduler<StreamIdType>::PopNextReadyStream() {
  if (ready_streams_.empty()) {
    SPDY_BUG << "No ready streams available";
    return 0;
  }
  auto it = --ready_streams_.end();
  StreamIdType id = *it;
  ready_streams_.erase(it);
  return id;
}

template <typename StreamIdType>
void LifoWriteScheduler<StreamIdType>::MarkStreamReady(StreamIdType stream_id,
                                                       bool /*add_to_front*/) {
  if (!StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " is not registered";
    return;
  }
  if (ready_streams_.find(stream_id) != ready_streams_.end()) {
    SPDY_VLOG(1) << "Stream already exists in the list";
    return;
  }
  ready_streams_.insert(stream_id);
}

template <typename StreamIdType>
void LifoWriteScheduler<StreamIdType>::MarkStreamNotReady(
    StreamIdType stream_id) {
  auto it = ready_streams_.find(stream_id);
  if (it == ready_streams_.end()) {
    SPDY_VLOG(1) << "Try to remove a stream that is not on list";
    return;
  }
  ready_streams_.erase(it);
}

template <typename StreamIdType>
bool LifoWriteScheduler<StreamIdType>::IsStreamReady(
    StreamIdType stream_id) const {
  if (!StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " is not registered";
    return false;
  }
  return ready_streams_.find(stream_id) != ready_streams_.end();
}

template <typename StreamIdType>
size_t LifoWriteScheduler<StreamIdType>::NumRegisteredStreams() const {
  return registered_streams_.size();
}

template <typename StreamIdType>
std::string LifoWriteScheduler<StreamIdType>::DebugString() const {
  return quiche::QuicheStrCat(
      "LifoWriteScheduler {num_streams=", registered_streams_.size(),
      " num_ready_streams=", NumReadyStreams(), "}");
}

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_LIFO_WRITE_SCHEDULER_H_
