// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_
#define QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_

#include "quic/core/quic_types.h"
#include "quic/core/quic_udp_socket.h"
#include "quic/masque/masque_compression_engine.h"
#include "quic/masque/masque_server_backend.h"
#include "quic/masque/masque_utils.h"
#include "quic/platform/api/quic_epoll.h"
#include "quic/platform/api/quic_export.h"
#include "quic/tools/quic_simple_server_session.h"

namespace quic {

// QUIC server session for connection to MASQUE proxy.
class QUIC_NO_EXPORT MasqueServerSession
    : public QuicSimpleServerSession,
      public MasqueServerBackend::BackendClient,
      public QuicEpollCallbackInterface {
 public:
  // Interface meant to be implemented by owner of this MasqueServerSession
  // instance.
  class QUIC_NO_EXPORT Visitor {
   public:
    virtual ~Visitor() {}
    // Register a client connection ID as being handled by this session.
    virtual void RegisterClientConnectionId(
        QuicConnectionId client_connection_id,
        MasqueServerSession* masque_server_session) = 0;

    // Unregister a client connection ID.
    virtual void UnregisterClientConnectionId(
        QuicConnectionId client_connection_id) = 0;
  };

  explicit MasqueServerSession(
      MasqueMode masque_mode, const QuicConfig& config,
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection, QuicSession::Visitor* visitor, Visitor* owner,
      QuicEpollServer* epoll_server, QuicCryptoServerStreamBase::Helper* helper,
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      MasqueServerBackend* masque_server_backend);

  // Disallow copy and assign.
  MasqueServerSession(const MasqueServerSession&) = delete;
  MasqueServerSession& operator=(const MasqueServerSession&) = delete;

  // From QuicSession.
  void OnMessageReceived(absl::string_view message) override;
  void OnMessageAcked(QuicMessageId message_id,
                      QuicTime receive_timestamp) override;
  void OnMessageLost(QuicMessageId message_id) override;
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;
  void OnStreamClosed(QuicStreamId stream_id) override;

  // From MasqueServerBackend::BackendClient.
  std::unique_ptr<QuicBackendResponse> HandleMasqueRequest(
      const std::string& masque_path,
      const spdy::Http2HeaderBlock& request_headers,
      const std::string& request_body,
      QuicSimpleServerBackend::RequestHandler* request_handler) override;

  // From QuicEpollCallbackInterface.
  void OnRegistration(QuicEpollServer* eps, QuicUdpSocketFd fd,
                      int event_mask) override;
  void OnModification(QuicUdpSocketFd fd, int event_mask) override;
  void OnEvent(QuicUdpSocketFd fd, QuicEpollEvent* event) override;
  void OnUnregistration(QuicUdpSocketFd fd, bool replaced) override;
  void OnShutdown(QuicEpollServer* eps, QuicUdpSocketFd fd) override;
  std::string Name() const override;

  // Handle packet for client, meant to be called by MasqueDispatcher.
  void HandlePacketFromServer(const ReceivedPacketInfo& packet_info);

  QuicEpollServer* epoll_server() const { return epoll_server_; }

 private:
  // State that the MasqueServerSession keeps for each CONNECT-UDP request.
  class QUIC_NO_EXPORT ConnectUdpServerState
      : public QuicSpdyStream::Http3DatagramRegistrationVisitor,
        public QuicSpdyStream::Http3DatagramVisitor {
   public:
    // ConnectUdpServerState takes ownership of |fd|. It will unregister it
    // from |epoll_server| and close the file descriptor when destructed.
    explicit ConnectUdpServerState(
        QuicSpdyStream* stream,
        absl::optional<QuicDatagramContextId> context_id,
        const QuicSocketAddress& target_server_address, QuicUdpSocketFd fd,
        MasqueServerSession* masque_session);

    ~ConnectUdpServerState();

    // Disallow copy but allow move.
    ConnectUdpServerState(const ConnectUdpServerState&) = delete;
    ConnectUdpServerState(ConnectUdpServerState&&);
    ConnectUdpServerState& operator=(const ConnectUdpServerState&) = delete;
    ConnectUdpServerState& operator=(ConnectUdpServerState&&);

    QuicSpdyStream* stream() const { return stream_; }
    absl::optional<QuicDatagramContextId> context_id() const {
      return context_id_;
    }
    const QuicSocketAddress& target_server_address() const {
      return target_server_address_;
    }
    QuicUdpSocketFd fd() const { return fd_; }

    // From QuicSpdyStream::Http3DatagramVisitor.
    void OnHttp3Datagram(QuicStreamId stream_id,
                         absl::optional<QuicDatagramContextId> context_id,
                         absl::string_view payload) override;

    // From QuicSpdyStream::Http3DatagramRegistrationVisitor.
    void OnContextReceived(
        QuicStreamId stream_id,
        absl::optional<QuicDatagramContextId> context_id,
        const Http3DatagramContextExtensions& extensions) override;
    void OnContextClosed(
        QuicStreamId stream_id,
        absl::optional<QuicDatagramContextId> context_id,
        const Http3DatagramContextExtensions& extensions) override;

   private:
    QuicSpdyStream* stream_;
    absl::optional<QuicDatagramContextId> context_id_;
    QuicSocketAddress target_server_address_;
    QuicUdpSocketFd fd_;                   // Owned.
    MasqueServerSession* masque_session_;  // Unowned.
    bool context_received_ = false;
    bool context_registered_ = false;
  };

  bool ShouldNegotiateHttp3Datagram() override { return true; }

  MasqueServerBackend* masque_server_backend_;  // Unowned.
  Visitor* owner_;                              // Unowned.
  QuicEpollServer* epoll_server_;               // Unowned.
  MasqueCompressionEngine compression_engine_;
  MasqueMode masque_mode_;
  std::list<ConnectUdpServerState> connect_udp_server_states_;
  bool masque_initialized_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_
