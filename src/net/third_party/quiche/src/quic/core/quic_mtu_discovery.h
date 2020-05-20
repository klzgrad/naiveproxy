// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_MTU_DISCOVERY_H_
#define QUICHE_QUIC_CORE_QUIC_MTU_DISCOVERY_H_

#include <iostream>

#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

// The initial number of packets between MTU probes.  After each attempt the
// number is doubled.
const QuicPacketCount kPacketsBetweenMtuProbesBase = 100;

// The number of MTU probes that get sent before giving up.
const size_t kMtuDiscoveryAttempts = 3;

// Ensure that exponential back-off does not result in an integer overflow.
// The number of packets can be potentially capped, but that is not useful at
// current kMtuDiscoveryAttempts value, and hence is not implemented at present.
static_assert(kMtuDiscoveryAttempts + 8 < 8 * sizeof(QuicPacketNumber),
              "The number of MTU discovery attempts is too high");
static_assert(kPacketsBetweenMtuProbesBase < (1 << 8),
              "The initial number of packets between MTU probes is too high");

// The incresed packet size targeted when doing path MTU discovery.
const QuicByteCount kMtuDiscoveryTargetPacketSizeHigh = 1450;
const QuicByteCount kMtuDiscoveryTargetPacketSizeLow = 1430;

static_assert(kMtuDiscoveryTargetPacketSizeLow <= kMaxOutgoingPacketSize,
              "MTU discovery target is too large");
static_assert(kMtuDiscoveryTargetPacketSizeHigh <= kMaxOutgoingPacketSize,
              "MTU discovery target is too large");

static_assert(kMtuDiscoveryTargetPacketSizeLow > kDefaultMaxPacketSize,
              "MTU discovery target does not exceed the default packet size");
static_assert(kMtuDiscoveryTargetPacketSizeHigh > kDefaultMaxPacketSize,
              "MTU discovery target does not exceed the default packet size");

// QuicConnectionMtuDiscoverer is a MTU discovery controller, it answers two
// questions:
// 1) Probe scheduling: Whether a connection should send a MTU probe packet
//    right now.
// 2) MTU search stradegy: When it is time to send, what should be the size of
//    the probing packet.
// Note the discoverer does not actually send or process probing packets.
//
// Unit tests are in QuicConnectionTest.MtuDiscovery*.
class QUIC_EXPORT_PRIVATE QuicConnectionMtuDiscoverer {
 public:
  // Construct a discoverer in the disabled state.
  QuicConnectionMtuDiscoverer() = default;

  // Construct a discoverer in the disabled state, with the given parameters.
  QuicConnectionMtuDiscoverer(QuicPacketCount packets_between_probes_base,
                              QuicPacketNumber next_probe_at);

  // Enable the discoverer by setting the probe target.
  // max_packet_length: The max packet length currently used.
  // target_max_packet_length: The target max packet length to probe.
  void Enable(QuicByteCount max_packet_length,
              QuicByteCount target_max_packet_length);

  // Disable the discoverer by unsetting the probe target.
  void Disable();

  // Whether a MTU probe packet should be sent right now.
  // Always return false if disabled.
  bool ShouldProbeMtu(QuicPacketNumber largest_sent_packet) const;

  // Called immediately before a probing packet is sent, to get the size of the
  // packet.
  // REQUIRES: ShouldProbeMtu(largest_sent_packet) == true.
  QuicPacketLength GetUpdatedMtuProbeSize(QuicPacketNumber largest_sent_packet);

  // Called after the max packet length is updated, which is triggered by a ack
  // of a probing packet.
  void OnMaxPacketLengthUpdated(QuicByteCount old_value,
                                QuicByteCount new_value);

  QuicPacketCount packets_between_probes() const {
    return packets_between_probes_;
  }

  QuicPacketNumber next_probe_at() const { return next_probe_at_; }

  QUIC_EXPORT_PRIVATE friend std::ostream& operator<<(
      std::ostream& os,
      const QuicConnectionMtuDiscoverer& d);

 private:
  bool IsEnabled() const;
  QuicPacketLength next_probe_packet_length() const;

  QuicPacketLength min_probe_length_ = 0;
  QuicPacketLength max_probe_length_ = 0;

  QuicPacketLength last_probe_length_ = 0;

  uint16_t remaining_probe_count_ = kMtuDiscoveryAttempts;

  // The number of packets between MTU probes.
  QuicPacketCount packets_between_probes_ = kPacketsBetweenMtuProbesBase;

  // The packet number of the packet after which the next MTU probe will be
  // sent.
  QuicPacketNumber next_probe_at_ =
      QuicPacketNumber(kPacketsBetweenMtuProbesBase);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_MTU_DISCOVERY_H_
