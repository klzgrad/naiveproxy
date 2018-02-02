// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_config.h"

#include <algorithm>

#include "net/quic/core/crypto/crypto_handshake_message.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/quic_socket_address_coder.h"
#include "net/quic/core/quic_utils.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flag_utils.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_string_piece.h"

using std::string;

namespace net {

// Reads the value corresponding to |name_| from |msg| into |out|. If the
// |name_| is absent in |msg| and |presence| is set to OPTIONAL |out| is set
// to |default_value|.
QuicErrorCode ReadUint32(const CryptoHandshakeMessage& msg,
                         QuicTag tag,
                         QuicConfigPresence presence,
                         uint32_t default_value,
                         uint32_t* out,
                         string* error_details) {
  DCHECK(error_details != nullptr);
  QuicErrorCode error = msg.GetUint32(tag, out);
  switch (error) {
    case QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND:
      if (presence == PRESENCE_REQUIRED) {
        *error_details = "Missing " + QuicTagToString(tag);
        break;
      }
      error = QUIC_NO_ERROR;
      *out = default_value;
      break;
    case QUIC_NO_ERROR:
      break;
    default:
      *error_details = "Bad " + QuicTagToString(tag);
      break;
  }
  return error;
}

QuicConfigValue::QuicConfigValue(QuicTag tag, QuicConfigPresence presence)
    : tag_(tag), presence_(presence) {}
QuicConfigValue::~QuicConfigValue() {}

QuicNegotiableValue::QuicNegotiableValue(QuicTag tag,
                                         QuicConfigPresence presence)
    : QuicConfigValue(tag, presence), negotiated_(false) {}
QuicNegotiableValue::~QuicNegotiableValue() {}

QuicNegotiableUint32::QuicNegotiableUint32(QuicTag tag,
                                           QuicConfigPresence presence)
    : QuicNegotiableValue(tag, presence),
      max_value_(0),
      default_value_(0),
      negotiated_value_(0) {}
QuicNegotiableUint32::~QuicNegotiableUint32() {}

void QuicNegotiableUint32::set(uint32_t max, uint32_t default_value) {
  DCHECK_LE(default_value, max);
  max_value_ = max;
  default_value_ = default_value;
}

uint32_t QuicNegotiableUint32::GetUint32() const {
  if (negotiated()) {
    return negotiated_value_;
  }
  return default_value_;
}

// Returns the maximum value negotiable.
uint32_t QuicNegotiableUint32::GetMax() const {
  return max_value_;
}

void QuicNegotiableUint32::ToHandshakeMessage(
    CryptoHandshakeMessage* out) const {
  if (negotiated()) {
    out->SetValue(tag_, negotiated_value_);
  } else {
    out->SetValue(tag_, max_value_);
  }
}

QuicErrorCode QuicNegotiableUint32::ProcessPeerHello(
    const CryptoHandshakeMessage& peer_hello,
    HelloType hello_type,
    string* error_details) {
  DCHECK(!negotiated());
  DCHECK(error_details != nullptr);
  uint32_t value;
  QuicErrorCode error = ReadUint32(peer_hello, tag_, presence_, default_value_,
                                   &value, error_details);
  if (error != QUIC_NO_ERROR) {
    return error;
  }
  if (hello_type == SERVER && value > max_value_) {
    *error_details = "Invalid value received for " + QuicTagToString(tag_);
    return QUIC_INVALID_NEGOTIATED_VALUE;
  }

  set_negotiated(true);
  negotiated_value_ = std::min(value, max_value_);
  return QUIC_NO_ERROR;
}

QuicFixedUint32::QuicFixedUint32(QuicTag tag, QuicConfigPresence presence)
    : QuicConfigValue(tag, presence),
      has_send_value_(false),
      has_receive_value_(false) {}
QuicFixedUint32::~QuicFixedUint32() {}

bool QuicFixedUint32::HasSendValue() const {
  return has_send_value_;
}

uint32_t QuicFixedUint32::GetSendValue() const {
  QUIC_BUG_IF(!has_send_value_) << "No send value to get for tag:"
                                << QuicTagToString(tag_);
  return send_value_;
}

void QuicFixedUint32::SetSendValue(uint32_t value) {
  has_send_value_ = true;
  send_value_ = value;
}

bool QuicFixedUint32::HasReceivedValue() const {
  return has_receive_value_;
}

uint32_t QuicFixedUint32::GetReceivedValue() const {
  QUIC_BUG_IF(!has_receive_value_) << "No receive value to get for tag:"
                                   << QuicTagToString(tag_);
  return receive_value_;
}

void QuicFixedUint32::SetReceivedValue(uint32_t value) {
  has_receive_value_ = true;
  receive_value_ = value;
}

void QuicFixedUint32::ToHandshakeMessage(CryptoHandshakeMessage* out) const {
  if (has_send_value_) {
    out->SetValue(tag_, send_value_);
  }
}

QuicErrorCode QuicFixedUint32::ProcessPeerHello(
    const CryptoHandshakeMessage& peer_hello,
    HelloType hello_type,
    string* error_details) {
  DCHECK(error_details != nullptr);
  QuicErrorCode error = peer_hello.GetUint32(tag_, &receive_value_);
  switch (error) {
    case QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND:
      if (presence_ == PRESENCE_OPTIONAL) {
        return QUIC_NO_ERROR;
      }
      *error_details = "Missing " + QuicTagToString(tag_);
      break;
    case QUIC_NO_ERROR:
      has_receive_value_ = true;
      break;
    default:
      *error_details = "Bad " + QuicTagToString(tag_);
      break;
  }
  return error;
}

QuicFixedUint128::QuicFixedUint128(QuicTag tag, QuicConfigPresence presence)
    : QuicConfigValue(tag, presence),
      has_send_value_(false),
      has_receive_value_(false) {}
QuicFixedUint128::~QuicFixedUint128() {}

bool QuicFixedUint128::HasSendValue() const {
  return has_send_value_;
}

uint128 QuicFixedUint128::GetSendValue() const {
  QUIC_BUG_IF(!has_send_value_)
      << "No send value to get for tag:" << QuicTagToString(tag_);
  return send_value_;
}

void QuicFixedUint128::SetSendValue(uint128 value) {
  has_send_value_ = true;
  send_value_ = value;
}

bool QuicFixedUint128::HasReceivedValue() const {
  return has_receive_value_;
}

uint128 QuicFixedUint128::GetReceivedValue() const {
  QUIC_BUG_IF(!has_receive_value_)
      << "No receive value to get for tag:" << QuicTagToString(tag_);
  return receive_value_;
}

void QuicFixedUint128::SetReceivedValue(uint128 value) {
  has_receive_value_ = true;
  receive_value_ = value;
}

void QuicFixedUint128::ToHandshakeMessage(CryptoHandshakeMessage* out) const {
  if (has_send_value_) {
    out->SetValue(tag_, send_value_);
  }
}

QuicErrorCode QuicFixedUint128::ProcessPeerHello(
    const CryptoHandshakeMessage& peer_hello,
    HelloType hello_type,
    string* error_details) {
  DCHECK(error_details != nullptr);
  QuicErrorCode error = peer_hello.GetUint128(tag_, &receive_value_);
  switch (error) {
    case QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND:
      if (presence_ == PRESENCE_OPTIONAL) {
        return QUIC_NO_ERROR;
      }
      *error_details = "Missing " + QuicTagToString(tag_);
      break;
    case QUIC_NO_ERROR:
      has_receive_value_ = true;
      break;
    default:
      *error_details = "Bad " + QuicTagToString(tag_);
      break;
  }
  return error;
}

QuicFixedTagVector::QuicFixedTagVector(QuicTag name,
                                       QuicConfigPresence presence)
    : QuicConfigValue(name, presence),
      has_send_values_(false),
      has_receive_values_(false) {}

QuicFixedTagVector::QuicFixedTagVector(const QuicFixedTagVector& other) =
    default;

QuicFixedTagVector::~QuicFixedTagVector() {}

bool QuicFixedTagVector::HasSendValues() const {
  return has_send_values_;
}

QuicTagVector QuicFixedTagVector::GetSendValues() const {
  QUIC_BUG_IF(!has_send_values_) << "No send values to get for tag:"
                                 << QuicTagToString(tag_);
  return send_values_;
}

void QuicFixedTagVector::SetSendValues(const QuicTagVector& values) {
  has_send_values_ = true;
  send_values_ = values;
}

bool QuicFixedTagVector::HasReceivedValues() const {
  return has_receive_values_;
}

QuicTagVector QuicFixedTagVector::GetReceivedValues() const {
  QUIC_BUG_IF(!has_receive_values_) << "No receive value to get for tag:"
                                    << QuicTagToString(tag_);
  return receive_values_;
}

void QuicFixedTagVector::SetReceivedValues(const QuicTagVector& values) {
  has_receive_values_ = true;
  receive_values_ = values;
}

void QuicFixedTagVector::ToHandshakeMessage(CryptoHandshakeMessage* out) const {
  if (has_send_values_) {
    out->SetVector(tag_, send_values_);
  }
}

QuicErrorCode QuicFixedTagVector::ProcessPeerHello(
    const CryptoHandshakeMessage& peer_hello,
    HelloType hello_type,
    string* error_details) {
  DCHECK(error_details != nullptr);
  QuicTagVector values;
  QuicErrorCode error = peer_hello.GetTaglist(tag_, &values);
  switch (error) {
    case QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND:
      if (presence_ == PRESENCE_OPTIONAL) {
        return QUIC_NO_ERROR;
      }
      *error_details = "Missing " + QuicTagToString(tag_);
      break;
    case QUIC_NO_ERROR:
      QUIC_DVLOG(1) << "Received Connection Option tags from receiver.";
      has_receive_values_ = true;
      receive_values_.insert(receive_values_.end(), values.begin(),
                             values.end());
      break;
    default:
      *error_details = "Bad " + QuicTagToString(tag_);
      break;
  }
  return error;
}

QuicFixedSocketAddress::QuicFixedSocketAddress(QuicTag tag,
                                               QuicConfigPresence presence)
    : QuicConfigValue(tag, presence),
      has_send_value_(false),
      has_receive_value_(false) {}

QuicFixedSocketAddress::~QuicFixedSocketAddress() {}

bool QuicFixedSocketAddress::HasSendValue() const {
  return has_send_value_;
}

const QuicSocketAddress& QuicFixedSocketAddress::GetSendValue() const {
  QUIC_BUG_IF(!has_send_value_) << "No send value to get for tag:"
                                << QuicTagToString(tag_);
  return send_value_;
}

void QuicFixedSocketAddress::SetSendValue(const QuicSocketAddress& value) {
  has_send_value_ = true;
  send_value_ = value;
}

bool QuicFixedSocketAddress::HasReceivedValue() const {
  return has_receive_value_;
}

const QuicSocketAddress& QuicFixedSocketAddress::GetReceivedValue() const {
  QUIC_BUG_IF(!has_receive_value_) << "No receive value to get for tag:"
                                   << QuicTagToString(tag_);
  return receive_value_;
}

void QuicFixedSocketAddress::SetReceivedValue(const QuicSocketAddress& value) {
  has_receive_value_ = true;
  receive_value_ = value;
}

void QuicFixedSocketAddress::ToHandshakeMessage(
    CryptoHandshakeMessage* out) const {
  if (has_send_value_) {
    QuicSocketAddressCoder address_coder(send_value_);
    out->SetStringPiece(tag_, address_coder.Encode());
  }
}

QuicErrorCode QuicFixedSocketAddress::ProcessPeerHello(
    const CryptoHandshakeMessage& peer_hello,
    HelloType hello_type,
    string* error_details) {
  QuicStringPiece address;
  if (!peer_hello.GetStringPiece(tag_, &address)) {
    if (presence_ == PRESENCE_REQUIRED) {
      *error_details = "Missing " + QuicTagToString(tag_);
      return QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND;
    }
  } else {
    QuicSocketAddressCoder address_coder;
    if (address_coder.Decode(address.data(), address.length())) {
      SetReceivedValue(
          QuicSocketAddress(address_coder.ip(), address_coder.port()));
    }
  }
  return QUIC_NO_ERROR;
}

QuicConfig::QuicConfig()
    : max_time_before_crypto_handshake_(QuicTime::Delta::Zero()),
      max_idle_time_before_crypto_handshake_(QuicTime::Delta::Zero()),
      max_undecryptable_packets_(0),
      connection_options_(kCOPT, PRESENCE_OPTIONAL),
      client_connection_options_(kCLOP, PRESENCE_OPTIONAL),
      idle_network_timeout_seconds_(kICSL, PRESENCE_REQUIRED),
      silent_close_(kSCLS, PRESENCE_OPTIONAL),
      max_streams_per_connection_(kMSPC, PRESENCE_OPTIONAL),
      max_incoming_dynamic_streams_(kMIDS, PRESENCE_OPTIONAL),
      bytes_for_connection_id_(kTCID, PRESENCE_OPTIONAL),
      initial_round_trip_time_us_(kIRTT, PRESENCE_OPTIONAL),
      initial_stream_flow_control_window_bytes_(kSFCW, PRESENCE_OPTIONAL),
      initial_session_flow_control_window_bytes_(kCFCW, PRESENCE_OPTIONAL),
      socket_receive_buffer_(kSRBF, PRESENCE_OPTIONAL),
      connection_migration_disabled_(kNCMR, PRESENCE_OPTIONAL),
      alternate_server_address_(kASAD, PRESENCE_OPTIONAL),
      support_max_header_list_size_(kSMHL, PRESENCE_OPTIONAL),
      stateless_reset_token_(kSRST, PRESENCE_OPTIONAL) {
  SetDefaults();
}

QuicConfig::QuicConfig(const QuicConfig& other) = default;

QuicConfig::~QuicConfig() {}

bool QuicConfig::SetInitialReceivedConnectionOptions(
    const QuicTagVector& tags) {
  if (HasReceivedConnectionOptions()) {
    // If we have already received connection options (via handshake or due to
    // a previous call), don't re-initialize.
    return false;
  }
  connection_options_.SetReceivedValues(tags);
  return true;
}

void QuicConfig::SetConnectionOptionsToSend(
    const QuicTagVector& connection_options) {
  connection_options_.SetSendValues(connection_options);
}

bool QuicConfig::HasReceivedConnectionOptions() const {
  return connection_options_.HasReceivedValues();
}

QuicTagVector QuicConfig::ReceivedConnectionOptions() const {
  return connection_options_.GetReceivedValues();
}

bool QuicConfig::HasSendConnectionOptions() const {
  return connection_options_.HasSendValues();
}

QuicTagVector QuicConfig::SendConnectionOptions() const {
  return connection_options_.GetSendValues();
}

bool QuicConfig::HasClientSentConnectionOption(QuicTag tag,
                                               Perspective perspective) const {
  if (perspective == Perspective::IS_SERVER) {
    if (HasReceivedConnectionOptions() &&
        ContainsQuicTag(ReceivedConnectionOptions(), tag)) {
      return true;
    }
  } else if (HasSendConnectionOptions() &&
             ContainsQuicTag(SendConnectionOptions(), tag)) {
    return true;
  }
  return false;
}

void QuicConfig::SetClientConnectionOptions(
    const QuicTagVector& client_connection_options) {
  client_connection_options_.SetSendValues(client_connection_options);
}

bool QuicConfig::HasClientRequestedIndependentOption(
    QuicTag tag,
    Perspective perspective) const {
  if (perspective == Perspective::IS_SERVER) {
    return (HasReceivedConnectionOptions() &&
            ContainsQuicTag(ReceivedConnectionOptions(), tag));
  }

  return (client_connection_options_.HasSendValues() &&
          ContainsQuicTag(client_connection_options_.GetSendValues(), tag));
}

void QuicConfig::SetIdleNetworkTimeout(
    QuicTime::Delta max_idle_network_timeout,
    QuicTime::Delta default_idle_network_timeout) {
  idle_network_timeout_seconds_.set(
      static_cast<uint32_t>(max_idle_network_timeout.ToSeconds()),
      static_cast<uint32_t>(default_idle_network_timeout.ToSeconds()));
}

QuicTime::Delta QuicConfig::IdleNetworkTimeout() const {
  return QuicTime::Delta::FromSeconds(
      idle_network_timeout_seconds_.GetUint32());
}

// TODO(ianswett) Use this for silent close on mobile, or delete.
void QuicConfig::SetSilentClose(bool silent_close) {
  silent_close_.set(silent_close ? 1 : 0, silent_close ? 1 : 0);
}

bool QuicConfig::SilentClose() const {
  return silent_close_.GetUint32() > 0;
}

void QuicConfig::SetMaxStreamsPerConnection(size_t max_streams,
                                            size_t default_streams) {
  max_streams_per_connection_.set(max_streams, default_streams);
}

uint32_t QuicConfig::MaxStreamsPerConnection() const {
  return max_streams_per_connection_.GetUint32();
}

void QuicConfig::SetMaxIncomingDynamicStreamsToSend(
    uint32_t max_incoming_dynamic_streams) {
  max_incoming_dynamic_streams_.SetSendValue(max_incoming_dynamic_streams);
}

uint32_t QuicConfig::GetMaxIncomingDynamicStreamsToSend() {
  return max_incoming_dynamic_streams_.GetSendValue();
}

bool QuicConfig::HasReceivedMaxIncomingDynamicStreams() {
  return max_incoming_dynamic_streams_.HasReceivedValue();
}

uint32_t QuicConfig::ReceivedMaxIncomingDynamicStreams() {
  return max_incoming_dynamic_streams_.GetReceivedValue();
}

bool QuicConfig::HasSetBytesForConnectionIdToSend() const {
  return bytes_for_connection_id_.HasSendValue();
}

void QuicConfig::SetBytesForConnectionIdToSend(uint32_t bytes) {
  bytes_for_connection_id_.SetSendValue(bytes);
}

bool QuicConfig::HasReceivedBytesForConnectionId() const {
  return bytes_for_connection_id_.HasReceivedValue();
}

uint32_t QuicConfig::ReceivedBytesForConnectionId() const {
  return bytes_for_connection_id_.GetReceivedValue();
}

void QuicConfig::SetInitialRoundTripTimeUsToSend(uint32_t rtt) {
  initial_round_trip_time_us_.SetSendValue(rtt);
}

bool QuicConfig::HasReceivedInitialRoundTripTimeUs() const {
  return initial_round_trip_time_us_.HasReceivedValue();
}

uint32_t QuicConfig::ReceivedInitialRoundTripTimeUs() const {
  return initial_round_trip_time_us_.GetReceivedValue();
}

bool QuicConfig::HasInitialRoundTripTimeUsToSend() const {
  return initial_round_trip_time_us_.HasSendValue();
}

uint32_t QuicConfig::GetInitialRoundTripTimeUsToSend() const {
  return initial_round_trip_time_us_.GetSendValue();
}

void QuicConfig::SetInitialStreamFlowControlWindowToSend(
    uint32_t window_bytes) {
  if (window_bytes < kMinimumFlowControlSendWindow) {
    QUIC_BUG << "Initial stream flow control receive window (" << window_bytes
             << ") cannot be set lower than default ("
             << kMinimumFlowControlSendWindow << ").";
    window_bytes = kMinimumFlowControlSendWindow;
  }
  initial_stream_flow_control_window_bytes_.SetSendValue(window_bytes);
}

uint32_t QuicConfig::GetInitialStreamFlowControlWindowToSend() const {
  return initial_stream_flow_control_window_bytes_.GetSendValue();
}

bool QuicConfig::HasReceivedInitialStreamFlowControlWindowBytes() const {
  return initial_stream_flow_control_window_bytes_.HasReceivedValue();
}

uint32_t QuicConfig::ReceivedInitialStreamFlowControlWindowBytes() const {
  return initial_stream_flow_control_window_bytes_.GetReceivedValue();
}

void QuicConfig::SetInitialSessionFlowControlWindowToSend(
    uint32_t window_bytes) {
  if (window_bytes < kMinimumFlowControlSendWindow) {
    QUIC_BUG << "Initial session flow control receive window (" << window_bytes
             << ") cannot be set lower than default ("
             << kMinimumFlowControlSendWindow << ").";
    window_bytes = kMinimumFlowControlSendWindow;
  }
  initial_session_flow_control_window_bytes_.SetSendValue(window_bytes);
}

uint32_t QuicConfig::GetInitialSessionFlowControlWindowToSend() const {
  return initial_session_flow_control_window_bytes_.GetSendValue();
}

bool QuicConfig::HasReceivedInitialSessionFlowControlWindowBytes() const {
  return initial_session_flow_control_window_bytes_.HasReceivedValue();
}

uint32_t QuicConfig::ReceivedInitialSessionFlowControlWindowBytes() const {
  return initial_session_flow_control_window_bytes_.GetReceivedValue();
}

void QuicConfig::SetDisableConnectionMigration() {
  connection_migration_disabled_.SetSendValue(1);
}

bool QuicConfig::DisableConnectionMigration() const {
  return connection_migration_disabled_.HasReceivedValue();
}

void QuicConfig::SetAlternateServerAddressToSend(
    const QuicSocketAddress& alternate_server_address) {
  alternate_server_address_.SetSendValue(alternate_server_address);
}

bool QuicConfig::HasReceivedAlternateServerAddress() const {
  return alternate_server_address_.HasReceivedValue();
}

const QuicSocketAddress& QuicConfig::ReceivedAlternateServerAddress() const {
  return alternate_server_address_.GetReceivedValue();
}

void QuicConfig::SetSupportMaxHeaderListSize() {
  support_max_header_list_size_.SetSendValue(1);
}

bool QuicConfig::SupportMaxHeaderListSize() const {
  return support_max_header_list_size_.HasReceivedValue();
}

void QuicConfig::SetStatelessResetTokenToSend(uint128 stateless_reset_token) {
  stateless_reset_token_.SetSendValue(stateless_reset_token);
}

bool QuicConfig::HasReceivedStatelessResetToken() const {
  return stateless_reset_token_.HasReceivedValue();
}

uint128 QuicConfig::ReceivedStatelessResetToken() const {
  return stateless_reset_token_.GetReceivedValue();
}

bool QuicConfig::negotiated() const {
  // TODO(ianswett): Add the negotiated parameters once and iterate over all
  // of them in negotiated, ToHandshakeMessage, ProcessClientHello, and
  // ProcessServerHello.
  return idle_network_timeout_seconds_.negotiated() &&
         max_streams_per_connection_.negotiated();
}

void QuicConfig::SetDefaults() {
  idle_network_timeout_seconds_.set(kMaximumIdleTimeoutSecs,
                                    kDefaultIdleTimeoutSecs);
  silent_close_.set(1, 0);
  SetMaxStreamsPerConnection(kDefaultMaxStreamsPerConnection,
                             kDefaultMaxStreamsPerConnection);
  SetMaxIncomingDynamicStreamsToSend(kDefaultMaxStreamsPerConnection);
  max_time_before_crypto_handshake_ =
      QuicTime::Delta::FromSeconds(kMaxTimeForCryptoHandshakeSecs);
  max_idle_time_before_crypto_handshake_ =
      QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs);
  max_undecryptable_packets_ = kDefaultMaxUndecryptablePackets;

