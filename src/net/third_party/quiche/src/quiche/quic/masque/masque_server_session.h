// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_
#define QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_

#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_udp_socket.h"
#include "quiche/quic/masque/masque_server_backend.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/tools/quic_simple_server_session.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

// QUIC server session for connection to MASQUE proxy.
class QUIC_NO_EXPORT MasqueServerSession
    : public QuicSimpleServerSession,
      public MasqueServerBackend::BackendClient,
      public QuicSocketEventListener {
 public:
  explicit MasqueServerSession(
      MasqueMode masque_mode, const QuicConfig& config,
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection, QuicSession::Visitor* visitor,
      QuicEventLoop* event_loop, QuicCryptoServerStreamBase::Helper* helper,
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      MasqueServerBackend* masque_server_backend);

  // Disallow copy and assign.
  MasqueServerSession(const MasqueServerSession&) = delete;
  MasqueServerSession& operator=(const MasqueServerSession&) = delete;

  // From QuicSession.
  void OnMessageAcked(QuicMessageId message_id,
                      QuicTime receive_timestamp) override;
  void OnMessageLost(QuicMessageId message_id) override;
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;
  void OnStreamClosed(QuicStreamId stream_id) override;

  // From MasqueServerBackend::BackendClient.
  std::unique_ptr<QuicBackendResponse> HandleMasqueRequest(
      const spdy::Http2HeaderBlock& request_headers,
      QuicSimpleServerBackend::RequestHandler* request_handler) override;

  // From QuicSocketEventListener.
  void OnSocketEvent(QuicEventLoop* event_loop, QuicUdpSocketFd fd,
                     QuicSocketEventMask events) override;

  QuicEventLoop* event_loop() const { return event_loop_; }

 private:
  // State that the MasqueServerSession keeps for each CONNECT-UDP request.
  class QUIC_NO_EXPORT ConnectUdpServerState
      : public QuicSpdyStream::Http3DatagramVisitor {
   public:
    // ConnectUdpServerState takes ownership of |fd|. It will unregister it
    // from |event_loop| and close the file descriptor when destructed.
    explicit ConnectUdpServerState(
        QuicSpdyStream* stream, const QuicSocketAddress& target_server_address,
        QuicUdpSocketFd fd, MasqueServerSession* masque_session);

    ~ConnectUdpServerState();

    // Disallow copy but allow move.
    ConnectUdpServerState(const ConnectUdpServerState&) = delete;
    ConnectUdpServerState(ConnectUdpServerState&&);
    ConnectUdpServerState& operator=(const ConnectUdpServerState&) = delete;
    ConnectUdpServerState& operator=(ConnectUdpServerState&&);

    QuicSpdyStream* stream() const { return stream_; }
    const QuicSocketAddress& target_server_address() const {
      return target_server_address_;
    }
    QuicUdpSocketFd fd() const { return fd_; }

    // From QuicSpdyStream::Http3DatagramVisitor.
    void OnHttp3Datagram(QuicStreamId stream_id,
                         absl::string_view payload) override;

   private:
    QuicSpdyStream* stream_;
    QuicSocketAddress target_server_address_;
    QuicUdpSocketFd fd_;                   // Owned.
    MasqueServerSession* masque_session_;  // Unowned.
  };

  // From QuicSpdySession.
  bool OnSettingsFrame(const SettingsFrame& frame) override;
  HttpDatagramSupport LocalHttpDatagramSupport() override {
    return HttpDatagramSupport::kDraft09;
  }

  MasqueServerBackend* masque_server_backend_;  // Unowned.
  QuicEventLoop* event_loop_;                   // Unowned.
  MasqueMode masque_mode_;
  std::list<ConnectUdpServerState> connect_udp_server_states_;
  bool masque_initialized_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_
