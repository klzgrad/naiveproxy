// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_WRITE_BLOCKED_LIST_H_
#define QUICHE_QUIC_CORE_QUIC_WRITE_BLOCKED_LIST_H_

#include <cstddef>
#include <cstdint>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "quiche/http2/core/priority_write_scheduler.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace quic {

// Keeps tracks of the order of QUIC streams that have data to write.
// Static streams come first, in the order they were registered with
// QuicWriteBlockedList.  They are followed by non-static streams, ordered by
// priority.
class QUIC_EXPORT_PRIVATE QuicWriteBlockedList {
 public:
  explicit QuicWriteBlockedList();
  QuicWriteBlockedList(const QuicWriteBlockedList&) = delete;
  QuicWriteBlockedList& operator=(const QuicWriteBlockedList&) = delete;
  ~QuicWriteBlockedList() = default;

  bool HasWriteBlockedDataStreams() const {
    return priority_write_scheduler_.HasReadyStreams();
  }

  bool HasWriteBlockedSpecialStream() const {
    return static_stream_collection_.num_blocked() > 0;
  }

  size_t NumBlockedSpecialStreams() const {
    return static_stream_collection_.num_blocked();
  }

  size_t NumBlockedStreams() const {
    return NumBlockedSpecialStreams() +
           priority_write_scheduler_.NumReadyStreams();
  }

  bool ShouldYield(QuicStreamId id) const;

  QuicStreamPriority GetPriorityofStream(QuicStreamId id) const {
    return priority_write_scheduler_.GetStreamPriority(id);
  }

  // Pops the highest priority stream, special casing static streams. Latches
  // the most recently popped data stream for batch writing purposes.
  QuicStreamId PopFront();

  // Register a stream with given priority.
  // `priority` is ignored for static streams.
  void RegisterStream(QuicStreamId stream_id, bool is_static_stream,
                      const QuicStreamPriority& priority);

  // Unregister a stream.  `stream_id` must be registered, either as a static
  // stream or as a non-static stream.
  void UnregisterStream(QuicStreamId stream_id);

  // Updates the stored priority of a stream.  Must not be called for static
  // streams.
  void UpdateStreamPriority(QuicStreamId stream_id,
                            const QuicStreamPriority& new_priority);

  void UpdateBytesForStream(QuicStreamId stream_id, size_t bytes);

  // Pushes a stream to the back of the list for its priority level *unless* it
  // is latched for doing batched writes in which case it goes to the front of
  // the list for its priority level.
  // Static streams are special cased to always resume first.
  // Stream must already be registered.
  void AddStream(QuicStreamId stream_id);

  // Returns true if stream with |stream_id| is write blocked.
  bool IsStreamBlocked(QuicStreamId stream_id) const;

 private:
  http2::PriorityWriteScheduler<QuicStreamId, QuicStreamPriority,
                                QuicStreamPriorityToInt,
                                IntToQuicStreamPriority>
      priority_write_scheduler_;

  // If performing batch writes, this will be the stream ID of the stream doing
  // batch writes for this priority level.  We will allow this stream to write
  // until it has written kBatchWriteSize bytes, it has no more data to write,
  // or a higher priority stream preempts.
  QuicStreamId batch_write_stream_id_[spdy::kV3LowestPriority + 1];
  // Set to kBatchWriteSize when we set a new batch_write_stream_id_ for a given
  // priority.  This is decremented with each write the stream does until it is
  // done with its batch write.
  size_t bytes_left_for_batch_write_[spdy::kV3LowestPriority + 1];
  // Tracks the last priority popped for UpdateBytesForStream.
  spdy::SpdyPriority last_priority_popped_;

  // A StaticStreamCollection is a vector of <QuicStreamId, bool> pairs plus a
  // eagerly-computed number of blocked static streams.
  class QUIC_EXPORT_PRIVATE StaticStreamCollection {
   public:
    struct QUIC_EXPORT_PRIVATE StreamIdBlockedPair {
      QuicStreamId id;
      bool is_blocked;
    };

    // Optimized for the typical case of 2 static streams per session.
    using StreamsVector = absl::InlinedVector<StreamIdBlockedPair, 2>;

    StreamsVector::const_iterator begin() const { return streams_.cbegin(); }

    StreamsVector::const_iterator end() const { return streams_.cend(); }

    size_t num_blocked() const { return num_blocked_; }

    // Add |id| to the collection in unblocked state.
    void Register(QuicStreamId id);

    // True if |id| is in the collection, regardless of its state.
    bool IsRegistered(QuicStreamId id) const;

    // Remove |id| from the collection.  If it is in the blocked state, reduce
    // |num_blocked_| by 1.  Returns true if |id| was in the collection.
    bool Unregister(QuicStreamId id);

    // Set |id| to be blocked. If |id| is not already blocked, increase
    // |num_blocked_| by 1.
    // Return true if |id| is in the collection.
    bool SetBlocked(QuicStreamId id);

    // Unblock the first blocked stream in the collection.
    // If no stream is blocked, return false. Otherwise return true, set *id to
    // the unblocked stream id and reduce |num_blocked_| by 1.
    bool UnblockFirstBlocked(QuicStreamId* id);

   private:
    size_t num_blocked_ = 0;
    StreamsVector streams_;
  };

  StaticStreamCollection static_stream_collection_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_WRITE_BLOCKED_LIST_H_
