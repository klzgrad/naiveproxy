// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_CLIENT_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOQT_CLIENT_H_

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_default_client.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace moqt {

// A synchronous MoQT client based on QuicDefaultClient.
class QUICHE_EXPORT MoqtClient {
 public:
  MoqtClient(quic::QuicSocketAddress peer_address,
             const quic::QuicServerId& server_id,
             std::unique_ptr<quic::ProofVerifier> proof_verifier,
             quic::QuicEventLoop* event_loop);

  // Establishes the connection to the specified endpoint. The errors are
  // returned via the session termination callback.
  void Connect(std::string path, MoqtSessionCallbacks callbacks);

  MoqtSession* session() { return session_; }

 private:
  absl::Status ConnectInner(std::string path, MoqtSessionCallbacks& callbacks);

  quic::QuicDefaultClient spdy_client_;
  MoqtSession* session_ = nullptr;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_CLIENT_H_