  SetInitialStreamFlowControlWindowToSend(kMinimumFlowControlSendWindow);
  SetInitialSessionFlowControlWindowToSend(kMinimumFlowControlSendWindow);
  if (FLAGS_quic_reloadable_flag_quic_send_max_header_list_size) {
    SetSupportMaxHeaderListSize();
  }
}

void QuicConfig::ToHandshakeMessage(CryptoHandshakeMessage* out) const {
  idle_network_timeout_seconds_.ToHandshakeMessage(out);
  silent_close_.ToHandshakeMessage(out);
  max_streams_per_connection_.ToHandshakeMessage(out);
  max_incoming_dynamic_streams_.ToHandshakeMessage(out);
  bytes_for_connection_id_.ToHandshakeMessage(out);
  initial_round_trip_time_us_.ToHandshakeMessage(out);
  initial_stream_flow_control_window_bytes_.ToHandshakeMessage(out);
  initial_session_flow_control_window_bytes_.ToHandshakeMessage(out);
  connection_migration_disabled_.ToHandshakeMessage(out);
  connection_options_.ToHandshakeMessage(out);
  alternate_server_address_.ToHandshakeMessage(out);
  support_max_header_list_size_.ToHandshakeMessage(out);
  stateless_reset_token_.ToHandshakeMessage(out);
}

