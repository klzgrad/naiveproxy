// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_FIFO_WRITE_SCHEDULER_H_
#define QUICHE_SPDY_CORE_FIFO_WRITE_SCHEDULER_H_

#include <map>
#include <set>
#include <string>

#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/spdy/core/write_scheduler.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_string_utils.h"

namespace spdy {

// A write scheduler where the stream with the smallest stream ID will have the
// highest priority.
template <typename StreamIdType>
class FifoWriteScheduler : public WriteScheduler<StreamIdType> {
 public:
  using typename WriteScheduler<StreamIdType>::StreamPrecedenceType;

  FifoWriteScheduler() = default;

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
  std::set<StreamIdType> ready_streams_;
  // This map maps stream ID to read/write event time (us since Unix epoch).
  std::map<StreamIdType, int64_t> registered_streams_;
};

template <typename StreamIdType>
void FifoWriteScheduler<StreamIdType>::RegisterStream(
    StreamIdType stream_id,
    const StreamPrecedenceType& /*precedence*/) {
  if (StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " already registered";
    return;
  }
  registered_streams_.emplace_hint(registered_streams_.end(), stream_id, 0);
}

template <typename StreamIdType>
void FifoWriteScheduler<StreamIdType>::UnregisterStream(
    StreamIdType stream_id) {
  if (!StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " is not registered";
    return;
  }
  registered_streams_.erase(stream_id);
  ready_streams_.erase(stream_id);
}

template <typename StreamIdType>
bool FifoWriteScheduler<StreamIdType>::StreamRegistered(
    StreamIdType stream_id) const {
  return registered_streams_.find(stream_id) != registered_streams_.end();
}

// Stream precedence is not supported by this scheduler.
template <typename StreamIdType>
typename FifoWriteScheduler<StreamIdType>::StreamPrecedenceType
FifoWriteScheduler<StreamIdType>::GetStreamPrecedence(
    StreamIdType /*stream_id*/) const {
  return StreamPrecedenceType(kV3LowestPriority);
}

template <typename StreamIdType>
void FifoWriteScheduler<StreamIdType>::UpdateStreamPrecedence(
    StreamIdType /*stream_id*/,
    const StreamPrecedenceType& /*precedence*/) {}

template <typename StreamIdType>
std::vector<StreamIdType> FifoWriteScheduler<StreamIdType>::GetStreamChildren(
    StreamIdType /*stream_id*/) const {
  return std::vector<StreamIdType>();
}

template <typename StreamIdType>
void FifoWriteScheduler<StreamIdType>::RecordStreamEventTime(
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
int64_t FifoWriteScheduler<StreamIdType>::GetLatestEventWithPrecedence(
    StreamIdType stream_id) const {
  if (!StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " is not registered";
    return 0;
  }
  int64_t latest_event_time_us = 0;
  for (auto it = registered_streams_.begin(); it != registered_streams_.end();
       ++it) {
    if (stream_id <= it->first) {
      break;
    }
    latest_event_time_us = std::max(latest_event_time_us, it->second);
  }
  return latest_event_time_us;
}

template <typename StreamIdType>
bool FifoWriteScheduler<StreamIdType>::ShouldYield(
    StreamIdType stream_id) const {
  return !ready_streams_.empty() && stream_id > *ready_streams_.begin();
}

template <typename StreamIdType>
void FifoWriteScheduler<StreamIdType>::MarkStreamReady(StreamIdType stream_id,
                                                       bool /*add_to_front*/) {
  if (!StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " is not registered";
    return;
  }
  if (ready_streams_.find(stream_id) != ready_streams_.end()) {
    SPDY_DVLOG(1) << "Stream already exists in the list";
    return;
  }
  ready_streams_.insert(stream_id);
}

template <typename StreamIdType>
void FifoWriteScheduler<StreamIdType>::MarkStreamNotReady(
    StreamIdType stream_id) {
  auto it = ready_streams_.find(stream_id);
  if (it == ready_streams_.end()) {
    SPDY_DVLOG(1) << "Try to remove a stream that is not on list";
    return;
  }
  ready_streams_.erase(it);
}

template <typename StreamIdType>
bool FifoWriteScheduler<StreamIdType>::HasReadyStreams() const {
  return !ready_streams_.empty();
}

template <typename StreamIdType>
StreamIdType FifoWriteScheduler<StreamIdType>::PopNextReadyStream() {
  if (ready_streams_.empty()) {
    SPDY_BUG << "No ready streams available";
    return 0;
  }
  auto it = ready_streams_.begin();
  StreamIdType id = *it;
  ready_streams_.erase(it);
  return id;
}

template <typename StreamIdType>
std::tuple<StreamIdType,
           typename FifoWriteScheduler<StreamIdType>::StreamPrecedenceType>
FifoWriteScheduler<StreamIdType>::PopNextReadyStreamAndPrecedence() {
  return std::make_tuple(PopNextReadyStream(),
                         StreamPrecedenceType(kV3LowestPriority));
}

template <typename StreamIdType>
size_t FifoWriteScheduler<StreamIdType>::NumReadyStreams() const {
  return ready_streams_.size();
}

template <typename StreamIdType>
bool FifoWriteScheduler<StreamIdType>::IsStreamReady(
    StreamIdType stream_id) const {
  if (!StreamRegistered(stream_id)) {
    SPDY_BUG << "Stream " << stream_id << " is not registered";
    return false;
  }
  return ready_streams_.find(stream_id) != ready_streams_.end();
}

template <typename StreamIdType>
size_t FifoWriteScheduler<StreamIdType>::NumRegisteredStreams() const {
  return registered_streams_.size();
}

template <typename StreamIdType>
std::string FifoWriteScheduler<StreamIdType>::DebugString() const {
  return quiche::QuicheStrCat(
      "FifoWriteScheduler {num_streams=", registered_streams_.size(),
      " num_ready_streams=", NumReadyStreams(), "}");
}

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_FIFO_WRITE_SCHEDULER_H_
