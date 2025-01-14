// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CONFIG_H_
#define QUICHE_QUIC_CORE_QUIC_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "quiche/quic/core/crypto/transport_parameters.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class QuicConfigPeer;
}  // namespace test

class CryptoHandshakeMessage;

// Describes whether or not a given QuicTag is required or optional in the
// handshake message.
enum QuicConfigPresence : uint8_t {
  // This negotiable value can be absent from the handshake message. Default
  // value is selected as the negotiated value in such a case.
  PRESENCE_OPTIONAL,
  // This negotiable value is required in the handshake message otherwise the
  // Process*Hello function returns an error.
  PRESENCE_REQUIRED,
};

// Whether the CryptoHandshakeMessage is from the client or server.
enum HelloType {
  CLIENT,
  SERVER,
};

// An abstract base class that stores a value that can be sent in CHLO/SHLO
// message. These values can be OPTIONAL or REQUIRED, depending on |presence_|.
class QUICHE_EXPORT QuicConfigValue {
 public:
  QuicConfigValue(QuicTag tag, QuicConfigPresence presence);
  virtual ~QuicConfigValue();

  // Serialises tag name and value(s) to |out|.
  virtual void ToHandshakeMessage(CryptoHandshakeMessage* out) const = 0;

  // Selects a mutually acceptable value from those offered in |peer_hello|
  // and those defined in the subclass.
  virtual QuicErrorCode ProcessPeerHello(
      const CryptoHandshakeMessage& peer_hello, HelloType hello_type,
      std::string* error_details) = 0;

 protected:
  const QuicTag tag_;
  const QuicConfigPresence presence_;
};

// Stores uint32_t from CHLO or SHLO messages that are not negotiated.
class QUICHE_EXPORT QuicFixedUint32 : public QuicConfigValue {
 public:
  QuicFixedUint32(QuicTag tag, QuicConfigPresence presence);
  ~QuicFixedUint32() override;

  bool HasSendValue() const;

  uint32_t GetSendValue() const;

  void SetSendValue(uint32_t value);

  bool HasReceivedValue() const;

  uint32_t GetReceivedValue() const;

  void SetReceivedValue(uint32_t value);

  // If has_send_value is true, serialises |tag_| and |send_value_| to |out|.
  void ToHandshakeMessage(CryptoHandshakeMessage* out) const override;

  // Sets |value_| to the corresponding value from |peer_hello_| if it exists.
  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 std::string* error_details) override;

 private:
  bool has_send_value_;
  bool has_receive_value_;
  uint32_t send_value_;
  uint32_t receive_value_;
};

// Stores 62bit numbers from handshake messages that unilaterally shared by each
// endpoint. IMPORTANT: these are serialized as 32-bit unsigned integers when
// using QUIC_CRYPTO versions and CryptoHandshakeMessage.
class QUICHE_EXPORT QuicFixedUint62 : public QuicConfigValue {
 public:
  QuicFixedUint62(QuicTag name, QuicConfigPresence presence);
  ~QuicFixedUint62() override;

  bool HasSendValue() const;

  uint64_t GetSendValue() const;

  void SetSendValue(uint64_t value);

  bool HasReceivedValue() const;

  uint64_t GetReceivedValue() const;

  void SetReceivedValue(uint64_t value);

  // If has_send_value is true, serialises |tag_| and |send_value_| to |out|.
  // IMPORTANT: this method serializes |send_value_| as an unsigned 32bit
  // integer.
  void ToHandshakeMessage(CryptoHandshakeMessage* out) const override;

  // Sets |value_| to the corresponding value from |peer_hello_| if it exists.
  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 std::string* error_details) override;

 private:
  bool has_send_value_;
  bool has_receive_value_;
  uint64_t send_value_;
  uint64_t receive_value_;
};

// Stores StatelessResetToken from CHLO or SHLO messages that are not
// negotiated.
class QUICHE_EXPORT QuicFixedStatelessResetToken : public QuicConfigValue {
 public:
  QuicFixedStatelessResetToken(QuicTag tag, QuicConfigPresence presence);
  ~QuicFixedStatelessResetToken() override;

  bool HasSendValue() const;

  const StatelessResetToken& GetSendValue() const;

  void SetSendValue(const StatelessResetToken& value);

  bool HasReceivedValue() const;

