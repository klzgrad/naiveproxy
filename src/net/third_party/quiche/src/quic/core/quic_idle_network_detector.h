// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_IDLE_NETWORK_DETECTOR_H_
#define QUICHE_QUIC_CORE_QUIC_IDLE_NETWORK_DETECTOR_H_

#include "net/third_party/quiche/src/quic/core/quic_alarm.h"
#include "net/third_party/quiche/src/quic/core/quic_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_one_block_arena.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class QuicConnectionPeer;
class QuicIdleNetworkDetectorTestPeer;
}  // namespace test

// QuicIdleNetworkDetector detects handshake timeout and idle network timeout.
// Handshake timeout detection is disabled after handshake completes. Idle
// network deadline is extended by network activity (e.g., sending or receiving
// packets).
class QUIC_EXPORT_PRIVATE QuicIdleNetworkDetector {
 public:
  class QUIC_EXPORT_PRIVATE Delegate {
   public:
    virtual ~Delegate() {}

    // Called when handshake times out.
    virtual void OnHandshakeTimeout() = 0;

    // Called when idle network has been detected.
    virtual void OnIdleNetworkDetected() = 0;
  };

  QuicIdleNetworkDetector(Delegate* delegate,
                          QuicTime now,
                          QuicConnectionArena* arena,
                          QuicAlarmFactory* alarm_factory);

  void OnAlarm();

  // Called to set handshake_timeout_ and idle_network_timeout_.
  void SetTimeouts(QuicTime::Delta handshake_timeout,
                   QuicTime::Delta idle_network_timeout);

  void StopDetection();

  // Called when a packet gets sent.
  void OnPacketSent(QuicTime now);

  // Called when a packet gets received.
  void OnPacketReceived(QuicTime now);

  QuicTime::Delta handshake_timeout() const { return handshake_timeout_; }

  QuicTime time_of_last_received_packet() const {
    return time_of_last_received_packet_;
  }

  QuicTime last_network_activity_time() const {
    return std::max(time_of_last_received_packet_,
                    time_of_first_packet_sent_after_receiving_);
  }

  QuicTime::Delta idle_network_timeout() const { return idle_network_timeout_; }

 private:
  friend class test::QuicConnectionPeer;
  friend class test::QuicIdleNetworkDetectorTestPeer;

  void SetAlarm();

  Delegate* delegate_;  // Not owned.

  // Start time of the detector, handshake deadline = start_time_ +
  // handshake_timeout_.
  const QuicTime start_time_;

  // Handshake timeout. Infinit means handshake has completed.
  QuicTime::Delta handshake_timeout_;

  // Time that last packet is received for this connection. Initialized to
  // start_time_.
  QuicTime time_of_last_received_packet_;

  // Time that the first packet gets sent after the received packet. idle
  // network deadline = std::max(time_of_last_received_packet_,
  // time_of_first_packet_sent_after_receiving_) + idle_network_timeout_.
  // Initialized to 0.
  QuicTime time_of_first_packet_sent_after_receiving_;

  // Idle network timeout. Infinit means no idle network timeout.
  QuicTime::Delta idle_network_timeout_;

  QuicArenaScopedPtr<QuicAlarm> alarm_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_IDLE_NETWORK_DETECTOR_H_
