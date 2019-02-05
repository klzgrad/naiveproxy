// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_CONFIG_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_CONFIG_H_

#include <cstddef>
#include <cstdint>

#include "net/third_party/quic/core/crypto/transport_parameters.h"
#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/core/quic_time.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_uint128.h"

namespace quic {

namespace test {
class QuicConfigPeer;
}  // namespace test

class CryptoHandshakeMessage;

// Describes whether or not a given QuicTag is required or optional in the
// handshake message.
enum QuicConfigPresence {
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
class QUIC_EXPORT_PRIVATE QuicConfigValue {
 public:
  QuicConfigValue(QuicTag tag, QuicConfigPresence presence);
  virtual ~QuicConfigValue();

  // Serialises tag name and value(s) to |out|.
  virtual void ToHandshakeMessage(CryptoHandshakeMessage* out) const = 0;

  // Selects a mutually acceptable value from those offered in |peer_hello|
  // and those defined in the subclass.
  virtual QuicErrorCode ProcessPeerHello(
      const CryptoHandshakeMessage& peer_hello,
      HelloType hello_type,
      QuicString* error_details) = 0;

 protected:
  const QuicTag tag_;
  const QuicConfigPresence presence_;
};

class QUIC_EXPORT_PRIVATE QuicNegotiableValue : public QuicConfigValue {
 public:
  QuicNegotiableValue(QuicTag tag, QuicConfigPresence presence);
  ~QuicNegotiableValue() override;

  bool negotiated() const { return negotiated_; }

 protected:
  void set_negotiated(bool negotiated) { negotiated_ = negotiated; }

 private:
  bool negotiated_;
};

class QUIC_EXPORT_PRIVATE QuicNegotiableUint32 : public QuicNegotiableValue {
  // TODO(fayang): some negotiated values use uint32 as bool (e.g., silent
  // close). Consider adding a QuicNegotiableBool type.
 public:
  // Default and max values default to 0.
  QuicNegotiableUint32(QuicTag name, QuicConfigPresence presence);
  ~QuicNegotiableUint32() override;

  // Sets the maximum possible value that can be achieved after negotiation and
  // also the default values to be assumed if PRESENCE_OPTIONAL and the *HLO msg
  // doesn't contain a value corresponding to |name_|. |max| is serialised via
  // ToHandshakeMessage call if |negotiated_| is false.
  void set(uint32_t max, uint32_t default_value);

  // Returns the value negotiated if |negotiated_| is true, otherwise returns
  // default_value_ (used to set default values before negotiation finishes).
  uint32_t GetUint32() const;

  // Returns the maximum value negotiable.
  uint32_t GetMax() const;

  // Serialises |name_| and value to |out|. If |negotiated_| is true then
  // |negotiated_value_| is serialised, otherwise |max_value_| is serialised.
  void ToHandshakeMessage(CryptoHandshakeMessage* out) const override;

  // Processes the corresponding value from |peer_hello| and if present calls
  // ReceiveValue with it. If the corresponding value is missing and
  // PRESENCE_OPTIONAL then |negotiated_value_| is set to |default_value_|.
  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 QuicString* error_details) override;

  // Takes a value |value| parsed from a handshake message (whether a TLS
  // ClientHello/ServerHello or a CryptoHandshakeMessage) whose sender was
  // |hello_type|, and sets |negotiated_value_| to the minimum of |value| and
  // |max_value_|. On success this function returns QUIC_NO_ERROR; if there is
  // an error, details are put in |*error_details|.
  QuicErrorCode ReceiveValue(uint32_t value,
                             HelloType hello_type,
                             QuicString* error_details);

 private:
  uint32_t max_value_;
  uint32_t default_value_;
  uint32_t negotiated_value_;
};

// Stores uint32_t from CHLO or SHLO messages that are not negotiated.
class QUIC_EXPORT_PRIVATE QuicFixedUint32 : public QuicConfigValue {
 public:
  QuicFixedUint32(QuicTag name, QuicConfigPresence presence);
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
                                 QuicString* error_details) override;

 private:
  uint32_t send_value_;
  bool has_send_value_;
  uint32_t receive_value_;
  bool has_receive_value_;
};

// Stores uint128 from CHLO or SHLO messages that are not negotiated.
class QUIC_EXPORT_PRIVATE QuicFixedUint128 : public QuicConfigValue {
 public:
  QuicFixedUint128(QuicTag tag, QuicConfigPresence presence);
  ~QuicFixedUint128() override;

  bool HasSendValue() const;

  QuicUint128 GetSendValue() const;

  void SetSendValue(QuicUint128 value);

  bool HasReceivedValue() const;

  QuicUint128 GetReceivedValue() const;

  void SetReceivedValue(QuicUint128 value);