  const StatelessResetToken& GetReceivedValue() const;

  void SetReceivedValue(const StatelessResetToken& value);

  // If has_send_value is true, serialises |tag_| and |send_value_| to |out|.
  void ToHandshakeMessage(CryptoHandshakeMessage* out) const override;

  // Sets |value_| to the corresponding value from |peer_hello_| if it exists.
  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 std::string* error_details) override;

 private:
  bool has_send_value_;
  bool has_receive_value_;
  StatelessResetToken send_value_;
  StatelessResetToken receive_value_;
};

// Stores tag from CHLO or SHLO messages that are not negotiated.
class QUICHE_EXPORT QuicFixedTagVector : public QuicConfigValue {
 public:
  QuicFixedTagVector(QuicTag name, QuicConfigPresence presence);
  QuicFixedTagVector(const QuicFixedTagVector& other);
  ~QuicFixedTagVector() override;

  bool HasSendValues() const;

  const QuicTagVector& GetSendValues() const;

  void SetSendValues(const QuicTagVector& values);

  bool HasReceivedValues() const;

  const QuicTagVector& GetReceivedValues() const;

  void SetReceivedValues(const QuicTagVector& values);

  // If has_send_value is true, serialises |tag_vector_| and |send_value_| to
  // |out|.
  void ToHandshakeMessage(CryptoHandshakeMessage* out) const override;

  // Sets |receive_values_| to the corresponding value from |client_hello_| if
  // it exists.
  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 std::string* error_details) override;

 private:
  bool has_send_values_;
  bool has_receive_values_;
  QuicTagVector send_values_;
  QuicTagVector receive_values_;
};

// Stores QuicSocketAddress from CHLO or SHLO messages that are not negotiated.
class QUICHE_EXPORT QuicFixedSocketAddress : public QuicConfigValue {
 public:
  QuicFixedSocketAddress(QuicTag tag, QuicConfigPresence presence);
  ~QuicFixedSocketAddress() override;

  bool HasSendValue() const;

  const QuicSocketAddress& GetSendValue() const;

  void SetSendValue(const QuicSocketAddress& value);

  void ClearSendValue();

  bool HasReceivedValue() const;

  const QuicSocketAddress& GetReceivedValue() const;

  void SetReceivedValue(const QuicSocketAddress& value);

  void ToHandshakeMessage(CryptoHandshakeMessage* out) const override;

  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 std::string* error_details) override;

 private:
  bool has_send_value_;
  bool has_receive_value_;
  QuicSocketAddress send_value_;
  QuicSocketAddress receive_value_;
};

// QuicConfig contains non-crypto configuration options that are negotiated in
// the crypto handshake.
class QUICHE_EXPORT QuicConfig {
 public:
  QuicConfig();
  QuicConfig(const QuicConfig& other);
  ~QuicConfig();

  void SetConnectionOptionsToSend(const QuicTagVector& connection_options);

  void AddConnectionOptionsToSend(const QuicTagVector& connection_options);

  bool HasReceivedConnectionOptions() const;

  // Sets the data length to be sent for transport parameter 'discard'. The data
  // to send in the transport parameter will be all zeros. Negative values means
  // do not send.
  void SetDiscardLengthToSend(int32_t discard_length);

  int32_t GetDiscardLengthReceived() const { return discard_length_received_; }

  void SetGoogleHandshakeMessageToSend(std::string message);

  const std::optional<std::string>& GetReceivedGoogleHandshakeMessage() const;

  // Sets initial received connection options.  All received connection options
  // will be initialized with these fields. Initial received options may only be
  // set once per config, prior to the setting of any other options.  If options
  // have already been set (either by previous calls or via handshake), this
  // function does nothing and returns false.
  bool SetInitialReceivedConnectionOptions(const QuicTagVector& tags);

  const QuicTagVector& ReceivedConnectionOptions() const;

  bool HasSendConnectionOptions() const;

  const QuicTagVector& SendConnectionOptions() const;

  // Returns true if the client is sending or the server has received a
  // connection option.
  // TODO(ianswett): Rename to HasClientRequestedSharedOption
  bool HasClientSentConnectionOption(QuicTag tag,
                                     Perspective perspective) const;

  void SetClientConnectionOptions(
      const QuicTagVector& client_connection_options);

