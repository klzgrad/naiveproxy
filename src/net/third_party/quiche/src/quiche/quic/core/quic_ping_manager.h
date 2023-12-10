// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_PING_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_PING_MANAGER_H_

#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_one_block_arena.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class QuicConnectionPeer;
class QuicPingManagerPeer;
}  // namespace test

// QuicPingManager manages an alarm that has two modes:
// 1) keep-alive. When alarm fires, send packet to extend idle timeout to keep
// connection alive.
// 2) retransmittable-on-wire. When alarm fires, send packets to detect path
// degrading (used in IP/port migrations).
class QUICHE_EXPORT QuicPingManager {
 public:
  // Interface that get notified when |alarm_| fires.
  class QUICHE_EXPORT Delegate {
   public:
    virtual ~Delegate() {}

    // Called when alarm fires in keep-alive mode.
    virtual void OnKeepAliveTimeout() = 0;
    // Called when alarm fires in retransmittable-on-wire mode.
    virtual void OnRetransmittableOnWireTimeout() = 0;
  };

  QuicPingManager(Perspective perspective, Delegate* delegate,
                  QuicConnectionArena* arena, QuicAlarmFactory* alarm_factory,
                  QuicConnectionContext* context);

  // Called to set |alarm_|.
  void SetAlarm(QuicTime now, bool should_keep_alive,
                bool has_in_flight_packets);

  // Called when |alarm_| fires.
  void OnAlarm();

  // Called to stop |alarm_| permanently.
  void Stop();

  void set_keep_alive_timeout(QuicTime::Delta keep_alive_timeout) {
    QUICHE_DCHECK(!alarm_->IsSet());
    keep_alive_timeout_ = keep_alive_timeout;
  }

  void set_initial_retransmittable_on_wire_timeout(
      QuicTime::Delta retransmittable_on_wire_timeout) {
    QUICHE_DCHECK(!alarm_->IsSet());
    initial_retransmittable_on_wire_timeout_ = retransmittable_on_wire_timeout;
  }

  void reset_consecutive_retransmittable_on_wire_count() {
    consecutive_retransmittable_on_wire_count_ = 0;
  }

 private:
  friend class test::QuicConnectionPeer;
  friend class test::QuicPingManagerPeer;

  // Update |retransmittable_on_wire_deadline_| and |keep_alive_deadline_|.
  void UpdateDeadlines(QuicTime now, bool should_keep_alive,
                       bool has_in_flight_packets);

  // Get earliest deadline of |retransmittable_on_wire_deadline_| and
  // |keep_alive_deadline_|. Returns 0 if both deadlines are not initialized.
  QuicTime GetEarliestDeadline() const;

  Perspective perspective_;

  Delegate* delegate_;  // Not owned.

  // Initial timeout for how long the wire can have no retransmittable packets.
  QuicTime::Delta initial_retransmittable_on_wire_timeout_ =
      QuicTime::Delta::Infinite();

  // Indicates how many consecutive retransmittable-on-wire has been armed
  // (since last reset).
  int consecutive_retransmittable_on_wire_count_ = 0;

  // Indicates how many retransmittable-on-wire has been armed in total.
  int retransmittable_on_wire_count_ = 0;

  QuicTime::Delta keep_alive_timeout_ =
      QuicTime::Delta::FromSeconds(kPingTimeoutSecs);

  QuicTime retransmittable_on_wire_deadline_ = QuicTime::Zero();

  QuicTime keep_alive_deadline_ = QuicTime::Zero();

  QuicArenaScopedPtr<QuicAlarm> alarm_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_PING_MANAGER_H_
