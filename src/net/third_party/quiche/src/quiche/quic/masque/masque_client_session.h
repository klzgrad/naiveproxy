// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_CLIENT_SESSION_H_
#define QUICHE_QUIC_MASQUE_MASQUE_CLIENT_SESSION_H_

#include <list>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/frames/quic_connection_close_frame.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/capsule.h"
#include "quiche/common/quiche_ip_address.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

// QUIC client session for connection to MASQUE proxy. This session establishes
// a connection to a MASQUE proxy and handles sending and receiving DATAGRAM
// frames for operation of the MASQUE protocol. Multiple end-to-end encapsulated
// sessions can then coexist inside this session. Once these are created, they
// need to be registered with this session.
class QUIC_NO_EXPORT MasqueClientSession : public QuicSpdyClientSession,
                                           public QuicSpdyStream::Visitor {
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

  // CONNECT-ETHERNET.
  class QUIC_NO_EXPORT EncapsulatedEthernetSession {
   public:
    virtual ~EncapsulatedEthernetSession() {}

    // Process packet that was just decapsulated. |frame| contains the
    // Ethernet header and payload.
    virtual void ProcessEthernetFrame(absl::string_view frame) = 0;

    // Close the encapsulated connection.
    virtual void CloseEthernetSession(const std::string& details) = 0;
  };

  // Constructor for when this is only an underlying session.
  // Takes ownership of |connection|, but not of |crypto_config| or |owner|.
  // All pointers must be non-null. Caller must ensure that |owner| stays valid
  // for the lifetime of the newly created MasqueClientSession.
  MasqueClientSession(MasqueMode masque_mode, const std::string& uri_template,
                      const QuicConfig& config,
                      const ParsedQuicVersionVector& supported_versions,
                      QuicConnection* connection, const QuicServerId& server_id,
                      QuicCryptoClientConfig* crypto_config, Owner* owner);

  // Constructor for when this is only an encapsulated session.
  MasqueClientSession(const QuicConfig& config,
                      const ParsedQuicVersionVector& supported_versions,
                      QuicConnection* connection, const QuicServerId& server_id,
                      QuicCryptoClientConfig* crypto_config, Owner* owner);

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

  // Send encapsulated Ethernet frame. |frame| contains the Ethernet
  // header and payload.
  void SendEthernetFrame(
      absl::string_view frame,
      EncapsulatedEthernetSession* encapsulated_ethernet_session);

  // Close CONNECT-UDP stream tied to this encapsulated client session.
  void CloseConnectUdpStream(
      EncapsulatedClientSession* encapsulated_client_session);

  // Close CONNECT-IP stream tied to this encapsulated client session.
  void CloseConnectIpStream(EncapsulatedIpSession* encapsulated_ip_session);

  // Close CONNECT-ETHERNET stream tied to this encapsulated client session.
  void CloseConnectEthernetStream(
      EncapsulatedEthernetSession* encapsulated_ethernet_session);

  // Generate a random Unique Local Address and register a mapping from
  // that address to the corresponding hostname. The returned address should be
  // removed by calling RemoveFakeAddress() once it is no longer needed.
  quiche::QuicheIpAddress GetFakeAddress(absl::string_view hostname);

  // Removes a fake address that was previously created by GetFakeAddress().
  void RemoveFakeAddress(const quiche::QuicheIpAddress& fake_address);

  // Set additional HTTP headers that will be sent on all requests to the MASQUE
  // proxy. Separated with colons and semicolons.
  // For example: "name1:value1;name2:value2".
  void set_additional_headers(absl::string_view additional_headers) {
    additional_headers_ = additional_headers;
  }

  // Send a GET request to the MASQUE proxy itself.
  QuicSpdyClientStream* SendGetRequest(absl::string_view path);

  // QuicSpdyStream::Visitor
  void OnClose(QuicSpdyStream* stream) override;

  // Set the signature auth key ID and private key. key_id MUST be non-empty,
  // private_key MUST be ED25519_PRIVATE_KEY_LEN bytes long and public_key MUST
  // be ED25519_PUBLIC_KEY_LEN bytes long.
  void EnableSignatureAuth(absl::string_view key_id,
                           absl::string_view private_key,
                           absl::string_view public_key);

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
    void OnUnknownCapsule(QuicStreamId /*stream_id*/,
                          const quiche::UnknownCapsule& /*capsule*/) override {}

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
    QuicSpdyClientStream* stream_;                    // Unowned.
    EncapsulatedIpSession* encapsulated_ip_session_;  // Unowned.
    MasqueClientSession* masque_session_;             // Unowned.
  };

  // State that the MasqueClientSession keeps for each CONNECT-ETHERNET request.
  class QUIC_NO_EXPORT ConnectEthernetClientState
      : public QuicSpdyStream::Http3DatagramVisitor {
   public:
    // |stream| and |encapsulated_client_session| must be valid for the lifetime
    // of the ConnectUdpClientState.
    explicit ConnectEthernetClientState(
        QuicSpdyClientStream* stream,
        EncapsulatedEthernetSession* encapsulated_ethernet_session,
        MasqueClientSession* masque_session);

    ~ConnectEthernetClientState();

    // Disallow copy but allow move.
    ConnectEthernetClientState(const ConnectEthernetClientState&) = delete;
    ConnectEthernetClientState(ConnectEthernetClientState&&);
    ConnectEthernetClientState& operator=(const ConnectEthernetClientState&) =
        delete;
    ConnectEthernetClientState& operator=(ConnectEthernetClientState&&);

    QuicSpdyClientStream* stream() const { return stream_; }
    EncapsulatedEthernetSession* encapsulated_ethernet_session() const {
      return encapsulated_ethernet_session_;
    }

    // From QuicSpdyStream::Http3DatagramVisitor.
    void OnHttp3Datagram(QuicStreamId stream_id,
                         absl::string_view payload) override;
    void OnUnknownCapsule(QuicStreamId /*stream_id*/,
                          const quiche::UnknownCapsule& /*capsule*/) override {}

   private:
    QuicSpdyClientStream* stream_;                                // Unowned.
    EncapsulatedEthernetSession* encapsulated_ethernet_session_;  // Unowned.
    MasqueClientSession* masque_session_;                         // Unowned.
  };

  HttpDatagramSupport LocalHttpDatagramSupport() override {
    return HttpDatagramSupport::kRfc;
  }

  const ConnectUdpClientState* GetOrCreateConnectUdpClientState(
      const QuicSocketAddress& target_server_address,
      EncapsulatedClientSession* encapsulated_client_session);

  const ConnectIpClientState* GetOrCreateConnectIpClientState(
      EncapsulatedIpSession* encapsulated_ip_session);

  const ConnectEthernetClientState* GetOrCreateConnectEthernetClientState(
      EncapsulatedEthernetSession* encapsulated_ethernet_session);

  std::optional<std::string> ComputeSignatureAuthHeader(const QuicUrl& url);
  void AddAdditionalHeaders(spdy::Http2HeaderBlock& headers,
                            const QuicUrl& url);

  MasqueMode masque_mode_;
  std::string uri_template_;
  std::string additional_headers_;
  std::string signature_auth_key_id_;
  std::string signature_auth_private_key_;
  std::string signature_auth_public_key_;
  std::list<ConnectUdpClientState> connect_udp_client_states_;
  std::list<ConnectIpClientState> connect_ip_client_states_;
  std::list<ConnectEthernetClientState> connect_ethernet_client_states_;
  // Maps fake addresses generated by GetFakeAddress() to their corresponding
  // hostnames.
  absl::flat_hash_map<std::string, std::string> fake_addresses_;
  Owner* owner_ = nullptr;  // Unowned;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_CLIENT_SESSION_H_