  // Returns true if the client has requested the specified connection option.
  // Checks the client connection options if the |perspective| is client and
  // connection options if the |perspective| is the server.
  bool HasClientRequestedIndependentOption(QuicTag tag,
                                           Perspective perspective) const;

  const QuicTagVector& ClientRequestedIndependentOptions(
      Perspective perspective) const;

  void SetIdleNetworkTimeout(QuicTime::Delta idle_network_timeout);

  QuicTime::Delta IdleNetworkTimeout() const;

  // Sets the max bidirectional stream count that this endpoint supports.
  void SetMaxBidirectionalStreamsToSend(uint32_t max_streams);
  uint32_t GetMaxBidirectionalStreamsToSend() const;

  bool HasReceivedMaxBidirectionalStreams() const;
  // Gets the max bidirectional stream limit imposed by the peer.
  uint32_t ReceivedMaxBidirectionalStreams() const;

  // Sets the max unidirectional stream count that this endpoint supports.
  void SetMaxUnidirectionalStreamsToSend(uint32_t max_streams);
  uint32_t GetMaxUnidirectionalStreamsToSend() const;

  bool HasReceivedMaxUnidirectionalStreams() const;
  // Gets the max unidirectional stream limit imposed by the peer.
  uint32_t ReceivedMaxUnidirectionalStreams() const;

  void set_max_time_before_crypto_handshake(
      QuicTime::Delta max_time_before_crypto_handshake) {
    max_time_before_crypto_handshake_ = max_time_before_crypto_handshake;
  }

  QuicTime::Delta max_time_before_crypto_handshake() const {
    return max_time_before_crypto_handshake_;
  }

