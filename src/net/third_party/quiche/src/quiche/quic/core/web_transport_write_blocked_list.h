// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_WEB_TRANSPORT_WRITE_BLOCKED_LIST_H_
#define QUICHE_QUIC_CORE_WEB_TRANSPORT_WRITE_BLOCKED_LIST_H_

#include <cstddef>
#include <limits>
#include <ostream>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_write_blocked_list.h"
#include "quiche/common/btree_scheduler.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

// Scheduler that is capable of handling both regular HTTP/3 priorities and
// WebTransport priorities for multiple sessions at the same time.
//
// Here is a brief overview of the scheme:
//   - At the top, there are HTTP/3 streams that are ordered by urgency as
//     defined in RFC 9218.
//   - The HTTP/3 connection can be a host to multiple WebTransport sessions.
//     Those are identified by the ID of the HTTP/3 control stream that created
//     the session; they also inherit the priority from that stream.
//   - The sessions consist of send groups that all have equal priority.
//   - The send groups have individual WebTransport data streams; each data
//     stream has a send order, which is a strict priority expressed as int64.
//
// To simplify the implementation of an already excessively complex scheme, this
// class makes a couple of affordances:
//   - Instead of first scheduling an individual session, then scheduling a
//     group within it, it schedules session-group pairs at the top level. This
//     is technically allowed by the spec, but it does mean that sessions with
//     more groups may get more bandwidth.
//   - Incremental priorities are not currently supported.
class QUICHE_EXPORT WebTransportWriteBlockedList
    : public QuicWriteBlockedListInterface {
 public:
  // Handle static streams by treating them as streams of priority MAX + 1.
  static constexpr int kStaticUrgency = HttpStreamPriority::kMaximumUrgency + 1;

  // QuicWriteBlockedListInterface implementation.
  bool HasWriteBlockedDataStreams() const override;
  size_t NumBlockedSpecialStreams() const override;
  size_t NumBlockedStreams() const override;

  void RegisterStream(QuicStreamId stream_id, bool is_static_stream,
                      const QuicStreamPriority& raw_priority) override;
  void UnregisterStream(QuicStreamId stream_id) override;
  void UpdateStreamPriority(QuicStreamId stream_id,
                            const QuicStreamPriority& new_priority) override;

  bool ShouldYield(QuicStreamId id) const override;
  QuicStreamPriority GetPriorityOfStream(QuicStreamId id) const override;
  QuicStreamId PopFront() override;
  void UpdateBytesForStream(QuicStreamId /*stream_id*/,
                            size_t /*bytes*/) override {}
  void AddStream(QuicStreamId stream_id) override;
  bool IsStreamBlocked(QuicStreamId stream_id) const override;

  size_t NumRegisteredGroups() const {
    return web_transport_session_schedulers_.size();
  }
  size_t NumRegisteredHttpStreams() const {
    return main_schedule_.NumRegistered() - NumRegisteredGroups();
  }

 private:
  // ScheduleKey represents anything that can be put into the main scheduler,
  // which is either:
  //   - an HTTP/3 stream, or
  //   - an individual WebTransport session-send group pair.
  class QUICHE_EXPORT ScheduleKey {
   public:
    static ScheduleKey HttpStream(QuicStreamId id) {
      return ScheduleKey(id, kNoSendGroup);
    }
    static ScheduleKey WebTransportSession(QuicStreamId session_id,
                                           webtransport::SendGroupId group_id) {
      return ScheduleKey(session_id, group_id);
    }
    static ScheduleKey WebTransportSession(const QuicStreamPriority& priority) {
      return ScheduleKey(priority.web_transport().session_id,
                         priority.web_transport().send_group_number);
    }

    bool operator==(const ScheduleKey& other) const {
      return stream_ == other.stream_ && group_ == other.group_;
    }
    bool operator!=(const ScheduleKey& other) const {
      return !(*this == other);
    }

    template <typename H>
    friend H AbslHashValue(H h, const ScheduleKey& key) {
      return H::combine(std::move(h), key.stream_, key.group_);
    }

    bool has_group() const { return group_ != kNoSendGroup; }
    quic::QuicStreamId stream() const { return stream_; }

    std::string DebugString() const;

    friend inline std::ostream& operator<<(std::ostream& os,
                                           const ScheduleKey& key) {
      os << key.DebugString();
      return os;
    }

   private:
    static constexpr webtransport::SendGroupId kNoSendGroup =
        std::numeric_limits<webtransport::SendGroupId>::max();

    explicit ScheduleKey(quic::QuicStreamId stream,
                         webtransport::SendGroupId group)
        : stream_(stream), group_(group) {}

    quic::QuicStreamId stream_;
    webtransport::SendGroupId group_;
  };

  // WebTransport requires individual sessions to have the same urgency as their
  // control streams; in a naive implementation, that would mean that both would
  // get the same urgency N, but we also want for the control streams to have
  // higher priority than WebTransport user data. In order to achieve that, we
  // enter control streams at urgency 2 * N + 1, and data streams at urgency
  // 2 * N.
  static constexpr int RemapUrgency(int urgency, bool is_http) {
    return urgency * 2 + (is_http ? 1 : 0);
  }

  // Scheduler for individual WebTransport send groups.
  using Subscheduler =
      quiche::BTreeScheduler<QuicStreamId, webtransport::SendOrder>;

  // Top-level scheduler used to multiplex WebTransport sessions and individual
  // HTTP/3 streams.
  quiche::BTreeScheduler<ScheduleKey, int> main_schedule_;
  // Records of priority for every stream; used when looking up WebTransport
  // session associated with an individual stream.
  absl::flat_hash_map<QuicStreamId, QuicStreamPriority> priorities_;
  // Schedulers for individual WebTransport send groups.
  absl::flat_hash_map<ScheduleKey, Subscheduler>
      web_transport_session_schedulers_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_WEB_TRANSPORT_WRITE_BLOCKED_LIST_H_
