// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_
#define QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_

#include <list>
#include <memory>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/frames/quic_connection_close_frame.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_udp_socket.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/masque/masque_server_backend.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_backend_response.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/quic/tools/quic_simple_server_session.h"
#include "quiche/common/capsule.h"
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
  bool HandleConnectUdpSocketEvent(QuicUdpSocketFd fd,
                                   QuicSocketEventMask events);
  bool HandleConnectIpSocketEvent(QuicUdpSocketFd fd,
                                  QuicSocketEventMask events);
  bool HandleConnectEthernetSocketEvent(QuicUdpSocketFd fd,
                                        QuicSocketEventMask events);
  std::unique_ptr<QuicBackendResponse> MaybeCheckSignatureAuth(
      const spdy::Http2HeaderBlock& request_headers,
      absl::string_view authority, absl::string_view scheme,
      QuicSimpleServerBackend::RequestHandler* request_handler);

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
    void OnUnknownCapsule(QuicStreamId /*stream_id*/,
                          const quiche::UnknownCapsule& /*capsule*/) override {}

   private:
    QuicSpdyStream* stream_;
    QuicSocketAddress target_server_address_;
    QuicUdpSocketFd fd_;                   // Owned.
    MasqueServerSession* masque_session_;  // Unowned.
  };

  // State that the MasqueServerSession keeps for each CONNECT-IP request.
  class QUIC_NO_EXPORT ConnectIpServerState
      : public QuicSpdyStream::Http3DatagramVisitor,
        public QuicSpdyStream::ConnectIpVisitor {
   public:
    // ConnectIpServerState takes ownership of |fd|. It will unregister it
    // from |event_loop| and close the file descriptor when destructed.
    explicit ConnectIpServerState(QuicIpAddress client_ip,
                                  QuicSpdyStream* stream, QuicUdpSocketFd fd,
                                  MasqueServerSession* masque_session);

    ~ConnectIpServerState();

    // Disallow copy but allow move.
    ConnectIpServerState(const ConnectIpServerState&) = delete;
    ConnectIpServerState(ConnectIpServerState&&);
    ConnectIpServerState& operator=(const ConnectIpServerState&) = delete;
    ConnectIpServerState& operator=(ConnectIpServerState&&);

    QuicSpdyStream* stream() const { return stream_; }
    QuicUdpSocketFd fd() const { return fd_; }

    // From QuicSpdyStream::Http3DatagramVisitor.
    void OnHttp3Datagram(QuicStreamId stream_id,
                         absl::string_view payload) override;
    void OnUnknownCapsule(QuicStreamId /*stream_id*/,
                          const quiche::UnknownCapsule& /*capsule*/) override {}

    // From QuicSpdyStream::ConnectIpVisitor.
    bool OnAddressAssignCapsule(
        const quiche::AddressAssignCapsule& capsule) override;
    bool OnAddressRequestCapsule(
        const quiche::AddressRequestCapsule& capsule) override;
    bool OnRouteAdvertisementCapsule(
        const quiche::RouteAdvertisementCapsule& capsule) override;
    void OnHeadersWritten() override;

   private:
    QuicIpAddress client_ip_;
    QuicSpdyStream* stream_;
    QuicUdpSocketFd fd_;                   // Owned.
    MasqueServerSession* masque_session_;  // Unowned.
  };

  // State that the MasqueServerSession keeps for each CONNECT-ETHERNET request.
  class QUIC_NO_EXPORT ConnectEthernetServerState
      : public QuicSpdyStream::Http3DatagramVisitor {
   public:
    // ConnectEthernetServerState takes ownership of |fd|. It will unregister it
    // from |event_loop| and close the file descriptor when destructed.
    explicit ConnectEthernetServerState(QuicSpdyStream* stream,
                                        QuicUdpSocketFd fd,
                                        MasqueServerSession* masque_session);

    ~ConnectEthernetServerState();

    // Disallow copy but allow move.
    ConnectEthernetServerState(const ConnectEthernetServerState&) = delete;
    ConnectEthernetServerState(ConnectEthernetServerState&&);
    ConnectEthernetServerState& operator=(const ConnectEthernetServerState&) =
        delete;
    ConnectEthernetServerState& operator=(ConnectEthernetServerState&&);

    QuicSpdyStream* stream() const { return stream_; }
    QuicUdpSocketFd fd() const { return fd_; }

    // From QuicSpdyStream::Http3DatagramVisitor.
    void OnHttp3Datagram(QuicStreamId stream_id,
                         absl::string_view payload) override;
    void OnUnknownCapsule(QuicStreamId /*stream_id*/,
                          const quiche::UnknownCapsule& /*capsule*/) override {}

   private:
    QuicSpdyStream* stream_;
    QuicUdpSocketFd fd_;                   // Owned.
    MasqueServerSession* masque_session_;  // Unowned.
  };

  // From QuicSpdySession.
  bool OnSettingsFrame(const SettingsFrame& frame) override;
  HttpDatagramSupport LocalHttpDatagramSupport() override {
    return HttpDatagramSupport::kRfc;
  }

  MasqueServerBackend* masque_server_backend_;  // Unowned.
  QuicEventLoop* event_loop_;                   // Unowned.
  MasqueMode masque_mode_;
  std::list<ConnectUdpServerState> connect_udp_server_states_;
  std::list<ConnectIpServerState> connect_ip_server_states_;
  std::list<ConnectEthernetServerState> connect_ethernet_server_states_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_
