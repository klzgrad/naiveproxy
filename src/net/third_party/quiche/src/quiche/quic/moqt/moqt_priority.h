// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_PRIORITY_H_
#define QUICHE_QUIC_MOQT_MOQT_PRIORITY_H_

#include <cstdint>

#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// WebTransport lets applications split a session into multiple send groups
// that have equal weight for scheduling. We don't have a use for that, so the
// send group is always the same.
constexpr webtransport::SendGroupId kMoqtSendGroupId = 0;

// Priority that can be assigned to a track or individual streams associated
// with the track by either the publisher or the subscriber.
using MoqtPriority = uint8_t;
static constexpr uint64_t kMaxPriority = 0xff;
static constexpr MoqtPriority kDefaultPublisherPriority = 128;
static constexpr MoqtPriority kDefaultSubscriberPriority = 128;

// Indicates the desired order of delivering groups associated with a given
// track.
enum class MoqtDeliveryOrder : uint8_t {
  kAscending = 0x01,
  kDescending = 0x02,
};
static constexpr uint64_t kMinMoqtDeliveryOrder =
    static_cast<uint64_t>(MoqtDeliveryOrder::kAscending);
static constexpr uint64_t kMaxMoqtDeliveryOrder =
    static_cast<uint64_t>(MoqtDeliveryOrder::kDescending);

// The session weighs pending streams solely on the subscriber_priority and the
// highest of all pending publisher_priorities.
struct MoqtTrackPriority {
  MoqtPriority subscriber_priority;
  MoqtPriority publisher_priority;
  auto operator<=>(const MoqtTrackPriority& other) const = default;
};

// Computes WebTransport send order for an MoQT data stream with the specified
// parameters.
QUICHE_EXPORT webtransport::SendOrder SendOrderForStream(
    MoqtPriority subscriber_priority, MoqtPriority publisher_priority,
    uint64_t group_id, uint64_t subgroup_id, MoqtDeliveryOrder delivery_order);
// This is just a wrapper for SendOrderForStream, that uses the object ID where
// the subgroup ID would normally go.
QUICHE_EXPORT webtransport::SendOrder SendOrderForDatagram(
    MoqtPriority subscriber_priority, MoqtPriority publisher_priority,
    uint64_t group_id, uint64_t object_id, MoqtDeliveryOrder delivery_order);
// Determine the send order for FETCH. As all objects are on one stream, only
// subscriber priority matters.
QUICHE_EXPORT webtransport::SendOrder SendOrderForFetch(
    MoqtPriority subscriber_priority);

// Returns |send_order| updated with the new |subscriber_priority|.
QUICHE_EXPORT webtransport::SendOrder UpdateSendOrderForSubscriberPriority(
    webtransport::SendOrder send_order, MoqtPriority subscriber_priority);

// WebTransport send order set on the MoQT control stream.
QUICHE_EXPORT extern const webtransport::SendOrder kMoqtControlStreamSendOrder;

// WebTransport send order set on MoQT bandwidth probe streams.
QUICHE_EXPORT extern const webtransport::SendOrder kMoqtProbeStreamSendOrder;

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_PRIORITY_H_
