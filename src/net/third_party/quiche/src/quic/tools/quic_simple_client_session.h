// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_session.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_client_stream.h"

namespace quic {

class QuicSimpleClientSession : public QuicSpdyClientSession {
 public:
  QuicSimpleClientSession(const QuicConfig& config,
                          const ParsedQuicVersionVector& supported_versions,
                          QuicConnection* connection,
                          const QuicServerId& server_id,
                          QuicCryptoClientConfig* crypto_config,
                          QuicClientPushPromiseIndex* push_promise_index,
                          bool drop_response_body);

  std::unique_ptr<QuicSpdyClientStream> CreateClientStream() override;

 private:
  const bool drop_response_body_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_
