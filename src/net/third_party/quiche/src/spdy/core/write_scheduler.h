// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_WRITE_SCHEDULER_H_
#define QUICHE_SPDY_CORE_WRITE_SCHEDULER_H_

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

namespace spdy {

// Abstract superclass for classes that decide which SPDY or HTTP/2 stream to
// write next. Concrete subclasses implement various scheduling policies:
//
// PriorityWriteScheduler: implements SPDY priority-based stream scheduling,
//     where (writable) higher-priority streams are always given precedence
//     over lower-priority streams.
//
// Http2PriorityWriteScheduler: implements SPDY priority-based stream
//     scheduling coupled with the HTTP/2 stream dependency model. This is only
//     intended as a transitional step towards Http2WeightedWriteScheduler.
//
// Http2WeightedWriteScheduler (coming soon): implements the HTTP/2 stream
//     dependency model with weighted stream scheduling, fully conforming to
//     RFC 7540.
//
// The type used to represent stream IDs (StreamIdType) is templated in order
// to allow for use by both SPDY and QUIC codebases. It must be a POD that
// supports comparison (i.e., a numeric type).
//
// Each stream can be in one of two states: ready or not ready (for writing).
// Ready state is changed by calling the MarkStreamReady() and
// MarkStreamNotReady() methods. Only streams in the ready state can be
// returned by PopNextReadyStream(); when returned by that method, the stream's
// state changes to not ready.
template <typename StreamIdType>
class QUICHE_EXPORT_PRIVATE WriteScheduler {
 public:
  typedef StreamPrecedence<StreamIdType> StreamPrecedenceType;

  virtual ~WriteScheduler() {}

  // Registers new stream |stream_id| with the scheduler, assigning it the
  // given precedence. If the scheduler supports stream dependencies, the
  // stream is inserted into the dependency tree under
  // |precedence.parent_id()|.
  //
  // Preconditions: |stream_id| should be unregistered, and
  // |precedence.parent_id()| should be registered or |kHttp2RootStreamId|.
  virtual void RegisterStream(StreamIdType stream_id,
                              const StreamPrecedenceType& precedence) = 0;

  // Unregisters the given stream from the scheduler, which will no longer keep
  // state for it.
  //
  // Preconditions: |stream_id| should be registered.
  virtual void UnregisterStream(StreamIdType stream_id) = 0;

  // Returns true if the given stream is currently registered.
  virtual bool StreamRegistered(StreamIdType stream_id) const = 0;

  // Returns the precedence of the specified stream. If the scheduler supports
  // stream dependencies, calling |parent_id()| on the return value returns the
  // stream's parent, and calling |exclusive()| returns true iff the specified
  // stream is an only child of the parent stream.
  //
  // Preconditions: |stream_id| should be registered.
  virtual StreamPrecedenceType GetStreamPrecedence(
      StreamIdType stream_id) const = 0;

  // Updates the precedence of the given stream. If the scheduler supports
  // stream dependencies, |stream_id|'s parent will be updated to be
  // |precedence.parent_id()| if it is not already.
  //
  // Preconditions: |stream_id| should be unregistered, and
  // |precedence.parent_id()| should be registered or |kHttp2RootStreamId|.
  virtual void UpdateStreamPrecedence(
      StreamIdType stream_id,
      const StreamPrecedenceType& precedence) = 0;

  // Returns child streams of the given stream, if any. If the scheduler
  // doesn't support stream dependencies, returns an empty vector.
  //
  // Preconditions: |stream_id| should be registered.
  virtual std::vector<StreamIdType> GetStreamChildren(
      StreamIdType stream_id) const = 0;

  // Records time (in microseconds) of a read/write event for the given
  // stream.
  //
  // Preconditions: |stream_id| should be registered.
  virtual void RecordStreamEventTime(StreamIdType stream_id,
                                     int64_t now_in_usec) = 0;

  // Returns time (in microseconds) of the last read/write event for a stream
  // with higher priority than the priority of the given stream, or 0 if there
  // is no such event.
  //
  // Preconditions: |stream_id| should be registered.
  virtual int64_t GetLatestEventWithPrecedence(
      StreamIdType stream_id) const = 0;

  // If the scheduler has any ready streams, returns the next scheduled
  // ready stream, in the process transitioning the stream from ready to not
  // ready.
  //
  // Preconditions: |HasReadyStreams() == true|
  virtual StreamIdType PopNextReadyStream() = 0;

  // If the scheduler has any ready streams, returns the next scheduled
  // ready stream and its priority, in the process transitioning the stream from
  // ready to not ready.
  //
  // Preconditions: |HasReadyStreams() == true|
  virtual std::tuple<StreamIdType, StreamPrecedenceType>
  PopNextReadyStreamAndPrecedence() = 0;

  // Returns true if there's another stream ahead of the given stream in the
  // scheduling queue.  This function can be called to see if the given stream
  // should yield work to another stream.
  //
  // Preconditions: |stream_id| should be registered.
  virtual bool ShouldYield(StreamIdType stream_id) const = 0;

  // Marks the stream as ready to write. If the stream was already ready, does
  // nothing. If add_to_front is true, the stream is scheduled ahead of other
  // streams of the same priority/weight, otherwise it is scheduled behind them.
  //
  // Preconditions: |stream_id| should be registered.
  virtual void MarkStreamReady(StreamIdType stream_id, bool add_to_front) = 0;

  // Marks the stream as not ready to write. If the stream is not registered or
  // not ready, does nothing.
  //
  // Preconditions: |stream_id| should be registered.
  virtual void MarkStreamNotReady(StreamIdType stream_id) = 0;

  // Returns true iff the scheduler has any ready streams.
  virtual bool HasReadyStreams() const = 0;

  // Returns the number of streams currently marked ready.
  virtual size_t NumReadyStreams() const = 0;

  // Returns true if stream with |stream_id| is ready.
  virtual bool IsStreamReady(StreamIdType stream_id) const = 0;

  // Returns the number of registered streams.
  virtual size_t NumRegisteredStreams() const = 0;

  // Returns summary of internal state, for logging/debugging.
  virtual std::string DebugString() const = 0;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_WRITE_SCHEDULER_H_
