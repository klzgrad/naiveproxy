// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_QUARTC_DISPATCHER_H_
#define QUICHE_QUIC_QUARTC_QUARTC_DISPATCHER_H_

#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quiche/src/quic/core/quic_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_dispatcher.h"
#include "net/third_party/quiche/src/quic/core/quic_version_manager.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_session.h"

namespace quic {

class QuartcDispatcher : public QuicDispatcher,
                         QuartcPacketTransport::Delegate {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnSessionCreated(QuartcSession* session) = 0;
  };

  QuartcDispatcher(
      std::unique_ptr<QuicConfig> config,
      std::unique_ptr<QuicCryptoServerConfig> crypto_config,
      QuicVersionManager* version_manager,
      std::unique_ptr<QuicConnectionHelperInterface> helper,
      std::unique_ptr<QuicCryptoServerStream::Helper> session_helper,
      std::unique_ptr<QuicAlarmFactory> alarm_factory,
      std::unique_ptr<QuartcPacketWriter> packet_writer,
      Delegate* delegate);
  ~QuartcDispatcher() override;

  QuartcSession* CreateQuicSession(QuicConnectionId server_connection_id,
                                   const QuicSocketAddress& client_address,
                                   QuicStringPiece alpn,
                                   const ParsedQuicVersion& version) override;

  // TODO(b/124399417): Override GenerateNewServerConnectionId and request a
  // zero-length connection id when the QUIC server perspective supports it.

  // QuartcPacketTransport::Delegate overrides.
  void OnTransportCanWrite() override;
  void OnTransportReceived(const char* data, size_t data_len) override;

 private:
  // Members owned by QuartcDispatcher but not QuicDispatcher.
  std::unique_ptr<QuicConfig> owned_quic_config_;
  std::unique_ptr<QuicCryptoServerConfig> owned_crypto_config_;

  // Delegate invoked when the dispatcher creates a new session.
  Delegate* delegate_;

  // The packet writer used by this dispatcher.  Owned by the base class, but
  // the base class upcasts it to QuicPacketWriter (which prevents detaching the
  // transport delegate without a downcast).
  QuartcPacketWriter* packet_writer_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_QUARTC_DISPATCHER_H_