QuicErrorCode QuicConfig::ProcessPeerHello(
    const CryptoHandshakeMessage& peer_hello,
    HelloType hello_type,
    string* error_details) {
  DCHECK(error_details != nullptr);

  QuicErrorCode error = QUIC_NO_ERROR;
  if (error == QUIC_NO_ERROR) {
    error = idle_network_timeout_seconds_.ProcessPeerHello(
        peer_hello, hello_type, error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error =
        silent_close_.ProcessPeerHello(peer_hello, hello_type, error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = max_streams_per_connection_.ProcessPeerHello(peer_hello, hello_type,
                                                         error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = max_incoming_dynamic_streams_.ProcessPeerHello(
        peer_hello, hello_type, error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = bytes_for_connection_id_.ProcessPeerHello(peer_hello, hello_type,
                                                      error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = initial_round_trip_time_us_.ProcessPeerHello(peer_hello, hello_type,
                                                         error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = initial_stream_flow_control_window_bytes_.ProcessPeerHello(
        peer_hello, hello_type, error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = initial_session_flow_control_window_bytes_.ProcessPeerHello(
        peer_hello, hello_type, error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = connection_migration_disabled_.ProcessPeerHello(
        peer_hello, hello_type, error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = connection_options_.ProcessPeerHello(peer_hello, hello_type,
                                                 error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = alternate_server_address_.ProcessPeerHello(peer_hello, hello_type,
                                                       error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = support_max_header_list_size_.ProcessPeerHello(
        peer_hello, hello_type, error_details);
  }
  if (error == QUIC_NO_ERROR) {
    error = stateless_reset_token_.ProcessPeerHello(peer_hello, hello_type,
                                                    error_details);
  }
  return error;
}

}  // namespace net