  // If has_send_value is true, serialises |tag_| and |send_value_| to |out|.
  void ToHandshakeMessage(CryptoHandshakeMessage* out) const override;

  // Sets |value_| to the corresponding value from |peer_hello_| if it exists.
  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 QuicString* error_details) override;

 private:
  QuicUint128 send_value_;
  bool has_send_value_;
  QuicUint128 receive_value_;
  bool has_receive_value_;
};

// Stores tag from CHLO or SHLO messages that are not negotiated.
class QUIC_EXPORT_PRIVATE QuicFixedTagVector : public QuicConfigValue {
 public:
  QuicFixedTagVector(QuicTag name, QuicConfigPresence presence);
  QuicFixedTagVector(const QuicFixedTagVector& other);
  ~QuicFixedTagVector() override;

  bool HasSendValues() const;

  QuicTagVector GetSendValues() const;

  void SetSendValues(const QuicTagVector& values);

  bool HasReceivedValues() const;

  QuicTagVector GetReceivedValues() const;

  void SetReceivedValues(const QuicTagVector& values);

  // If has_send_value is true, serialises |tag_vector_| and |send_value_| to
  // |out|.
  void ToHandshakeMessage(CryptoHandshakeMessage* out) const override;

  // Sets |receive_values_| to the corresponding value from |client_hello_| if
  // it exists.
  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 QuicString* error_details) override;

 private:
  QuicTagVector send_values_;
  bool has_send_values_;
  QuicTagVector receive_values_;
  bool has_receive_values_;
};

// Stores QuicSocketAddress from CHLO or SHLO messages that are not negotiated.
class QUIC_EXPORT_PRIVATE QuicFixedSocketAddress : public QuicConfigValue {
 public:
  QuicFixedSocketAddress(QuicTag tag, QuicConfigPresence presence);
  ~QuicFixedSocketAddress() override;

  bool HasSendValue() const;

  const QuicSocketAddress& GetSendValue() const;

  void SetSendValue(const QuicSocketAddress& value);

  bool HasReceivedValue() const;

  const QuicSocketAddress& GetReceivedValue() const;

  void SetReceivedValue(const QuicSocketAddress& value);

  void ToHandshakeMessage(CryptoHandshakeMessage* out) const override;

  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 QuicString* error_details) override;

 private:
  QuicSocketAddress send_value_;
  bool has_send_value_;
  QuicSocketAddress receive_value_;
  bool has_receive_value_;
};

// QuicConfig contains non-crypto configuration options that are negotiated in
// the crypto handshake.
class QUIC_EXPORT_PRIVATE QuicConfig {
 public:
  QuicConfig();
  QuicConfig(const QuicConfig& other);
  ~QuicConfig();

  void SetConnectionOptionsToSend(const QuicTagVector& connection_options);

  bool HasReceivedConnectionOptions() const;

  // Sets initial received connection options.  All received connection options
  // will be initialized with these fields. Initial received options may only be
  // set once per config, prior to the setting of any other options.  If options
  // have already been set (either by previous calls or via handshake), this
  // function does nothing and returns false.
  bool SetInitialReceivedConnectionOptions(const QuicTagVector& tags);

  QuicTagVector ReceivedConnectionOptions() const;

  bool HasSendConnectionOptions() const;

  QuicTagVector SendConnectionOptions() const;

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

  void SetIdleNetworkTimeout(QuicTime::Delta max_idle_network_timeout,
                             QuicTime::Delta default_idle_network_timeout);

  QuicTime::Delta IdleNetworkTimeout() const;

  void SetSilentClose(bool silent_close);

  bool SilentClose() const;

  void SetMaxIncomingDynamicStreamsToSend(
      uint32_t max_incoming_dynamic_streams);

  uint32_t GetMaxIncomingDynamicStreamsToSend();

  bool HasReceivedMaxIncomingDynamicStreams();

  uint32_t ReceivedMaxIncomingDynamicStreams();

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

  QuicNegotiableUint32 idle_network_timeout_seconds() const {
    return idle_network_timeout_seconds_;
  }

  void set_max_undecryptable_packets(size_t max_undecryptable_packets) {
    max_undecryptable_packets_ = max_undecryptable_packets;
  }

  size_t max_undecryptable_packets() const {
    return max_undecryptable_packets_;
  }

  bool HasSetBytesForConnectionIdToSend() const;

  // Sets the peer's connection id length, in bytes.
  void SetBytesForConnectionIdToSend(uint32_t bytes);

  bool HasReceivedBytesForConnectionId() const;

  uint32_t ReceivedBytesForConnectionId() const;

  // Sets an estimated initial round trip time in us.
  void SetInitialRoundTripTimeUsToSend(uint32_t rtt_us);