  void set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta max_idle_time_before_crypto_handshake) {
    max_idle_time_before_crypto_handshake_ =
        max_idle_time_before_crypto_handshake;
  }

  QuicTime::Delta max_idle_time_before_crypto_handshake() const {
    return max_idle_time_before_crypto_handshake_;
  }

  void set_max_undecryptable_packets(size_t max_undecryptable_packets) {
    max_undecryptable_packets_ = max_undecryptable_packets;
  }

  size_t max_undecryptable_packets() const {
    return max_undecryptable_packets_;
  }

  // Peer's connection id length, in bytes. Only used in Q043 and Q046.
  bool HasSetBytesForConnectionIdToSend() const;
  void SetBytesForConnectionIdToSend(uint32_t bytes);
  bool HasReceivedBytesForConnectionId() const;
  uint32_t ReceivedBytesForConnectionId() const;

  // Estimated initial round trip time in us.
  void SetInitialRoundTripTimeUsToSend(uint64_t rtt_us);
  bool HasReceivedInitialRoundTripTimeUs() const;
  uint64_t ReceivedInitialRoundTripTimeUs() const;
  bool HasInitialRoundTripTimeUsToSend() const;
  uint64_t GetInitialRoundTripTimeUsToSend() const;

  // Sets an initial stream flow control window size to transmit to the peer.
  void SetInitialStreamFlowControlWindowToSend(uint64_t window_bytes);
  uint64_t GetInitialStreamFlowControlWindowToSend() const;
  bool HasReceivedInitialStreamFlowControlWindowBytes() const;
  uint64_t ReceivedInitialStreamFlowControlWindowBytes() const;

  // Specifies the initial flow control window (max stream data) for
  // incoming bidirectional streams. Incoming means streams initiated by our
  // peer. If not set, GetInitialMaxStreamDataBytesIncomingBidirectionalToSend
  // returns the value passed to SetInitialStreamFlowControlWindowToSend.
  void SetInitialMaxStreamDataBytesIncomingBidirectionalToSend(
      uint64_t window_bytes);
  uint64_t GetInitialMaxStreamDataBytesIncomingBidirectionalToSend() const;
  bool HasReceivedInitialMaxStreamDataBytesIncomingBidirectional() const;
  uint64_t ReceivedInitialMaxStreamDataBytesIncomingBidirectional() const;

  // Specifies the initial flow control window (max stream data) for
  // outgoing bidirectional streams. Outgoing means streams initiated by us.
  // If not set, GetInitialMaxStreamDataBytesOutgoingBidirectionalToSend
  // returns the value passed to SetInitialStreamFlowControlWindowToSend.
  void SetInitialMaxStreamDataBytesOutgoingBidirectionalToSend(
      uint64_t window_bytes);
  uint64_t GetInitialMaxStreamDataBytesOutgoingBidirectionalToSend() const;
  bool HasReceivedInitialMaxStreamDataBytesOutgoingBidirectional() const;
  uint64_t ReceivedInitialMaxStreamDataBytesOutgoingBidirectional() const;

  // Specifies the initial flow control window (max stream data) for
  // unidirectional streams. If not set,
  // GetInitialMaxStreamDataBytesUnidirectionalToSend returns the value passed
  // to SetInitialStreamFlowControlWindowToSend.
  void SetInitialMaxStreamDataBytesUnidirectionalToSend(uint64_t window_bytes);
  uint64_t GetInitialMaxStreamDataBytesUnidirectionalToSend() const;
  bool HasReceivedInitialMaxStreamDataBytesUnidirectional() const;
  uint64_t ReceivedInitialMaxStreamDataBytesUnidirectional() const;

  // Sets an initial session flow control window size to transmit to the peer.
  void SetInitialSessionFlowControlWindowToSend(uint64_t window_bytes);
  uint64_t GetInitialSessionFlowControlWindowToSend() const;
  bool HasReceivedInitialSessionFlowControlWindowBytes() const;
  uint64_t ReceivedInitialSessionFlowControlWindowBytes() const;

  // Disable connection migration.
  void SetDisableConnectionMigration();
  bool DisableConnectionMigration() const;

  // IPv6 alternate server address.
  void SetIPv6AlternateServerAddressToSend(
      const QuicSocketAddress& alternate_server_address_ipv6);
  bool HasReceivedIPv6AlternateServerAddress() const;
  const QuicSocketAddress& ReceivedIPv6AlternateServerAddress() const;

  // IPv4 alternate server address.
  void SetIPv4AlternateServerAddressToSend(
      const QuicSocketAddress& alternate_server_address_ipv4);
  bool HasReceivedIPv4AlternateServerAddress() const;
  const QuicSocketAddress& ReceivedIPv4AlternateServerAddress() const;

  // Called to set |connection_id| and |stateless_reset_token| if server
  // preferred address has been set via SetIPv(4|6)AlternateServerAddressToSend.
  // Please note, this is different from SetStatelessResetTokenToSend(const
  // StatelessResetToken&) which is used to send the token corresponding to the
  // existing server_connection_id.
  void SetPreferredAddressConnectionIdAndTokenToSend(
      const QuicConnectionId& connection_id,
      const StatelessResetToken& stateless_reset_token);

  // Preferred Address Connection ID and Token.
  bool HasReceivedPreferredAddressConnectionIdAndToken() const;
  const std::pair<QuicConnectionId, StatelessResetToken>&
  ReceivedPreferredAddressConnectionIdAndToken() const;
  std::optional<QuicSocketAddress> GetPreferredAddressToSend(
      quiche::IpAddressFamily address_family) const;
  void ClearAlternateServerAddressToSend(
      quiche::IpAddressFamily address_family);

  // Sets the alternate server addresses to be used for a server behind a
  // DNAT. The `to_send` address will be sent to the client, and the
  // `mapped` address will be the corresponding internal address. Server-only.
  void SetIPv4AlternateServerAddressForDNat(
      const QuicSocketAddress& alternate_server_address_ipv4_to_send,
      const QuicSocketAddress& mapped_alternate_server_address_ipv4);
  void SetIPv6AlternateServerAddressForDNat(
      const QuicSocketAddress& alternate_server_address_ipv6_to_send,
      const QuicSocketAddress& mapped_alternate_server_address_ipv6);

  // Returns the address the the server will receive packest from
  // when the client is sending to the preferred address. Will be
  // the mapped address, if present, or the alternate address otherwise.
  std::optional<QuicSocketAddress> GetMappedAlternativeServerAddress(
      quiche::IpAddressFamily address_family) const;

  // Returns true if this config supports server preferred address,
  // either via the kSPAD connection option or the QUIC protocol flag
  // quic_always_support_server_preferred_address.
  bool SupportsServerPreferredAddress(Perspective perspective) const;

  // Returns true if this config supports reliable stream reset.
  void SetReliableStreamReset(bool reliable_stream_reset);
  bool SupportsReliableStreamReset() const;

  // Original destination connection ID.
  void SetOriginalConnectionIdToSend(
      const QuicConnectionId& original_destination_connection_id);
  bool HasReceivedOriginalConnectionId() const;
  QuicConnectionId ReceivedOriginalConnectionId() const;

  // Stateless reset token.
  void SetStatelessResetTokenToSend(
      const StatelessResetToken& stateless_reset_token);
  bool HasStatelessResetTokenToSend() const;
  bool HasReceivedStatelessResetToken() const;
  const StatelessResetToken& ReceivedStatelessResetToken() const;

  // Manage the IETF QUIC Max ACK Delay transport parameter.
  // The sent value is the delay that this node uses
  // (QuicSentPacketManager::local_max_ack_delay_).
  // The received delay is the value received from
  // the peer (QuicSentPacketManager::peer_max_ack_delay_).
  void SetMaxAckDelayToSendMs(uint32_t max_ack_delay_ms);
  uint32_t GetMaxAckDelayToSendMs() const;
  bool HasReceivedMaxAckDelayMs() const;
  uint32_t ReceivedMaxAckDelayMs() const;

  // Manage the IETF QUIC extension Min Ack Delay transport parameter.
  // An endpoint uses min_ack_delay to advsertise its support for
  // AckFrequencyFrame sent by peer.
  void SetMinAckDelayMs(uint32_t min_ack_delay_ms);
  uint32_t GetMinAckDelayToSendMs() const;
  bool HasReceivedMinAckDelayMs() const;
  uint32_t ReceivedMinAckDelayMs() const;

  void SetAckDelayExponentToSend(uint32_t exponent);
  uint32_t GetAckDelayExponentToSend() const;
  bool HasReceivedAckDelayExponent() const;
  uint32_t ReceivedAckDelayExponent() const;

  // IETF QUIC max_udp_payload_size transport parameter.
  void SetMaxPacketSizeToSend(uint64_t max_udp_payload_size);
  uint64_t GetMaxPacketSizeToSend() const;
  bool HasReceivedMaxPacketSize() const;
  uint64_t ReceivedMaxPacketSize() const;

  // IETF QUIC max_datagram_frame_size transport parameter.
  void SetMaxDatagramFrameSizeToSend(uint64_t max_datagram_frame_size);
  uint64_t GetMaxDatagramFrameSizeToSend() const;
  bool HasReceivedMaxDatagramFrameSize() const;
  uint64_t ReceivedMaxDatagramFrameSize() const;

  // IETF QUIC active_connection_id_limit transport parameter.
  void SetActiveConnectionIdLimitToSend(uint64_t active_connection_id_limit);
  uint64_t GetActiveConnectionIdLimitToSend() const;
  bool HasReceivedActiveConnectionIdLimit() const;
  uint64_t ReceivedActiveConnectionIdLimit() const;

  // Initial source connection ID.
  void SetInitialSourceConnectionIdToSend(
      const QuicConnectionId& initial_source_connection_id);
  bool HasReceivedInitialSourceConnectionId() const;
  QuicConnectionId ReceivedInitialSourceConnectionId() const;

  // Retry source connection ID.
  void SetRetrySourceConnectionIdToSend(
      const QuicConnectionId& retry_source_connection_id);
  bool HasReceivedRetrySourceConnectionId() const;
  QuicConnectionId ReceivedRetrySourceConnectionId() const;

  bool negotiated() const;

  void SetCreateSessionTagIndicators(QuicTagVector tags);

  const QuicTagVector& create_session_tag_indicators() const;

  // ToHandshakeMessage serialises the settings in this object as a series of
  // tags /value pairs and adds them to |out|.
  void ToHandshakeMessage(CryptoHandshakeMessage* out,
                          QuicTransportVersion transport_version) const;

  // Calls ProcessPeerHello on each negotiable parameter. On failure returns
  // the corresponding QuicErrorCode and sets detailed error in |error_details|.
  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 std::string* error_details);

  // FillTransportParameters writes the values to send for ICSL, MIDS, CFCW, and
  // SFCW to |*params|, returning true if the values could be written and false
  // if something prevents them from being written (e.g. a value is too large).
  bool FillTransportParameters(TransportParameters* params) const;

  // ProcessTransportParameters reads from |params| which were received from a
  // peer. If |is_resumption|, some configs will not be processed.
  // On failure, it returns a QuicErrorCode and puts a detailed error in
  // |*error_details|.
  QuicErrorCode ProcessTransportParameters(const TransportParameters& params,
                                           bool is_resumption,
                                           std::string* error_details);

  TransportParameters::ParameterMap& custom_transport_parameters_to_send() {
    return custom_transport_parameters_to_send_;
  }
  const TransportParameters::ParameterMap&
  received_custom_transport_parameters() const {
    return received_custom_transport_parameters_;
  }

  // Called to clear google_handshake_message to send or received.
  void ClearGoogleHandshakeMessage();

 private:
  friend class test::QuicConfigPeer;

  // SetDefaults sets the members to sensible, default values.
  void SetDefaults();

  // Whether we've received the peer's config.
  bool negotiated_;

  // Configurations options that are not negotiated.
  // Maximum time the session can be alive before crypto handshake is finished.
  QuicTime::Delta max_time_before_crypto_handshake_;
  // Maximum idle time before the crypto handshake has completed.
  QuicTime::Delta max_idle_time_before_crypto_handshake_;
  // Maximum number of undecryptable packets stored before CHLO/SHLO.
  size_t max_undecryptable_packets_;

  // Connection options which affect the server side.  May also affect the
  // client side in cases when identical behavior is desirable.
  QuicFixedTagVector connection_options_;
  // Connection options which only affect the client side.
  QuicFixedTagVector client_connection_options_;
  // Maximum idle network timeout.
  // Uses the max_idle_timeout transport parameter in IETF QUIC.
  // Note that received_max_idle_timeout_ is only populated if we receive the
  // peer's value, which isn't guaranteed in IETF QUIC as sending is optional.
  QuicTime::Delta max_idle_timeout_to_send_;
  std::optional<QuicTime::Delta> received_max_idle_timeout_;
  // Maximum number of dynamic streams that a Google QUIC connection
  // can support or the maximum number of bidirectional streams that
  // an IETF QUIC connection can support.
  // The SendValue is the limit on peer-created streams that this endpoint is
  // advertising.
  // The ReceivedValue is the limit on locally-created streams that
  // the peer advertised.
  // Uses the initial_max_streams_bidi transport parameter in IETF QUIC.
  QuicFixedUint32 max_bidirectional_streams_;
  // Maximum number of unidirectional streams that the connection can
  // support.
  // The SendValue is the limit on peer-created streams that this endpoint is
  // advertising.
  // The ReceivedValue is the limit on locally-created streams that the peer
  // advertised.
  // Uses the initial_max_streams_uni transport parameter in IETF QUIC.
  QuicFixedUint32 max_unidirectional_streams_;
  // The number of bytes required for the connection ID. This is only used in
  // the legacy header format used only by Q043 at this point.
  QuicFixedUint32 bytes_for_connection_id_;
  // Initial round trip time estimate in microseconds.
  QuicFixedUint62 initial_round_trip_time_us_;

  // Initial IETF QUIC stream flow control receive windows in bytes.
  // Incoming bidirectional streams.
  // Uses the initial_max_stream_data_bidi_{local,remote} transport parameter
  // in IETF QUIC, depending on whether we're sending or receiving.
  QuicFixedUint62 initial_max_stream_data_bytes_incoming_bidirectional_;
  // Outgoing bidirectional streams.
  // Uses the initial_max_stream_data_bidi_{local,remote} transport parameter
  // in IETF QUIC, depending on whether we're sending or receiving.
  QuicFixedUint62 initial_max_stream_data_bytes_outgoing_bidirectional_;
  // Unidirectional streams.
  // Uses the initial_max_stream_data_uni transport parameter in IETF QUIC.
  QuicFixedUint62 initial_max_stream_data_bytes_unidirectional_;

  // Initial Google QUIC stream flow control receive window in bytes.
  QuicFixedUint62 initial_stream_flow_control_window_bytes_;

  // Initial session flow control receive window in bytes.
  // Uses the initial_max_data transport parameter in IETF QUIC.
  QuicFixedUint62 initial_session_flow_control_window_bytes_;

  // Whether active connection migration is allowed.
  // Uses the disable_active_migration transport parameter in IETF QUIC.
  QuicFixedUint32 connection_migration_disabled_;

  // Alternate server addresses the client could connect to.
  // Uses the preferred_address transport parameter in IETF QUIC.
  // Note that when QUIC_CRYPTO is in use, only one of the addresses is sent.
  QuicFixedSocketAddress alternate_server_address_ipv6_;
  QuicFixedSocketAddress alternate_server_address_ipv4_;

  // When a server is behind DNAT, the addresses it sends to the client will
  // not be the source address recevied in packets from the client. These
  // two optional members capture the internal addresses which map to
  // the addresses sent on the wire.
  std::optional<QuicSocketAddress> mapped_alternate_server_address_ipv6_;
  std::optional<QuicSocketAddress> mapped_alternate_server_address_ipv4_;

  // Connection Id data to send from the server or receive at the client as part
  // of the preferred address transport parameter.
  std::optional<std::pair<QuicConnectionId, StatelessResetToken>>
      preferred_address_connection_id_and_token_;

  // Stateless reset token used in IETF public reset packet.
  // Uses the stateless_reset_token transport parameter in IETF QUIC.
  QuicFixedStatelessResetToken stateless_reset_token_;

  // List of QuicTags whose presence immediately causes the session to
  // be created. This allows for CHLOs that are larger than a single
  // packet to be processed.
  QuicTagVector create_session_tag_indicators_;

  // Maximum ack delay. The sent value is the value used on this node.
  // The received value is the value received from the peer and used by
  // the peer.
  // Uses the max_ack_delay transport parameter in IETF QUIC.
  QuicFixedUint32 max_ack_delay_ms_;

  // Minimum ack delay. Used to enable sender control of max_ack_delay.
  // Uses the min_ack_delay transport parameter in IETF QUIC extension.
  QuicFixedUint32 min_ack_delay_ms_;

  // The sent exponent is the exponent that this node uses when serializing an
  // ACK frame (and the peer should use when deserializing the frame);
  // the received exponent is the value the peer uses to serialize frames and
  // this node uses to deserialize them.
  // Uses the ack_delay_exponent transport parameter in IETF QUIC.
  QuicFixedUint32 ack_delay_exponent_;

  // Maximum packet size in bytes.
  // Uses the max_udp_payload_size transport parameter in IETF QUIC.
  QuicFixedUint62 max_udp_payload_size_;

  // Maximum DATAGRAM/MESSAGE frame size in bytes.
  // Uses the max_datagram_frame_size transport parameter in IETF QUIC.
  QuicFixedUint62 max_datagram_frame_size_;

  // Maximum number of connection IDs from the peer.
  // Uses the active_connection_id_limit transport parameter in IETF QUIC.
  QuicFixedUint62 active_connection_id_limit_;

  // The value of the Destination Connection ID field from the first
  // Initial packet sent by the client.
  // Uses the original_destination_connection_id transport parameter in
  // IETF QUIC.
  std::optional<QuicConnectionId> original_destination_connection_id_to_send_;
  std::optional<QuicConnectionId> received_original_destination_connection_id_;

  // The value that the endpoint included in the Source Connection ID field of
  // the first Initial packet it sent.
  // Uses the initial_source_connection_id transport parameter in IETF QUIC.
  std::optional<QuicConnectionId> initial_source_connection_id_to_send_;
  std::optional<QuicConnectionId> received_initial_source_connection_id_;

  // The value that the server included in the Source Connection ID field of a
  // Retry packet it sent.
  // Uses the retry_source_connection_id transport parameter in IETF QUIC.
  std::optional<QuicConnectionId> retry_source_connection_id_to_send_;
  std::optional<QuicConnectionId> received_retry_source_connection_id_;

  // Custom transport parameters that can be sent and received in the TLS
  // handshake.
  TransportParameters::ParameterMap custom_transport_parameters_to_send_;
  TransportParameters::ParameterMap received_custom_transport_parameters_;

  // Length of the data to send in the 'discard' transport parameter. Negative
  // values means do not send.
  int32_t discard_length_to_send_ = -1;

  // Length of the receive data in the 'discard' transport parameter. Negative
  // values means 'discard' data not received.
  int32_t discard_length_received_ = -1;

  // Google internal handshake message.
  std::optional<std::string> google_handshake_message_to_send_;
  std::optional<std::string> received_google_handshake_message_;

  // Support for RESET_STREAM_AT frame.
  bool reliable_stream_reset_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONFIG_H_
