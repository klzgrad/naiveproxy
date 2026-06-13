// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_WEB_TRANSPORT_STATS_H_
#define QUICHE_QUIC_CORE_WEB_TRANSPORT_STATS_H_

#include "quiche/quic/core/quic_session.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

QUICHE_EXPORT webtransport::DatagramStats
WebTransportDatagramStatsForQuicSession(const QuicSession& session);

QUICHE_EXPORT webtransport::SessionStats WebTransportStatsForQuicSession(
    const QuicSession& session);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_WEB_TRANSPORT_STATS_H_
