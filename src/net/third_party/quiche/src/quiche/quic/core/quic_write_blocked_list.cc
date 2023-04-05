// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_write_blocked_list.h"

#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

QuicWriteBlockedList::QuicWriteBlockedList() : last_priority_popped_(0) {
  memset(batch_write_stream_id_, 0, sizeof(batch_write_stream_id_));
  memset(bytes_left_for_batch_write_, 0, sizeof(bytes_left_for_batch_write_));
}

bool QuicWriteBlockedList::ShouldYield(QuicStreamId id) const {
  for (const auto& stream : static_stream_collection_) {
    if (stream.id == id) {
      // Static streams should never yield to data streams, or to lower
      // priority static stream.
      return false;
    }
    if (stream.is_blocked) {
      return true;  // All data streams yield to static streams.
    }
  }

  return priority_write_scheduler_.ShouldYield(id);
}

QuicStreamId QuicWriteBlockedList::PopFront() {
  QuicStreamId static_stream_id;
  if (static_stream_collection_.UnblockFirstBlocked(&static_stream_id)) {
    return static_stream_id;
  }

  const auto id_and_priority =
      priority_write_scheduler_.PopNextReadyStreamAndPriority();
  const QuicStreamId id = std::get<0>(id_and_priority);
  const spdy::SpdyPriority priority = std::get<1>(id_and_priority).urgency;

  if (!priority_write_scheduler_.HasReadyStreams()) {
    // If no streams are blocked, don't bother latching.  This stream will be
    // the first popped for its priority anyway.
    batch_write_stream_id_[priority] = 0;
    last_priority_popped_ = priority;
  } else if (batch_write_stream_id_[priority] != id) {
    // If newly latching this batch write stream, let it write 16k.
    batch_write_stream_id_[priority] = id;
    bytes_left_for_batch_write_[priority] = 16000;
    last_priority_popped_ = priority;
  }

  return id;
}

void QuicWriteBlockedList::RegisterStream(QuicStreamId stream_id,
                                          bool is_static_stream,
                                          const QuicStreamPriority& priority) {
  QUICHE_DCHECK(!priority_write_scheduler_.StreamRegistered(stream_id))
      << "stream " << stream_id << " already registered";
  if (is_static_stream) {
    static_stream_collection_.Register(stream_id);
    return;
  }

  priority_write_scheduler_.RegisterStream(stream_id, priority);
}

void QuicWriteBlockedList::UnregisterStream(QuicStreamId stream_id) {
  if (static_stream_collection_.Unregister(stream_id)) {
    return;
  }
  priority_write_scheduler_.UnregisterStream(stream_id);
}

void QuicWriteBlockedList::UpdateStreamPriority(
    QuicStreamId stream_id, const QuicStreamPriority& new_priority) {
  QUICHE_DCHECK(!static_stream_collection_.IsRegistered(stream_id));
  priority_write_scheduler_.UpdateStreamPriority(stream_id, new_priority);
}

void QuicWriteBlockedList::UpdateBytesForStream(QuicStreamId stream_id,
                                                size_t bytes) {
  if (batch_write_stream_id_[last_priority_popped_] == stream_id) {
    // If this was the last data stream popped by PopFront, update the
    // bytes remaining in its batch write.
    bytes_left_for_batch_write_[last_priority_popped_] -=
        std::min(bytes_left_for_batch_write_[last_priority_popped_], bytes);
  }
}

void QuicWriteBlockedList::AddStream(QuicStreamId stream_id) {
  if (static_stream_collection_.SetBlocked(stream_id)) {
    return;
  }

  bool push_front =
      stream_id == batch_write_stream_id_[last_priority_popped_] &&
      bytes_left_for_batch_write_[last_priority_popped_] > 0;
  priority_write_scheduler_.MarkStreamReady(stream_id, push_front);
}

bool QuicWriteBlockedList::IsStreamBlocked(QuicStreamId stream_id) const {
  for (const auto& stream : static_stream_collection_) {
    if (stream.id == stream_id) {
      return stream.is_blocked;
    }
  }

  return priority_write_scheduler_.IsStreamReady(stream_id);
}

void QuicWriteBlockedList::StaticStreamCollection::Register(QuicStreamId id) {
  QUICHE_DCHECK(!IsRegistered(id));
  streams_.push_back({id, false});
}

bool QuicWriteBlockedList::StaticStreamCollection::IsRegistered(
    QuicStreamId id) const {
  for (const auto& stream : streams_) {
    if (stream.id == id) {
      return true;
    }
  }
  return false;
}

bool QuicWriteBlockedList::StaticStreamCollection::Unregister(QuicStreamId id) {
  for (auto it = streams_.begin(); it != streams_.end(); ++it) {
    if (it->id == id) {
      if (it->is_blocked) {
        --num_blocked_;
      }
      streams_.erase(it);
      return true;
    }
  }
  return false;
}

bool QuicWriteBlockedList::StaticStreamCollection::SetBlocked(QuicStreamId id) {
  for (auto& stream : streams_) {
    if (stream.id == id) {
      if (!stream.is_blocked) {
        stream.is_blocked = true;
        ++num_blocked_;
      }
      return true;
    }
  }
  return false;
}

bool QuicWriteBlockedList::StaticStreamCollection::UnblockFirstBlocked(
    QuicStreamId* id) {
  for (auto& stream : streams_) {
    if (stream.is_blocked) {
      --num_blocked_;
      stream.is_blocked = false;
      *id = stream.id;
      return true;
    }
  }
  return false;
}

}  // namespace quic
