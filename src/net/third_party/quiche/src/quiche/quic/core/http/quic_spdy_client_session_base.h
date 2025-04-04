// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_SESSION_BASE_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_SESSION_BASE_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/quic_crypto_client_stream.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/http/http_header_block.h"

namespace quic {

// Base class for all client-specific QuicSession subclasses.
class QUICHE_EXPORT QuicSpdyClientSessionBase
    : public QuicSpdySession,
      public QuicCryptoClientStream::ProofHandler {
 public:
  // Takes ownership of |connection|.
  QuicSpdyClientSessionBase(QuicConnection* connection,
                            QuicSession::Visitor* visitor,
                            const QuicConfig& config,
                            const ParsedQuicVersionVector& supported_versions);
  QuicSpdyClientSessionBase(const QuicSpdyClientSessionBase&) = delete;
  QuicSpdyClientSessionBase& operator=(const QuicSpdyClientSessionBase&) =
      delete;

  ~QuicSpdyClientSessionBase() override;

  void OnConfigNegotiated() override;

  // Release headers stream's sequencer buffer if it's empty.
  void OnStreamClosed(QuicStreamId stream_id) override;

  // Returns true if there are no active requests.
  bool ShouldReleaseHeadersStreamSequencerBuffer() override;

  // Override to wait for all received responses to be consumed by application.
  bool ShouldKeepConnectionAlive() const override;

  // Override to serialize the settings and pass it down to the handshaker.
  bool OnSettingsFrame(const SettingsFrame& frame) override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_SESSION_BASE_H_
