// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_priority.h"

#include <cstdint>
#include <limits>

#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace {
template <uint64_t NumBits>
constexpr uint64_t Flip(uint64_t number) {
  static_assert(NumBits <= 63);
  return (1ull << NumBits) - 1 - number;
}
template <uint64_t N>
constexpr uint64_t OnlyLowestNBits(uint64_t value) {
  static_assert(N <= 62);
  return value & ((1ull << (N + 1)) - 1);
}
}  // namespace

// The send order is packed into a signed 64-bit integer as follows:
//   63: always zero to indicate a positive number
//   62: 0 for data streams, 1 for control streams
//   54-61: subscriber priority
//   46-53: publisher priority
//   20-45: group ID
//   0-19: object (for Datagrams) or subgroup (for streams) ID

webtransport::SendOrder SendOrderForStream(MoqtPriority subscriber_priority,
                                           MoqtPriority publisher_priority,
                                           uint64_t group_id,
                                           uint64_t subgroup_id,
                                           MoqtDeliveryOrder delivery_order) {
  const int64_t track_bits = (Flip<8>(subscriber_priority) << 54) |
                             (Flip<8>(publisher_priority) << 46);
  group_id = OnlyLowestNBits<26>(group_id);
  subgroup_id = OnlyLowestNBits<20>(subgroup_id);
  if (delivery_order == MoqtDeliveryOrder::kAscending) {
    group_id = Flip<26>(group_id);
  }
  subgroup_id = Flip<20>(subgroup_id);
  return track_bits | (group_id << 20) | subgroup_id;
}

webtransport::SendOrder SendOrderForDatagram(MoqtPriority subscriber_priority,
                                             MoqtPriority publisher_priority,
                                             uint64_t group_id,
                                             uint64_t object_id,
                                             MoqtDeliveryOrder delivery_order) {
  return SendOrderForStream(subscriber_priority, publisher_priority, group_id,
                            object_id, delivery_order);
}

webtransport::SendOrder SendOrderForFetch(MoqtPriority subscriber_priority) {
  return (Flip<8>(subscriber_priority) << 54);
}

webtransport::SendOrder UpdateSendOrderForSubscriberPriority(
    const webtransport::SendOrder send_order,
    MoqtPriority subscriber_priority) {
  webtransport::SendOrder new_send_order = OnlyLowestNBits<54>(send_order);
  const int64_t sub_bits = Flip<8>(subscriber_priority) << 54;
  new_send_order |= sub_bits;
  return new_send_order;
}

const webtransport::SendOrder kMoqtControlStreamSendOrder =
    std::numeric_limits<webtransport::SendOrder>::max();
const webtransport::SendOrder kMoqtProbeStreamSendOrder =
    std::numeric_limits<webtransport::SendOrder>::min();

}  // namespace moqt
