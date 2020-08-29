// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/tools/quic_epoll_server_factory.h"

#include <utility>

#include "net/third_party/quiche/src/quic/tools/quic_server.h"

namespace quic {

std::unique_ptr<quic::QuicSpdyServerBase> QuicEpollServerFactory::CreateServer(
    quic::QuicSimpleServerBackend* backend,
    std::unique_ptr<quic::ProofSource> proof_source,
    const quic::ParsedQuicVersionVector& supported_versions) {
  return std::make_unique<quic::QuicServer>(std::move(proof_source), backend,
                                            supported_versions);
}

}  // namespace quic
