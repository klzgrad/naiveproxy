// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_CLIENT_SESSION_H_
#define QUICHE_QUIC_MASQUE_MASQUE_CLIENT_SESSION_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace quic {

// QUIC client session for connection to MASQUE proxy. This session establishes
// a connection to a MASQUE proxy and handles sending and receiving DATAGRAM
// frames for operation of the MASQUE protocol. Multiple end-to-end encapsulated
// sessions can then coexist inside this session. Once these are created, they
// need to be registered with this session.
class QUIC_NO_EXPORT MasqueClientSession : public QuicSpdyClientSession {
 public:
  // Interface meant to be implemented by the owner of the
  // MasqueClientSession instance.
  class QUIC_NO_EXPORT Owner {
   public:
    virtual ~Owner() {}

    // Notifies the owner that a settings frame has been received.
    virtual void OnSettingsReceived() = 0;
  };

  // Interface meant to be implemented by client sessions encapsulated inside
  // CONNECT-UDP, i.e. the end-to-end QUIC client sessions that run inside
  // CONNECT-UDP encapsulation.
  class QUIC_NO_EXPORT EncapsulatedClientSession {
   public:
    virtual ~EncapsulatedClientSession() {}

    // Process UDP packet that was just decapsulated. |packet| contains the UDP
    // payload.
    virtual void ProcessPacket(absl::string_view packet,
                               QuicSocketAddress target_server_address) = 0;

    // Close the encapsulated connection.
    virtual void CloseConnection(
        QuicErrorCode error, const std::string& details,
        ConnectionCloseBehavior connection_close_behavior) = 0;
  };

  // Interface meant to be implemented by client sessions encapsulated inside
  // CONNECT-IP, i.e. the end-to-end QUIC client sessions that run inside
  // CONNECT-IP encapsulation.
  class QUIC_NO_EXPORT EncapsulatedIpSession {
   public:
    virtual ~EncapsulatedIpSession() {}

    // Process packet that was just decapsulated. |packet| contains the IP
    // header and payload.
    virtual void ProcessIpPacket(absl::string_view packet) = 0;

    // Close the encapsulated connection.
    virtual void CloseIpSession(const std::string& details) = 0;

    virtual bool OnAddressAssignCapsule(
        const quiche::AddressAssignCapsule& capsule) = 0;
    virtual bool OnAddressRequestCapsule(
        const quiche::AddressRequestCapsule& capsule) = 0;
    virtual bool OnRouteAdvertisementCapsule(
        const quiche::RouteAdvertisementCapsule& capsule) = 0;
  };

  // Takes ownership of |connection|, but not of |crypto_config| or
  // |push_promise_index| or |owner|. All pointers must be non-null. Caller
  // must ensure that |push_promise_index| and |owner| stay valid for the
  // lifetime of the newly created MasqueClientSession.
  MasqueClientSession(MasqueMode masque_mode, const std::string& uri_template,
                      const QuicConfig& config,
                      const ParsedQuicVersionVector& supported_versions,
                      QuicConnection* connection, const QuicServerId& server_id,
                      QuicCryptoClientConfig* crypto_config,
                      QuicClientPushPromiseIndex* push_promise_index,
                      Owner* owner);

  // Disallow copy and assign.
  MasqueClientSession(const MasqueClientSession&) = delete;
  MasqueClientSession& operator=(const MasqueClientSession&) = delete;

  // From QuicSession.
  void OnMessageAcked(QuicMessageId message_id,
                      QuicTime receive_timestamp) override;
  void OnMessageLost(QuicMessageId message_id) override;
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;
  void OnStreamClosed(QuicStreamId stream_id) override;

  // From QuicSpdySession.
  bool OnSettingsFrame(const SettingsFrame& frame) override;

  // Send encapsulated UDP packet. |packet| contains the UDP payload.
  void SendPacket(absl::string_view packet,
                  const QuicSocketAddress& target_server_address,
                  EncapsulatedClientSession* encapsulated_client_session);

  // Send encapsulated IP packet. |packet| contains the IP header and payload.
  void SendIpPacket(absl::string_view packet,
                    EncapsulatedIpSession* encapsulated_ip_session);

  // Close CONNECT-UDP stream tied to this encapsulated client session.
  void CloseConnectUdpStream(
      EncapsulatedClientSession* encapsulated_client_session);

