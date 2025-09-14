// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_SERVER_H_
#define QUICHE_QUIC_MASQUE_MASQUE_SERVER_H_

#include "quiche/quic/masque/masque_server_backend.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/tools/quic_server.h"

namespace quic {

// QUIC server that implements MASQUE.
class QUIC_NO_EXPORT MasqueServer : public QuicServer {
 public:
  explicit MasqueServer(MasqueMode masque_mode,
                        MasqueServerBackend* masque_server_backend);

  // Disallow copy and assign.
  MasqueServer(const MasqueServer&) = delete;
  MasqueServer& operator=(const MasqueServer&) = delete;

  // From QuicServer.
  QuicDispatcher* CreateQuicDispatcher() override;

 private:
  MasqueMode masque_mode_;
  MasqueServerBackend* masque_server_backend_;  // Unowned.
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_SERVER_H_
