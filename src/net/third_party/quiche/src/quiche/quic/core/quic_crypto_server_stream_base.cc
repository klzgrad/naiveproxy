// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_crypto_server_stream_base.h"

#include <memory>
#include <string>
#include <utility>

#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/crypto/crypto_utils.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/proto/cached_network_parameters_proto.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_crypto_server_stream.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/tls_server_handshaker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

QuicCryptoServerStreamBase::QuicCryptoServerStreamBase(QuicSession* session)
    : QuicCryptoStream(session) {}

std::unique_ptr<QuicCryptoServerStreamBase> CreateCryptoServerStream(
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache, QuicSession* session,
    QuicCryptoServerStreamBase::Helper* helper) {
  switch (session->connection()->version().handshake_protocol) {
    case PROTOCOL_QUIC_CRYPTO:
      return std::unique_ptr<QuicCryptoServerStream>(new QuicCryptoServerStream(
          crypto_config, compressed_certs_cache, session, helper));
    case PROTOCOL_TLS1_3:
      return std::unique_ptr<TlsServerHandshaker>(
          new TlsServerHandshaker(session, crypto_config));
    case PROTOCOL_UNSUPPORTED:
      break;
  }
  QUIC_BUG(quic_bug_10492_1)
      << "Unknown handshake protocol: "
      << static_cast<int>(session->connection()->version().handshake_protocol);
  return nullptr;
}

}  // namespace quic