  // Close CONNECT-IP stream tied to this encapsulated client session.
  void CloseConnectIpStream(EncapsulatedIpSession* encapsulated_ip_session);

 private:
  // State that the MasqueClientSession keeps for each CONNECT-UDP request.
  class QUIC_NO_EXPORT ConnectUdpClientState
      : public QuicSpdyStream::Http3DatagramVisitor {
   public:
    // |stream| and |encapsulated_client_session| must be valid for the lifetime
    // of the ConnectUdpClientState.
    explicit ConnectUdpClientState(
        QuicSpdyClientStream* stream,
        EncapsulatedClientSession* encapsulated_client_session,
        MasqueClientSession* masque_session,
        const QuicSocketAddress& target_server_address);

    ~ConnectUdpClientState();

    // Disallow copy but allow move.
    ConnectUdpClientState(const ConnectUdpClientState&) = delete;
    ConnectUdpClientState(ConnectUdpClientState&&);
    ConnectUdpClientState& operator=(const ConnectUdpClientState&) = delete;
    ConnectUdpClientState& operator=(ConnectUdpClientState&&);

    QuicSpdyClientStream* stream() const { return stream_; }
    EncapsulatedClientSession* encapsulated_client_session() const {
      return encapsulated_client_session_;
    }
    const QuicSocketAddress& target_server_address() const {
      return target_server_address_;
    }

    // From QuicSpdyStream::Http3DatagramVisitor.
    void OnHttp3Datagram(QuicStreamId stream_id,
                         absl::string_view payload) override;

   private:
    QuicSpdyClientStream* stream_;                            // Unowned.
    EncapsulatedClientSession* encapsulated_client_session_;  // Unowned.
    MasqueClientSession* masque_session_;                     // Unowned.
    QuicSocketAddress target_server_address_;
  };

  // State that the MasqueClientSession keeps for each CONNECT-IP request.
  class QUIC_NO_EXPORT ConnectIpClientState
      : public QuicSpdyStream::Http3DatagramVisitor,
        public QuicSpdyStream::ConnectIpVisitor {
   public:
    // |stream| and |encapsulated_client_session| must be valid for the lifetime
    // of the ConnectUdpClientState.
    explicit ConnectIpClientState(
        QuicSpdyClientStream* stream,
        EncapsulatedIpSession* encapsulated_ip_session,
        MasqueClientSession* masque_session);

    ~ConnectIpClientState();

    // Disallow copy but allow move.
    ConnectIpClientState(const ConnectIpClientState&) = delete;
    ConnectIpClientState(ConnectIpClientState&&);
    ConnectIpClientState& operator=(const ConnectIpClientState&) = delete;
    ConnectIpClientState& operator=(ConnectIpClientState&&);

    QuicSpdyClientStream* stream() const { return stream_; }
    EncapsulatedIpSession* encapsulated_ip_session() const {
      return encapsulated_ip_session_;
    }

    // From QuicSpdyStream::Http3DatagramVisitor.
    void OnHttp3Datagram(QuicStreamId stream_id,
                         absl::string_view payload) override;

    // From QuicSpdyStream::ConnectIpVisitor.
    bool OnAddressAssignCapsule(
        const quiche::AddressAssignCapsule& capsule) override;
    bool OnAddressRequestCapsule(
        const quiche::AddressRequestCapsule& capsule) override;
    bool OnRouteAdvertisementCapsule(
        const quiche::RouteAdvertisementCapsule& capsule) override;
    void OnHeadersWritten() override;

   private:
    QuicSpdyClientStream* stream_;                    // Unowned.
    EncapsulatedIpSession* encapsulated_ip_session_;  // Unowned.
    MasqueClientSession* masque_session_;             // Unowned.
  };

  HttpDatagramSupport LocalHttpDatagramSupport() override {
    return HttpDatagramSupport::kRfc;
  }

  const ConnectUdpClientState* GetOrCreateConnectUdpClientState(
      const QuicSocketAddress& target_server_address,
      EncapsulatedClientSession* encapsulated_client_session);

  const ConnectIpClientState* GetOrCreateConnectIpClientState(
      EncapsulatedIpSession* encapsulated_ip_session);

  MasqueMode masque_mode_;
  std::string uri_template_;
  std::list<ConnectUdpClientState> connect_udp_client_states_;
  std::list<ConnectIpClientState> connect_ip_client_states_;
  Owner* owner_;  // Unowned;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_CLIENT_SESSION_H_
