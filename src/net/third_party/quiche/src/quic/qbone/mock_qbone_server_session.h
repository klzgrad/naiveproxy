// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_MOCK_QBONE_SERVER_SESSION_H_
#define QUICHE_QUIC_QBONE_MOCK_QBONE_SERVER_SESSION_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_server_session.h"

namespace quic {

class MockQboneServerSession : public QboneServerSession {
 public:
  explicit MockQboneServerSession(QuicConnection* connection)
      : QboneServerSession(CurrentSupportedVersions(),
                           connection,
                           /*owner=*/nullptr,
                           /*config=*/{},
                           /*quic_crypto_server_config=*/nullptr,
                           /*compressed_certs_cache=*/nullptr,
                           /*writer=*/nullptr,
                           /*self_ip=*/QuicIpAddress::Loopback6(),
                           /*client_ip=*/QuicIpAddress::Loopback6(),
                           /*client_ip_subnet_length=*/0,
                           /*handler=*/nullptr) {}

  MOCK_METHOD1(SendClientRequest, bool(const QboneClientRequest&));

  MOCK_METHOD1(ProcessPacketFromNetwork, void(QuicStringPiece));
  MOCK_METHOD1(ProcessPacketFromPeer, void(QuicStringPiece));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_MOCK_QBONE_SERVER_SESSION_H_
