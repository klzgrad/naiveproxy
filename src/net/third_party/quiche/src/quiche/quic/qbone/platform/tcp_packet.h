// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_TCP_PACKET_H_
#define QUICHE_QUIC_QBONE_PLATFORM_TCP_PACKET_H_

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <functional>

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {

// Creates an TCPv6 RST packet, returning a packed string representation of the
// packet to |cb|.
void CreateTcpResetPacket(
    absl::string_view original_packet,
    quiche::UnretainedCallback<void(absl::string_view)> cb);

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_TCP_PACKET_H_
