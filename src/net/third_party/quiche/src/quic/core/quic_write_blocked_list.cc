// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_write_blocked_list.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

QuicWriteBlockedList::QuicWriteBlockedList(QuicTransportVersion version)
    : priority_write_scheduler_(
          std::make_unique<spdy::PriorityWriteScheduler<QuicStreamId>>(
              QuicVersionUsesCryptoFrames(version)
                  ? std::numeric_limits<QuicStreamId>::max()
                  : 0)),
      last_priority_popped_(0),
      scheduler_type_(spdy::WriteSchedulerType::SPDY) {
  memset(batch_write_stream_id_, 0, sizeof(batch_write_stream_id_));
  memset(bytes_left_for_batch_write_, 0, sizeof(bytes_left_for_batch_write_));
}

QuicWriteBlockedList::~QuicWriteBlockedList() {}

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

  return priority_write_scheduler_->ShouldYield(id);
}

bool QuicWriteBlockedList::SwitchWriteScheduler(spdy::WriteSchedulerType type,
                                                QuicTransportVersion version) {
  if (scheduler_type_ == type) {
    return true;
  }
  if (priority_write_scheduler_->NumRegisteredStreams() != 0) {
    QUIC_BUG << "Cannot switch scheduler with registered streams";
    return false;
  }
  QUIC_DVLOG(1) << "Switching to scheduler type: "
                << spdy::WriteSchedulerTypeToString(type);
  switch (type) {
    case spdy::WriteSchedulerType::LIFO:
      priority_write_scheduler_ =
          std::make_unique<spdy::LifoWriteScheduler<QuicStreamId>>();
      break;
    case spdy::WriteSchedulerType::SPDY:
      priority_write_scheduler_ =
          std::make_unique<spdy::PriorityWriteScheduler<QuicStreamId>>(
              QuicVersionUsesCryptoFrames(version)
                  ? std::numeric_limits<QuicStreamId>::max()
                  : 0);
      break;
    case spdy::WriteSchedulerType::HTTP2:
      priority_write_scheduler_ =
          std::make_unique<spdy::Http2PriorityWriteScheduler<QuicStreamId>>();
      break;
    case spdy::WriteSchedulerType::FIFO:
      priority_write_scheduler_ =
          std::make_unique<spdy::FifoWriteScheduler<QuicStreamId>>();
      break;
    default:
      QUIC_BUG << "Scheduler is not supported for type: "
               << spdy::WriteSchedulerTypeToString(type);
      return false;
  }
  scheduler_type_ = type;
  return true;
}

QuicStreamId QuicWriteBlockedList::PopFront() {
  QuicStreamId static_stream_id;
  if (static_stream_collection_.UnblockFirstBlocked(&static_stream_id)) {
    return static_stream_id;
  }

  const auto id_and_precedence =
      priority_write_scheduler_->PopNextReadyStreamAndPrecedence();
  const QuicStreamId id = std::get<0>(id_and_precedence);
  if (scheduler_type_ != spdy::WriteSchedulerType::SPDY) {
    // No batch writing logic for non-SPDY priority write scheduler.
    return id;
  }
  const spdy::SpdyPriority priority =
      std::get<1>(id_and_precedence).spdy3_priority();

  if (!priority_write_scheduler_->HasReadyStreams()) {
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

void QuicWriteBlockedList::RegisterStream(
    QuicStreamId stream_id,
    bool is_static_stream,
    const spdy::SpdyStreamPrecedence& precedence) {
  DCHECK(!priority_write_scheduler_->StreamRegistered(stream_id))
      << "stream " << stream_id << " already registered";
  DCHECK(PrecedenceMatchesSchedulerType(precedence));
  if (is_static_stream) {
    static_stream_collection_.Register(stream_id);
    return;
  }

  priority_write_scheduler_->RegisterStream(stream_id, precedence);
}

void QuicWriteBlockedList::UnregisterStream(QuicStreamId stream_id,
                                            bool is_static) {
  if (is_static) {
    static_stream_collection_.Unregister(stream_id);
    return;
  }
  priority_write_scheduler_->UnregisterStream(stream_id);
}

void QuicWriteBlockedList::UpdateStreamPriority(
    QuicStreamId stream_id,
    const spdy::SpdyStreamPrecedence& new_precedence) {
  DCHECK(!static_stream_collection_.IsRegistered(stream_id));
  DCHECK(PrecedenceMatchesSchedulerType(new_precedence));
  priority_write_scheduler_->UpdateStreamPrecedence(stream_id, new_precedence);
}

void QuicWriteBlockedList::UpdateBytesForStream(QuicStreamId stream_id,
                                                size_t bytes) {
  if (scheduler_type_ != spdy::WriteSchedulerType::SPDY) {
    return;
  }
  if (batch_write_stream_id_[last_priority_popped_] == stream_id) {
    // If this was the last data stream popped by PopFront, update the
    // bytes remaining in its batch write.
    if (fix_bytes_left_for_batch_write_) {
      QUIC_RELOADABLE_FLAG_COUNT(quic_fix_bytes_left_for_batch_write);
      // TODO(fayang): change this static_cast to static_cast<uint32_t> when
      // deprecating quic_fix_bytes_left_for_batch_write.
      bytes_left_for_batch_write_[last_priority_popped_] -=
          std::min(bytes_left_for_batch_write_[last_priority_popped_],
                   static_cast<int32_t>(bytes));
    } else {
      bytes_left_for_batch_write_[last_priority_popped_] -=
          static_cast<int32_t>(bytes);
    }
  }
}

void QuicWriteBlockedList::AddStream(QuicStreamId stream_id) {
  if (static_stream_collection_.SetBlocked(stream_id)) {
    return;
  }

  bool push_front =
      scheduler_type_ == spdy::WriteSchedulerType::SPDY &&
      stream_id == batch_write_stream_id_[last_priority_popped_] &&
      bytes_left_for_batch_write_[last_priority_popped_] > 0;
  priority_write_scheduler_->MarkStreamReady(stream_id, push_front);
}

bool QuicWriteBlockedList::IsStreamBlocked(QuicStreamId stream_id) const {
  for (const auto& stream : static_stream_collection_) {
    if (stream.id == stream_id) {
      return stream.is_blocked;
    }
  }

  return priority_write_scheduler_->IsStreamReady(stream_id);
}

bool QuicWriteBlockedList::PrecedenceMatchesSchedulerType(
    const spdy::SpdyStreamPrecedence& precedence) {
  switch (scheduler_type_) {
    case spdy::WriteSchedulerType::LIFO:
      break;
    case spdy::WriteSchedulerType::SPDY:
      return precedence.is_spdy3_priority();
    case spdy::WriteSchedulerType::HTTP2:
      return !precedence.is_spdy3_priority();
    case spdy::WriteSchedulerType::FIFO:
      break;
    default:
      DCHECK(false);
      return false;
  }
  return true;
}

void QuicWriteBlockedList::StaticStreamCollection::Register(QuicStreamId id) {
  DCHECK(!IsRegistered(id));
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

void QuicWriteBlockedList::StaticStreamCollection::Unregister(QuicStreamId id) {
  for (auto it = streams_.begin(); it != streams_.end(); ++it) {
    if (it->id == id) {
      if (it->is_blocked) {
        --num_blocked_;
      }
      streams_.erase(it);
      return;
    }
  }
  DCHECK(false) << "Erasing a non-exist stream with id " << id;
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
