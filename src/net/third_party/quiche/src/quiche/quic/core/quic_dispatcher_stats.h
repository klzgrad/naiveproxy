// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_DISPATCHER_STATS_H_
#define QUICHE_QUIC_CORE_QUIC_DISPATCHER_STATS_H_

#include <cstddef>
#include <ostream>

#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Stats for a QuicDispatcher.
// Don't forget to update the operator<< implementation when adding new fields.
struct QUICHE_EXPORT QuicDispatcherStats {
  QUICHE_EXPORT friend std::ostream& operator<<(std::ostream& os,
                                                const QuicDispatcherStats& s);

  // How many incoming packets the dispatcher has processed.
  QuicPacketCount packets_processed = 0;

  // How many incoming packets the dispatcher has processed slowly. Packet
  // processing is slow if QuicDispatcher::ProcessHeader is called for the
  // packet.
  QuicPacketCount packets_processed_with_unknown_cid = 0;

  // How many incoming packets the dispatcher has processed whose packet header
  // has a replaced connection ID, according to the buffered packet store.
  // This counter is only incremented in debug builds.
  QuicPacketCount packets_processed_with_replaced_cid_in_store = 0;

  // How many incoming packets the dispatcher has enqueued into the buffered
  // packet store, because the received packet does not complete a CHLO.
  QuicPacketCount packets_enqueued_early = 0;

  // How many incoming packets the dispatcher has enqueued into the buffered
  // packet store, because the received packet completes a CHLO but the
  // dispatcher needs to limit the number of sessions created per event loop.
  QuicPacketCount packets_enqueued_chlo = 0;

  // How many packets the dispatcher has sent. Dispatcher only sends ACKs to
  // buffered IETF Initial packets.
  QuicPacketCount packets_sent = 0;

  // Number of sessions created by the dispatcher.
  size_t sessions_created = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_DISPATCHER_STATS_H_
