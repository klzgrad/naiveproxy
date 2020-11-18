// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_EPOLL_SERVER_FACTORY_H_
#define QUICHE_QUIC_TOOLS_EPOLL_SERVER_FACTORY_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_epoll.h"
#include "net/third_party/quiche/src/quic/tools/quic_toy_server.h"

namespace quic {

// Factory creating QuicServer instances.
class QuicEpollServerFactory : public QuicToyServer::ServerFactory {
 public:
  std::unique_ptr<QuicSpdyServerBase> CreateServer(
      QuicSimpleServerBackend* backend,
      std::unique_ptr<ProofSource> proof_source,
      const quic::ParsedQuicVersionVector& supported_versions) override;

 private:
  QuicEpollServer epoll_server_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_EPOLL_SERVER_FACTORY_H_