  bool HasReceivedInitialRoundTripTimeUs() const;

  uint32_t ReceivedInitialRoundTripTimeUs() const;

  bool HasInitialRoundTripTimeUsToSend() const;

  uint32_t GetInitialRoundTripTimeUsToSend() const;

  // Sets an initial stream flow control window size to transmit to the peer.
  void SetInitialStreamFlowControlWindowToSend(uint32_t window_bytes);

  uint32_t GetInitialStreamFlowControlWindowToSend() const;

  bool HasReceivedInitialStreamFlowControlWindowBytes() const;

  uint32_t ReceivedInitialStreamFlowControlWindowBytes() const;

  // Sets an initial session flow control window size to transmit to the peer.
  void SetInitialSessionFlowControlWindowToSend(uint32_t window_bytes);

  uint32_t GetInitialSessionFlowControlWindowToSend() const;

  bool HasReceivedInitialSessionFlowControlWindowBytes() const;

  uint32_t ReceivedInitialSessionFlowControlWindowBytes() const;

  void SetDisableConnectionMigration();

  bool DisableConnectionMigration() const;

  void SetAlternateServerAddressToSend(
      const QuicSocketAddress& alternate_server_address);

  bool HasReceivedAlternateServerAddress() const;

  const QuicSocketAddress& ReceivedAlternateServerAddress() const;

  void SetSupportMaxHeaderListSize();

  bool SupportMaxHeaderListSize() const;

  void SetStatelessResetTokenToSend(QuicUint128 stateless_reset_token);

  bool HasReceivedStatelessResetToken() const;

  QuicUint128 ReceivedStatelessResetToken() const;

  bool negotiated() const;

  void SetCreateSessionTagIndicators(QuicTagVector tags);

  const QuicTagVector& create_session_tag_indicators() const;

  // ToHandshakeMessage serialises the settings in this object as a series of
  // tags /value pairs and adds them to |out|.
  void ToHandshakeMessage(CryptoHandshakeMessage* out) const;

  // Calls ProcessPeerHello on each negotiable parameter. On failure returns
  // the corresponding QuicErrorCode and sets detailed error in |error_details|.
  QuicErrorCode ProcessPeerHello(const CryptoHandshakeMessage& peer_hello,
                                 HelloType hello_type,
                                 QuicString* error_details);

  // FillTransportParameters writes the values to send for ICSL, MIDS, CFCW, and
  // SFCW to |*params|, returning true if the values could be written and false
  // if something prevents them from being written (e.g. a value is too large).
  bool FillTransportParameters(TransportParameters* params) const;

  // ProcessTransportParameters reads from |params| which was received from a
  // peer operating as a |hello_type|. It processes values for ICSL, MIDS, CFCW,
  // and SFCW and sets the corresponding members of this QuicConfig. On failure,
  // it returns a QuicErrorCode and puts a detailed error in |*error_details|.
  QuicErrorCode ProcessTransportParameters(const TransportParameters& params,
                                           HelloType hello_type,
                                           QuicString* error_details);

 private:
  friend class test::QuicConfigPeer;

  // SetDefaults sets the members to sensible, default values.
  void SetDefaults();

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
  // Idle network timeout in seconds.
  QuicNegotiableUint32 idle_network_timeout_seconds_;
  // Whether to use silent close.  Defaults to 0 (false) and is otherwise true.
  QuicNegotiableUint32 silent_close_;
  // Maximum number of incoming dynamic streams that the connection can support.
  QuicFixedUint32 max_incoming_dynamic_streams_;
  // The number of bytes required for the connection ID.
  QuicFixedUint32 bytes_for_connection_id_;
  // Initial round trip time estimate in microseconds.
  QuicFixedUint32 initial_round_trip_time_us_;

  // Initial stream flow control receive window in bytes.
  QuicFixedUint32 initial_stream_flow_control_window_bytes_;
  // Initial session flow control receive window in bytes.
  QuicFixedUint32 initial_session_flow_control_window_bytes_;

  // Whether tell peer not to attempt connection migration.
  QuicFixedUint32 connection_migration_disabled_;

  // An alternate server address the client could connect to.
  QuicFixedSocketAddress alternate_server_address_;

  // Whether support HTTP/2 SETTINGS_MAX_HEADER_LIST_SIZE SETTINGS frame.
  QuicFixedUint32 support_max_header_list_size_;

  // Stateless reset token used in IETF public reset packet.
  QuicFixedUint128 stateless_reset_token_;

  // List of QuicTags whose presence immediately causes the session to
  // be created. This allows for CHLOs that are larger than a single
  // packet to be processed.
  QuicTagVector create_session_tag_indicators_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_CONFIG_H_
