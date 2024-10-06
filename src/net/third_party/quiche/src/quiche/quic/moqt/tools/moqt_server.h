// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_SERVER_H_

#define QUICHE_QUIC_MOQT_TOOLS_MOQT_SERVER_H_

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/tools/quic_server.h"
#include "quiche/quic/tools/web_transport_only_backend.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace moqt {

// A callback to configure an already created MoQT session.
using MoqtConfigureSessionCallback =
    quiche::SingleUseCallback<void(MoqtSession* session)>;

// A callback to provide MoQT handler based on the path in the request.
using MoqtIncomingSessionCallback =
    quiche::MultiUseCallback<absl::StatusOr<MoqtConfigureSessionCallback>(
        absl::string_view path)>;

// A simple MoQT server.
class QUICHE_EXPORT MoqtServer {
 public:
  explicit MoqtServer(std::unique_ptr<quic::ProofSource> proof_source,
                      MoqtIncomingSessionCallback callback);

  quic::QuicServer& quic_server() { return server_; }

 private:
  quic::WebTransportOnlyBackend backend_;
  quic::QuicServer server_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_SERVER_H_
