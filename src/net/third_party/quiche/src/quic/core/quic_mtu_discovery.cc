// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_mtu_discovery.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_stack_trace.h"

namespace quic {

QuicConnectionMtuDiscoverer::QuicConnectionMtuDiscoverer(
    QuicPacketCount packets_between_probes_base,
    QuicPacketNumber next_probe_at)
    : packets_between_probes_(packets_between_probes_base),
      next_probe_at_(next_probe_at) {}

void QuicConnectionMtuDiscoverer::Enable(
    QuicByteCount max_packet_length,
    QuicByteCount target_max_packet_length) {
  DCHECK(!IsEnabled());

  if (target_max_packet_length <= max_packet_length) {
    QUIC_DVLOG(1) << "MtuDiscoverer not enabled. target_max_packet_length:"
                  << target_max_packet_length
                  << " <= max_packet_length:" << max_packet_length;
    return;
  }

  min_probe_length_ = max_packet_length;
  max_probe_length_ = target_max_packet_length;
  DCHECK(IsEnabled());

  QUIC_DVLOG(1) << "MtuDiscoverer enabled. min:" << min_probe_length_
                << ", max:" << max_probe_length_
                << ", next:" << next_probe_packet_length();
}

void QuicConnectionMtuDiscoverer::Disable() {
  *this = QuicConnectionMtuDiscoverer(packets_between_probes_, next_probe_at_);
}

bool QuicConnectionMtuDiscoverer::IsEnabled() const {
  return min_probe_length_ < max_probe_length_;
}

bool QuicConnectionMtuDiscoverer::ShouldProbeMtu(
    QuicPacketNumber largest_sent_packet) const {
  if (!IsEnabled()) {
    return false;
  }

  if (remaining_probe_count_ == 0) {
    QUIC_DVLOG(1)
        << "ShouldProbeMtu returns false because max probe count reached";
    return false;
  }

  if (largest_sent_packet < next_probe_at_) {
    QUIC_DVLOG(1) << "ShouldProbeMtu returns false because not enough packets "
                     "sent since last probe. largest_sent_packet:"
                  << largest_sent_packet
                  << ", next_probe_at_:" << next_probe_at_;
    return false;
  }

  QUIC_DVLOG(1) << "ShouldProbeMtu returns true. largest_sent_packet:"
                << largest_sent_packet;
  return true;
}

QuicPacketLength QuicConnectionMtuDiscoverer::GetUpdatedMtuProbeSize(
    QuicPacketNumber largest_sent_packet) {
  DCHECK(ShouldProbeMtu(largest_sent_packet));

  QuicPacketLength probe_packet_length = next_probe_packet_length();
  if (probe_packet_length == last_probe_length_) {
    // The next probe packet is as big as the previous one. Assuming the
    // previous one exceeded MTU, we need to decrease the probe packet length.
    max_probe_length_ = probe_packet_length;
  } else {
    DCHECK_GT(probe_packet_length, last_probe_length_);
  }
  last_probe_length_ = next_probe_packet_length();

  packets_between_probes_ *= 2;
  next_probe_at_ = largest_sent_packet + packets_between_probes_ + 1;
  if (remaining_probe_count_ > 0) {
    --remaining_probe_count_;
  }

  QUIC_DVLOG(1) << "GetUpdatedMtuProbeSize: probe_packet_length:"
                << last_probe_length_
                << ", New packets_between_probes_:" << packets_between_probes_
                << ", next_probe_at_:" << next_probe_at_
                << ", remaining_probe_count_:" << remaining_probe_count_;
  DCHECK(!ShouldProbeMtu(largest_sent_packet));
  return last_probe_length_;
}

QuicPacketLength QuicConnectionMtuDiscoverer::next_probe_packet_length() const {
  DCHECK_NE(min_probe_length_, 0);
  DCHECK_NE(max_probe_length_, 0);
  DCHECK_GE(max_probe_length_, min_probe_length_);

  const QuicPacketLength normal_next_probe_length =
      (min_probe_length_ + max_probe_length_ + 1) / 2;

  if (remaining_probe_count_ == 1 &&
      normal_next_probe_length > last_probe_length_) {
    // If the previous probe succeeded, and there is only one last probe to
    // send, use |max_probe_length_| for the last probe.
    return max_probe_length_;
  }
  return normal_next_probe_length;
}

void QuicConnectionMtuDiscoverer::OnMaxPacketLengthUpdated(
    QuicByteCount old_value,
    QuicByteCount new_value) {
  if (!IsEnabled() || new_value <= old_value) {
    return;
  }

  DCHECK_EQ(old_value, min_probe_length_);
  min_probe_length_ = new_value;
}

std::ostream& operator<<(std::ostream& os,
                         const QuicConnectionMtuDiscoverer& d) {
  os << "{ min_probe_length_:" << d.min_probe_length_
     << " max_probe_length_:" << d.max_probe_length_
     << " last_probe_length_:" << d.last_probe_length_
     << " remaining_probe_count_:" << d.remaining_probe_count_
     << " packets_between_probes_:" << d.packets_between_probes_
     << " next_probe_at_:" << d.next_probe_at_ << " }";
  return os;
}

}  // namespace quic
