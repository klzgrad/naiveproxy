// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/crypto/transport_parameters.h"

#include <cstdint>
#include <cstring>
#include <forward_list>
#include <memory>
#include <utility>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/sha.h"
#include "quic/core/quic_connection_id.h"
#include "quic/core/quic_data_reader.h"
#include "quic/core/quic_data_writer.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_utils.h"
#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_bug_tracker.h"
#include "common/platform/api/quiche_text_utils.h"

namespace quic {

// Values of the TransportParameterId enum as defined in the
// "Transport Parameter Encoding" section of draft-ietf-quic-transport.
// When parameters are encoded, one of these enum values is used to indicate
// which parameter is encoded. The supported draft version is noted in
// transport_parameters.h.
enum TransportParameters::TransportParameterId : uint64_t {
  kOriginalDestinationConnectionId = 0,
  kMaxIdleTimeout = 1,
  kStatelessResetToken = 2,
  kMaxPacketSize = 3,
  kInitialMaxData = 4,
  kInitialMaxStreamDataBidiLocal = 5,
  kInitialMaxStreamDataBidiRemote = 6,
  kInitialMaxStreamDataUni = 7,
  kInitialMaxStreamsBidi = 8,
  kInitialMaxStreamsUni = 9,
  kAckDelayExponent = 0xa,
  kMaxAckDelay = 0xb,
  kDisableActiveMigration = 0xc,
  kPreferredAddress = 0xd,
  kActiveConnectionIdLimit = 0xe,
  kInitialSourceConnectionId = 0xf,
  kRetrySourceConnectionId = 0x10,

  kMaxDatagramFrameSize = 0x20,

  kInitialRoundTripTime = 0x3127,
  kGoogleConnectionOptions = 0x3128,
  kGoogleUserAgentId = 0x3129,
  // 0x312A was used only in T050 to indicate support for HANDSHAKE_DONE.
  kGoogleKeyUpdateNotYetSupported = 0x312B,
  // 0x4751 was used for non-standard Google-specific parameters encoded as a
  // Google QUIC_CRYPTO CHLO, it has been replaced by individual parameters.
  kGoogleQuicVersion =
      0x4752,  // Used to transmit version and supported_versions.

  kMinAckDelay = 0xDE1A,  // draft-iyengar-quic-delayed-ack.
};

namespace {

// The following constants define minimum and maximum allowed values for some of
// the parameters. These come from the "Transport Parameter Definitions"
// section of draft-ietf-quic-transport.
constexpr uint64_t kMinMaxPacketSizeTransportParam = 1200;
constexpr uint64_t kMaxAckDelayExponentTransportParam = 20;
constexpr uint64_t kDefaultAckDelayExponentTransportParam = 3;
constexpr uint64_t kMaxMaxAckDelayTransportParam = 16383;
constexpr uint64_t kDefaultMaxAckDelayTransportParam = 25;
constexpr size_t kStatelessResetTokenLength = 16;
constexpr uint64_t kMinActiveConnectionIdLimitTransportParam = 2;
constexpr uint64_t kDefaultActiveConnectionIdLimitTransportParam = 2;

std::string TransportParameterIdToString(
    TransportParameters::TransportParameterId param_id) {
  switch (param_id) {
    case TransportParameters::kOriginalDestinationConnectionId:
      return "original_destination_connection_id";
    case TransportParameters::kMaxIdleTimeout:
      return "max_idle_timeout";
    case TransportParameters::kStatelessResetToken:
      return "stateless_reset_token";
    case TransportParameters::kMaxPacketSize:
      return "max_udp_payload_size";
    case TransportParameters::kInitialMaxData:
      return "initial_max_data";
    case TransportParameters::kInitialMaxStreamDataBidiLocal:
      return "initial_max_stream_data_bidi_local";
    case TransportParameters::kInitialMaxStreamDataBidiRemote:
      return "initial_max_stream_data_bidi_remote";
    case TransportParameters::kInitialMaxStreamDataUni:
      return "initial_max_stream_data_uni";
    case TransportParameters::kInitialMaxStreamsBidi:
      return "initial_max_streams_bidi";
    case TransportParameters::kInitialMaxStreamsUni:
      return "initial_max_streams_uni";
    case TransportParameters::kAckDelayExponent:
      return "ack_delay_exponent";
    case TransportParameters::kMaxAckDelay:
      return "max_ack_delay";
    case TransportParameters::kDisableActiveMigration:
      return "disable_active_migration";
    case TransportParameters::kPreferredAddress:
      return "preferred_address";
    case TransportParameters::kActiveConnectionIdLimit:
      return "active_connection_id_limit";
    case TransportParameters::kInitialSourceConnectionId:
      return "initial_source_connection_id";
    case TransportParameters::kRetrySourceConnectionId:
      return "retry_source_connection_id";
    case TransportParameters::kMaxDatagramFrameSize:
      return "max_datagram_frame_size";
    case TransportParameters::kInitialRoundTripTime:
      return "initial_round_trip_time";
    case TransportParameters::kGoogleConnectionOptions:
      return "google_connection_options";
    case TransportParameters::kGoogleUserAgentId:
      return "user_agent_id";
    case TransportParameters::kGoogleKeyUpdateNotYetSupported:
      return "key_update_not_yet_supported";
    case TransportParameters::kGoogleQuicVersion:
      return "google-version";
    case TransportParameters::kMinAckDelay:
      return "min_ack_delay_us";
  }
  return "Unknown(" + quiche::QuicheTextUtils::Uint64ToString(param_id) + ")";
}

bool TransportParameterIdIsKnown(
    TransportParameters::TransportParameterId param_id) {
  switch (param_id) {
    case TransportParameters::kOriginalDestinationConnectionId:
    case TransportParameters::kMaxIdleTimeout:
    case TransportParameters::kStatelessResetToken:
    case TransportParameters::kMaxPacketSize:
    case TransportParameters::kInitialMaxData:
    case TransportParameters::kInitialMaxStreamDataBidiLocal:
    case TransportParameters::kInitialMaxStreamDataBidiRemote:
    case TransportParameters::kInitialMaxStreamDataUni:
    case TransportParameters::kInitialMaxStreamsBidi:
    case TransportParameters::kInitialMaxStreamsUni:
    case TransportParameters::kAckDelayExponent:
    case TransportParameters::kMaxAckDelay:
    case TransportParameters::kDisableActiveMigration:
    case TransportParameters::kPreferredAddress:
    case TransportParameters::kActiveConnectionIdLimit:
    case TransportParameters::kInitialSourceConnectionId:
    case TransportParameters::kRetrySourceConnectionId:
    case TransportParameters::kMaxDatagramFrameSize:
    case TransportParameters::kInitialRoundTripTime:
    case TransportParameters::kGoogleConnectionOptions:
    case TransportParameters::kGoogleUserAgentId:
    case TransportParameters::kGoogleKeyUpdateNotYetSupported:
    case TransportParameters::kGoogleQuicVersion:
    case TransportParameters::kMinAckDelay:
      return true;
  }
  return false;
}

}  // namespace

TransportParameters::IntegerParameter::IntegerParameter(
    TransportParameters::TransportParameterId param_id,
    uint64_t default_value,
    uint64_t min_value,
    uint64_t max_value)
    : param_id_(param_id),
      value_(default_value),
      default_value_(default_value),
      min_value_(min_value),
      max_value_(max_value),
      has_been_read_(false) {
  QUICHE_DCHECK_LE(min_value, default_value);
  QUICHE_DCHECK_LE(default_value, max_value);
  QUICHE_DCHECK_LE(max_value, kVarInt62MaxValue);
}

TransportParameters::IntegerParameter::IntegerParameter(
    TransportParameters::TransportParameterId param_id)
    : TransportParameters::IntegerParameter::IntegerParameter(
          param_id,
          0,
          0,
          kVarInt62MaxValue) {}

void TransportParameters::IntegerParameter::set_value(uint64_t value) {
  value_ = value;
}

uint64_t TransportParameters::IntegerParameter::value() const {
  return value_;
}

bool TransportParameters::IntegerParameter::IsValid() const {
  return min_value_ <= value_ && value_ <= max_value_;
}

bool TransportParameters::IntegerParameter::Write(
    QuicDataWriter* writer) const {
  QUICHE_DCHECK(IsValid());
  if (value_ == default_value_) {
    // Do not write if the value is default.
    return true;
  }
  if (!writer->WriteVarInt62(param_id_)) {
    QUIC_BUG << "Failed to write param_id for " << *this;
    return false;
  }
  const QuicVariableLengthIntegerLength value_length =
      QuicDataWriter::GetVarInt62Len(value_);
  if (!writer->WriteVarInt62(value_length)) {
    QUIC_BUG << "Failed to write value_length for " << *this;
    return false;
  }
  if (!writer->WriteVarInt62(value_, value_length)) {
    QUIC_BUG << "Failed to write value for " << *this;
    return false;
  }
  return true;
}

bool TransportParameters::IntegerParameter::Read(QuicDataReader* reader,
                                                 std::string* error_details) {
  if (has_been_read_) {
    *error_details =
        "Received a second " + TransportParameterIdToString(param_id_);
    return false;
  }
  has_been_read_ = true;

  if (!reader->ReadVarInt62(&value_)) {
    *error_details =
        "Failed to parse value for " + TransportParameterIdToString(param_id_);
    return false;
  }
  if (!reader->IsDoneReading()) {
    *error_details =
        absl::StrCat("Received unexpected ", reader->BytesRemaining(),
                     " bytes after parsing ", this->ToString(false));
    return false;
  }
  return true;
}

std::string TransportParameters::IntegerParameter::ToString(
    bool for_use_in_list) const {
  if (for_use_in_list && value_ == default_value_) {
    return "";
  }
  std::string rv = for_use_in_list ? " " : "";
  rv += TransportParameterIdToString(param_id_) + " ";
  rv += quiche::QuicheTextUtils::Uint64ToString(value_);
  if (!IsValid()) {
    rv += " (Invalid)";
  }
  return rv;
}

std::ostream& operator<<(std::ostream& os,
                         const TransportParameters::IntegerParameter& param) {
  os << param.ToString(/*for_use_in_list=*/false);
  return os;
}

TransportParameters::PreferredAddress::PreferredAddress()
    : ipv4_socket_address(QuicIpAddress::Any4(), 0),
      ipv6_socket_address(QuicIpAddress::Any6(), 0),
      connection_id(EmptyQuicConnectionId()),
      stateless_reset_token(kStatelessResetTokenLength, 0) {}

TransportParameters::PreferredAddress::~PreferredAddress() {}

bool TransportParameters::PreferredAddress::operator==(
    const PreferredAddress& rhs) const {
  return ipv4_socket_address == rhs.ipv4_socket_address &&
         ipv6_socket_address == rhs.ipv6_socket_address &&
         connection_id == rhs.connection_id &&
         stateless_reset_token == rhs.stateless_reset_token;
}

bool TransportParameters::PreferredAddress::operator!=(
    const PreferredAddress& rhs) const {
  return !(*this == rhs);
}

std::ostream& operator<<(
    std::ostream& os,
    const TransportParameters::PreferredAddress& preferred_address) {
  os << preferred_address.ToString();
  return os;
}

std::string TransportParameters::PreferredAddress::ToString() const {
  return "[" + ipv4_socket_address.ToString() + " " +
         ipv6_socket_address.ToString() + " connection_id " +
         connection_id.ToString() + " stateless_reset_token " +
         absl::BytesToHexString(absl::string_view(
             reinterpret_cast<const char*>(stateless_reset_token.data()),
             stateless_reset_token.size())) +
         "]";
}

std::ostream& operator<<(std::ostream& os, const TransportParameters& params) {
  os << params.ToString();
  return os;
}

std::string TransportParameters::ToString() const {
  std::string rv = "[";
  if (perspective == Perspective::IS_SERVER) {
    rv += "Server";
  } else {
    rv += "Client";
  }
  if (version != 0) {
    rv += " version " + QuicVersionLabelToString(version);
  }
  if (!supported_versions.empty()) {
    rv += " supported_versions " +
          QuicVersionLabelVectorToString(supported_versions);
  }
  if (original_destination_connection_id.has_value()) {
    rv += " " + TransportParameterIdToString(kOriginalDestinationConnectionId) +
          " " + original_destination_connection_id.value().ToString();
  }
  rv += max_idle_timeout_ms.ToString(/*for_use_in_list=*/true);
  if (!stateless_reset_token.empty()) {
    rv += " " + TransportParameterIdToString(kStatelessResetToken) + " " +
          absl::BytesToHexString(absl::string_view(
              reinterpret_cast<const char*>(stateless_reset_token.data()),
              stateless_reset_token.size()));
  }
  rv += max_udp_payload_size.ToString(/*for_use_in_list=*/true);
  rv += initial_max_data.ToString(/*for_use_in_list=*/true);
  rv += initial_max_stream_data_bidi_local.ToString(/*for_use_in_list=*/true);
  rv += initial_max_stream_data_bidi_remote.ToString(/*for_use_in_list=*/true);
  rv += initial_max_stream_data_uni.ToString(/*for_use_in_list=*/true);
  rv += initial_max_streams_bidi.ToString(/*for_use_in_list=*/true);
  rv += initial_max_streams_uni.ToString(/*for_use_in_list=*/true);
  rv += ack_delay_exponent.ToString(/*for_use_in_list=*/true);
  rv += max_ack_delay.ToString(/*for_use_in_list=*/true);
  rv += min_ack_delay_us.ToString(/*for_use_in_list=*/true);
  if (disable_active_migration) {
    rv += " " + TransportParameterIdToString(kDisableActiveMigration);
  }
  if (preferred_address) {
    rv += " " + TransportParameterIdToString(kPreferredAddress) + " " +
          preferred_address->ToString();
  }
  rv += active_connection_id_limit.ToString(/*for_use_in_list=*/true);
  if (initial_source_connection_id.has_value()) {
    rv += " " + TransportParameterIdToString(kInitialSourceConnectionId) + " " +
          initial_source_connection_id.value().ToString();
  }
  if (retry_source_connection_id.has_value()) {
    rv += " " + TransportParameterIdToString(kRetrySourceConnectionId) + " " +
          retry_source_connection_id.value().ToString();
  }
  rv += max_datagram_frame_size.ToString(/*for_use_in_list=*/true);
  rv += initial_round_trip_time_us.ToString(/*for_use_in_list=*/true);
  if (google_connection_options.has_value()) {
    rv += " " + TransportParameterIdToString(kGoogleConnectionOptions) + " ";
    bool first = true;
    for (const QuicTag& connection_option : google_connection_options.value()) {
      if (first) {
        first = false;
      } else {
        rv += ",";
      }
      rv += QuicTagToString(connection_option);
    }
  }
  if (user_agent_id.has_value()) {
    rv += " " + TransportParameterIdToString(kGoogleUserAgentId) + " \"" +
          user_agent_id.value() + "\"";
  }
  if (key_update_not_yet_supported) {
    rv += " " + TransportParameterIdToString(kGoogleKeyUpdateNotYetSupported);
  }
  for (const auto& kv : custom_parameters) {
    rv += " 0x" + quiche::QuicheTextUtils::Hex(static_cast<uint32_t>(kv.first));
    rv += "=";
    static constexpr size_t kMaxPrintableLength = 32;
    if (kv.second.length() <= kMaxPrintableLength) {
      rv += absl::BytesToHexString(kv.second);
    } else {
      absl::string_view truncated(kv.second.data(), kMaxPrintableLength);
      rv += absl::StrCat(absl::BytesToHexString(truncated), "...(length ",
                         kv.second.length(), ")");
    }
  }
  rv += "]";
  return rv;
}

TransportParameters::TransportParameters()
    : version(0),
      max_idle_timeout_ms(kMaxIdleTimeout),
      max_udp_payload_size(kMaxPacketSize,
                           kDefaultMaxPacketSizeTransportParam,
                           kMinMaxPacketSizeTransportParam,
                           kVarInt62MaxValue),
      initial_max_data(kInitialMaxData),
      initial_max_stream_data_bidi_local(kInitialMaxStreamDataBidiLocal),
      initial_max_stream_data_bidi_remote(kInitialMaxStreamDataBidiRemote),
      initial_max_stream_data_uni(kInitialMaxStreamDataUni),
      initial_max_streams_bidi(kInitialMaxStreamsBidi),
      initial_max_streams_uni(kInitialMaxStreamsUni),
      ack_delay_exponent(kAckDelayExponent,
                         kDefaultAckDelayExponentTransportParam,
                         0,
                         kMaxAckDelayExponentTransportParam),
      max_ack_delay(kMaxAckDelay,
                    kDefaultMaxAckDelayTransportParam,
                    0,
                    kMaxMaxAckDelayTransportParam),
      min_ack_delay_us(kMinAckDelay,
                       0,
                       0,
                       kMaxMaxAckDelayTransportParam * kNumMicrosPerMilli),
      disable_active_migration(false),
      active_connection_id_limit(kActiveConnectionIdLimit,
                                 kDefaultActiveConnectionIdLimitTransportParam,
                                 kMinActiveConnectionIdLimitTransportParam,
                                 kVarInt62MaxValue),
      max_datagram_frame_size(kMaxDatagramFrameSize),
      initial_round_trip_time_us(kInitialRoundTripTime),
      key_update_not_yet_supported(false)
// Important note: any new transport parameters must be added
// to TransportParameters::AreValid, SerializeTransportParameters and
// ParseTransportParameters, TransportParameters's custom copy constructor, the
// operator==, and TransportParametersTest.Comparator.
{}

TransportParameters::TransportParameters(const TransportParameters& other)
    : perspective(other.perspective),
      version(other.version),
      supported_versions(other.supported_versions),
      original_destination_connection_id(
          other.original_destination_connection_id),
      max_idle_timeout_ms(other.max_idle_timeout_ms),
      stateless_reset_token(other.stateless_reset_token),
      max_udp_payload_size(other.max_udp_payload_size),
      initial_max_data(other.initial_max_data),
      initial_max_stream_data_bidi_local(
          other.initial_max_stream_data_bidi_local),
      initial_max_stream_data_bidi_remote(
          other.initial_max_stream_data_bidi_remote),
      initial_max_stream_data_uni(other.initial_max_stream_data_uni),
      initial_max_streams_bidi(other.initial_max_streams_bidi),
      initial_max_streams_uni(other.initial_max_streams_uni),
      ack_delay_exponent(other.ack_delay_exponent),
      max_ack_delay(other.max_ack_delay),
      min_ack_delay_us(other.min_ack_delay_us),
      disable_active_migration(other.disable_active_migration),
      active_connection_id_limit(other.active_connection_id_limit),
      initial_source_connection_id(other.initial_source_connection_id),
      retry_source_connection_id(other.retry_source_connection_id),
      max_datagram_frame_size(other.max_datagram_frame_size),
      initial_round_trip_time_us(other.initial_round_trip_time_us),
      google_connection_options(other.google_connection_options),
      user_agent_id(other.user_agent_id),
      key_update_not_yet_supported(other.key_update_not_yet_supported),
      custom_parameters(other.custom_parameters) {
  if (other.preferred_address) {
    preferred_address = std::make_unique<TransportParameters::PreferredAddress>(
        *other.preferred_address);
  }
}

bool TransportParameters::operator==(const TransportParameters& rhs) const {
  if (!(perspective == rhs.perspective && version == rhs.version &&
        supported_versions == rhs.supported_versions &&
        original_destination_connection_id ==
            rhs.original_destination_connection_id &&
        max_idle_timeout_ms.value() == rhs.max_idle_timeout_ms.value() &&
        stateless_reset_token == rhs.stateless_reset_token &&
        max_udp_payload_size.value() == rhs.max_udp_payload_size.value() &&
        initial_max_data.value() == rhs.initial_max_data.value() &&
        initial_max_stream_data_bidi_local.value() ==
            rhs.initial_max_stream_data_bidi_local.value() &&
        initial_max_stream_data_bidi_remote.value() ==
            rhs.initial_max_stream_data_bidi_remote.value() &&
        initial_max_stream_data_uni.value() ==
            rhs.initial_max_stream_data_uni.value() &&
        initial_max_streams_bidi.value() ==
            rhs.initial_max_streams_bidi.value() &&
        initial_max_streams_uni.value() ==
            rhs.initial_max_streams_uni.value() &&
        ack_delay_exponent.value() == rhs.ack_delay_exponent.value() &&
        max_ack_delay.value() == rhs.max_ack_delay.value() &&
        min_ack_delay_us.value() == rhs.min_ack_delay_us.value() &&
        disable_active_migration == rhs.disable_active_migration &&
        active_connection_id_limit.value() ==
            rhs.active_connection_id_limit.value() &&
        initial_source_connection_id == rhs.initial_source_connection_id &&
        retry_source_connection_id == rhs.retry_source_connection_id &&
        max_datagram_frame_size.value() ==
            rhs.max_datagram_frame_size.value() &&
        initial_round_trip_time_us.value() ==
            rhs.initial_round_trip_time_us.value() &&
        google_connection_options == rhs.google_connection_options &&
        user_agent_id == rhs.user_agent_id &&
        key_update_not_yet_supported == rhs.key_update_not_yet_supported &&
        custom_parameters == rhs.custom_parameters)) {
    return false;
  }

  if ((!preferred_address && rhs.preferred_address) ||
      (preferred_address && !rhs.preferred_address)) {
    return false;
  }
  if (preferred_address && rhs.preferred_address &&
      *preferred_address != *rhs.preferred_address) {
    return false;
  }

  return true;
}

bool TransportParameters::operator!=(const TransportParameters& rhs) const {
  return !(*this == rhs);
}

bool TransportParameters::AreValid(std::string* error_details) const {
  QUICHE_DCHECK(perspective == Perspective::IS_CLIENT ||
                perspective == Perspective::IS_SERVER);
  if (perspective == Perspective::IS_CLIENT && !stateless_reset_token.empty()) {
    *error_details = "Client cannot send stateless reset token";
    return false;
  }
  if (perspective == Perspective::IS_CLIENT &&
      original_destination_connection_id.has_value()) {
    *error_details = "Client cannot send original_destination_connection_id";
    return false;
  }
  if (!stateless_reset_token.empty() &&
      stateless_reset_token.size() != kStatelessResetTokenLength) {
    *error_details = absl::StrCat("Stateless reset token has bad length ",
                                  stateless_reset_token.size());
    return false;
  }
  if (perspective == Perspective::IS_CLIENT && preferred_address) {
    *error_details = "Client cannot send preferred address";
    return false;
  }
  if (preferred_address && preferred_address->stateless_reset_token.size() !=
                               kStatelessResetTokenLength) {
    *error_details =
        absl::StrCat("Preferred address stateless reset token has bad length ",
                     preferred_address->stateless_reset_token.size());
    return false;
  }
  if (preferred_address &&
      (!preferred_address->ipv4_socket_address.host().IsIPv4() ||
       !preferred_address->ipv6_socket_address.host().IsIPv6())) {
    QUIC_BUG << "Preferred address family failure";
    *error_details = "Internal preferred address family failure";
    return false;
  }
  if (perspective == Perspective::IS_CLIENT &&
      retry_source_connection_id.has_value()) {
    *error_details = "Client cannot send retry_source_connection_id";
    return false;
  }
  for (const auto& kv : custom_parameters) {
    if (TransportParameterIdIsKnown(kv.first)) {
      *error_details = absl::StrCat("Using custom_parameters with known ID ",
                                    TransportParameterIdToString(kv.first),
                                    " is not allowed");
      return false;
    }
  }
  if (perspective == Perspective::IS_SERVER &&
      initial_round_trip_time_us.value() > 0) {
    *error_details = "Server cannot send initial round trip time";
    return false;
  }
  if (perspective == Perspective::IS_SERVER && user_agent_id.has_value()) {
    *error_details = "Server cannot send user agent ID";
    return false;
  }
  const bool ok =
      max_idle_timeout_ms.IsValid() && max_udp_payload_size.IsValid() &&
      initial_max_data.IsValid() &&
      initial_max_stream_data_bidi_local.IsValid() &&
      initial_max_stream_data_bidi_remote.IsValid() &&
      initial_max_stream_data_uni.IsValid() &&
      initial_max_streams_bidi.IsValid() && initial_max_streams_uni.IsValid() &&
      ack_delay_exponent.IsValid() && max_ack_delay.IsValid() &&
      min_ack_delay_us.IsValid() && active_connection_id_limit.IsValid() &&
      max_datagram_frame_size.IsValid() && initial_round_trip_time_us.IsValid();
  if (!ok) {
    *error_details = "Invalid transport parameters " + this->ToString();
  }
  return ok;
}

TransportParameters::~TransportParameters() = default;

bool SerializeTransportParameters(ParsedQuicVersion /*version*/,
                                  const TransportParameters& in,
                                  std::vector<uint8_t>* out) {
  std::string error_details;
  if (!in.AreValid(&error_details)) {
    QUIC_BUG << "Not serializing invalid transport parameters: "
             << error_details;
    return false;
  }
  if (in.version == 0 || (in.perspective == Perspective::IS_SERVER &&
                          in.supported_versions.empty())) {
    QUIC_BUG << "Refusing to serialize without versions";
    return false;
  }

  // Maximum length of the GREASE transport parameter (see below).
  static constexpr size_t kMaxGreaseLength = 16;

  // Empirically transport parameters generally fit within 128 bytes, but we
  // need to allocate the size up front. Integer transport parameters
  // have a maximum encoded length of 24 bytes (3 variable length integers),
  // other transport parameters have a length of 16 + the maximum value length.
  static constexpr size_t kTypeAndValueLength = 2 * sizeof(uint64_t);
  static constexpr size_t kIntegerParameterLength =
      kTypeAndValueLength + sizeof(uint64_t);
  static constexpr size_t kStatelessResetParameterLength =
      kTypeAndValueLength + 16 /* stateless reset token length */;
  static constexpr size_t kConnectionIdParameterLength =
      kTypeAndValueLength + 255 /* maximum connection ID length */;
  static constexpr size_t kPreferredAddressParameterLength =
      kTypeAndValueLength + 4 /*IPv4 address */ + 2 /* IPv4 port */ +
      16 /* IPv6 address */ + 1 /* Connection ID length */ +
      255 /* maximum connection ID length */ + 16 /* stateless reset token */;
  static constexpr size_t kGreaseParameterLength =
      kTypeAndValueLength + kMaxGreaseLength;
  static constexpr size_t kKnownTransportParamLength =
      kConnectionIdParameterLength +      // original_destination_connection_id
      kIntegerParameterLength +           // max_idle_timeout
      kStatelessResetParameterLength +    // stateless_reset_token
      kIntegerParameterLength +           // max_udp_payload_size
      kIntegerParameterLength +           // initial_max_data
      kIntegerParameterLength +           // initial_max_stream_data_bidi_local
      kIntegerParameterLength +           // initial_max_stream_data_bidi_remote
      kIntegerParameterLength +           // initial_max_stream_data_uni
      kIntegerParameterLength +           // initial_max_streams_bidi
      kIntegerParameterLength +           // initial_max_streams_uni
      kIntegerParameterLength +           // ack_delay_exponent
      kIntegerParameterLength +           // max_ack_delay
      kIntegerParameterLength +           // min_ack_delay_us
      kTypeAndValueLength +               // disable_active_migration
      kPreferredAddressParameterLength +  // preferred_address
      kIntegerParameterLength +           // active_connection_id_limit
      kConnectionIdParameterLength +      // initial_source_connection_id
      kConnectionIdParameterLength +      // retry_source_connection_id
      kIntegerParameterLength +           // max_datagram_frame_size
      kIntegerParameterLength +           // initial_round_trip_time_us
      kTypeAndValueLength +               // google_connection_options
      kTypeAndValueLength +               // user_agent_id
      kTypeAndValueLength +               // key_update_not_yet_supported
      kTypeAndValueLength +               // google-version
      kGreaseParameterLength;             // GREASE

  size_t max_transport_param_length = kKnownTransportParamLength;
  // google_connection_options.
  if (in.google_connection_options.has_value()) {
    max_transport_param_length +=
        in.google_connection_options.value().size() * sizeof(QuicTag);
  }
  // user_agent_id.
  if (in.user_agent_id.has_value()) {
    max_transport_param_length += in.user_agent_id.value().length();
  }
  // Google-specific version extension.
  max_transport_param_length +=
      sizeof(in.version) + 1 /* versions length */ +
      in.supported_versions.size() * sizeof(QuicVersionLabel);
  // Custom parameters.
  for (const auto& kv : in.custom_parameters) {
    max_transport_param_length += kTypeAndValueLength + kv.second.length();
  }

  out->resize(max_transport_param_length);
  QuicDataWriter writer(out->size(), reinterpret_cast<char*>(out->data()));

  // original_destination_connection_id
  if (in.original_destination_connection_id.has_value()) {
    QUICHE_DCHECK_EQ(Perspective::IS_SERVER, in.perspective);
    QuicConnectionId original_destination_connection_id =
        in.original_destination_connection_id.value();
    if (!writer.WriteVarInt62(
            TransportParameters::kOriginalDestinationConnectionId) ||
        !writer.WriteStringPieceVarInt62(
            absl::string_view(original_destination_connection_id.data(),
                              original_destination_connection_id.length()))) {
      QUIC_BUG << "Failed to write original_destination_connection_id "
               << original_destination_connection_id << " for " << in;
      return false;
    }
  }

  if (!in.max_idle_timeout_ms.Write(&writer)) {
    QUIC_BUG << "Failed to write idle_timeout for " << in;
    return false;
  }

  // stateless_reset_token
  if (!in.stateless_reset_token.empty()) {
    QUICHE_DCHECK_EQ(kStatelessResetTokenLength,
                     in.stateless_reset_token.size());
    QUICHE_DCHECK_EQ(Perspective::IS_SERVER, in.perspective);
    if (!writer.WriteVarInt62(TransportParameters::kStatelessResetToken) ||
        !writer.WriteStringPieceVarInt62(absl::string_view(
            reinterpret_cast<const char*>(in.stateless_reset_token.data()),
            in.stateless_reset_token.size()))) {
      QUIC_BUG << "Failed to write stateless_reset_token of length "
               << in.stateless_reset_token.size() << " for " << in;
      return false;
    }
  }

  if (!in.max_udp_payload_size.Write(&writer) ||
      !in.initial_max_data.Write(&writer) ||
      !in.initial_max_stream_data_bidi_local.Write(&writer) ||
      !in.initial_max_stream_data_bidi_remote.Write(&writer) ||
      !in.initial_max_stream_data_uni.Write(&writer) ||
      !in.initial_max_streams_bidi.Write(&writer) ||
      !in.initial_max_streams_uni.Write(&writer) ||
      !in.ack_delay_exponent.Write(&writer) ||
      !in.max_ack_delay.Write(&writer) || !in.min_ack_delay_us.Write(&writer) ||
      !in.active_connection_id_limit.Write(&writer) ||
      !in.max_datagram_frame_size.Write(&writer) ||
      !in.initial_round_trip_time_us.Write(&writer)) {
    QUIC_BUG << "Failed to write integers for " << in;
    return false;
  }

  // disable_active_migration
  if (in.disable_active_migration) {
    if (!writer.WriteVarInt62(TransportParameters::kDisableActiveMigration) ||
        !writer.WriteVarInt62(/* transport parameter length */ 0)) {
      QUIC_BUG << "Failed to write disable_active_migration for " << in;
      return false;
    }
  }

  // preferred_address
  if (in.preferred_address) {
    std::string v4_address_bytes =
        in.preferred_address->ipv4_socket_address.host().ToPackedString();
    std::string v6_address_bytes =
        in.preferred_address->ipv6_socket_address.host().ToPackedString();
    if (v4_address_bytes.length() != 4 || v6_address_bytes.length() != 16 ||
        in.preferred_address->stateless_reset_token.size() !=
            kStatelessResetTokenLength) {
      QUIC_BUG << "Bad lengths " << *in.preferred_address;
      return false;
    }
    const uint64_t preferred_address_length =
        v4_address_bytes.length() + /* IPv4 port */ sizeof(uint16_t) +
        v6_address_bytes.length() + /* IPv6 port */ sizeof(uint16_t) +
        /* connection ID length byte */ sizeof(uint8_t) +
        in.preferred_address->connection_id.length() +
        in.preferred_address->stateless_reset_token.size();
    if (!writer.WriteVarInt62(TransportParameters::kPreferredAddress) ||
        !writer.WriteVarInt62(
            /* transport parameter length */ preferred_address_length) ||
        !writer.WriteStringPiece(v4_address_bytes) ||
        !writer.WriteUInt16(in.preferred_address->ipv4_socket_address.port()) ||
        !writer.WriteStringPiece(v6_address_bytes) ||
        !writer.WriteUInt16(in.preferred_address->ipv6_socket_address.port()) ||
        !writer.WriteUInt8(in.preferred_address->connection_id.length()) ||
        !writer.WriteBytes(in.preferred_address->connection_id.data(),
                           in.preferred_address->connection_id.length()) ||
        !writer.WriteBytes(
            in.preferred_address->stateless_reset_token.data(),
            in.preferred_address->stateless_reset_token.size())) {
      QUIC_BUG << "Failed to write preferred_address for " << in;
      return false;
    }
  }

  // initial_source_connection_id
  if (in.initial_source_connection_id.has_value()) {
    QuicConnectionId initial_source_connection_id =
        in.initial_source_connection_id.value();
    if (!writer.WriteVarInt62(
            TransportParameters::kInitialSourceConnectionId) ||
        !writer.WriteStringPieceVarInt62(
            absl::string_view(initial_source_connection_id.data(),
                              initial_source_connection_id.length()))) {
      QUIC_BUG << "Failed to write initial_source_connection_id "
               << initial_source_connection_id << " for " << in;
      return false;
    }
  }

  // retry_source_connection_id
  if (in.retry_source_connection_id.has_value()) {
    QUICHE_DCHECK_EQ(Perspective::IS_SERVER, in.perspective);
    QuicConnectionId retry_source_connection_id =
        in.retry_source_connection_id.value();
    if (!writer.WriteVarInt62(TransportParameters::kRetrySourceConnectionId) ||
        !writer.WriteStringPieceVarInt62(
            absl::string_view(retry_source_connection_id.data(),
                              retry_source_connection_id.length()))) {
      QUIC_BUG << "Failed to write retry_source_connection_id "
               << retry_source_connection_id << " for " << in;
      return false;
    }
  }

  // Google-specific connection options.
  if (in.google_connection_options.has_value()) {
    static_assert(sizeof(in.google_connection_options.value().front()) == 4,
                  "bad size");
    uint64_t connection_options_length =
        in.google_connection_options.value().size() * 4;
    if (!writer.WriteVarInt62(TransportParameters::kGoogleConnectionOptions) ||
        !writer.WriteVarInt62(
            /* transport parameter length */ connection_options_length)) {
      QUIC_BUG << "Failed to write google_connection_options of length "
               << connection_options_length << " for " << in;
      return false;
    }
    for (const QuicTag& connection_option :
         in.google_connection_options.value()) {
      if (!writer.WriteTag(connection_option)) {
        QUIC_BUG << "Failed to write google_connection_option "
                 << QuicTagToString(connection_option) << " for " << in;
        return false;
      }
    }
  }

  // Google-specific user agent identifier.
  if (in.user_agent_id.has_value()) {
    if (!writer.WriteVarInt62(TransportParameters::kGoogleUserAgentId) ||
        !writer.WriteStringPieceVarInt62(in.user_agent_id.value())) {
      QUIC_BUG << "Failed to write Google user agent ID \""
               << in.user_agent_id.value() << "\" for " << in;
      return false;
    }
  }

  // Google-specific indicator for key update not yet supported.
  if (in.key_update_not_yet_supported) {
    if (!writer.WriteVarInt62(
            TransportParameters::kGoogleKeyUpdateNotYetSupported) ||
        !writer.WriteVarInt62(/* transport parameter length */ 0)) {
      QUIC_BUG << "Failed to write key_update_not_yet_supported for " << in;
      return false;
    }
  }

  // Google-specific version extension.
  static_assert(sizeof(QuicVersionLabel) == sizeof(uint32_t), "bad length");
  uint64_t google_version_length = sizeof(in.version);
  if (in.perspective == Perspective::IS_SERVER) {
    google_version_length +=
        /* versions length */ sizeof(uint8_t) +
        sizeof(QuicVersionLabel) * in.supported_versions.size();
  }
  if (!writer.WriteVarInt62(TransportParameters::kGoogleQuicVersion) ||
      !writer.WriteVarInt62(
          /* transport parameter length */ google_version_length) ||
      !writer.WriteUInt32(in.version)) {
    QUIC_BUG << "Failed to write Google version extension for " << in;
    return false;
  }
  if (in.perspective == Perspective::IS_SERVER) {
    if (!writer.WriteUInt8(sizeof(QuicVersionLabel) *
                           in.supported_versions.size())) {
      QUIC_BUG << "Failed to write versions length for " << in;
      return false;
    }
    for (QuicVersionLabel version_label : in.supported_versions) {
      if (!writer.WriteUInt32(version_label)) {
        QUIC_BUG << "Failed to write supported version for " << in;
        return false;
      }
    }
  }

  for (const auto& kv : in.custom_parameters) {
    const TransportParameters::TransportParameterId param_id = kv.first;
    if (param_id % 31 == 27) {
      // See the "Reserved Transport Parameters" section of
      // draft-ietf-quic-transport.
      QUIC_BUG << "Serializing custom_parameters with GREASE ID " << param_id
               << " is not allowed";
      return false;
    }
    if (!writer.WriteVarInt62(param_id) ||
        !writer.WriteStringPieceVarInt62(kv.second)) {
      QUIC_BUG << "Failed to write custom parameter " << param_id;
      return false;
    }
  }

  {
    // Add a random GREASE transport parameter, as defined in the
    // "Reserved Transport Parameters" section of draft-ietf-quic-transport.
    // https://quicwg.org/base-drafts/draft-ietf-quic-transport.html
    // This forces receivers to support unexpected input.
    QuicRandom* random = QuicRandom::GetInstance();
    // Transport parameter identifiers are 62 bits long so we need to ensure
    // that the output of the computation below fits in 62 bits.
    uint64_t grease_id64 = random->RandUint64() % ((1ULL << 62) - 31);
    // Make sure grease_id % 31 == 27. Note that this is not uniformely
    // distributed but is acceptable since no security depends on this
    // randomness.
    grease_id64 = (grease_id64 / 31) * 31 + 27;
    TransportParameters::TransportParameterId grease_id =
        static_cast<TransportParameters::TransportParameterId>(grease_id64);
    const size_t grease_length = random->RandUint64() % kMaxGreaseLength;
    QUICHE_DCHECK_GE(kMaxGreaseLength, grease_length);
    char grease_contents[kMaxGreaseLength];
    random->RandBytes(grease_contents, grease_length);
    if (!writer.WriteVarInt62(grease_id) ||
        !writer.WriteStringPieceVarInt62(
            absl::string_view(grease_contents, grease_length))) {
      QUIC_BUG << "Failed to write GREASE parameter "
               << TransportParameterIdToString(grease_id);
      return false;
    }
  }

  out->resize(writer.length());

  QUIC_DLOG(INFO) << "Serialized " << in << " as " << writer.length()
                  << " bytes";

  return true;
}

bool ParseTransportParameters(ParsedQuicVersion version,
                              Perspective perspective,
                              const uint8_t* in,
                              size_t in_len,
                              TransportParameters* out,
                              std::string* error_details) {
  out->perspective = perspective;
  QuicDataReader reader(reinterpret_cast<const char*>(in), in_len);

  while (!reader.IsDoneReading()) {
    uint64_t param_id64;
    if (!reader.ReadVarInt62(&param_id64)) {
      *error_details = "Failed to parse transport parameter ID";
      return false;
    }
    TransportParameters::TransportParameterId param_id =
        static_cast<TransportParameters::TransportParameterId>(param_id64);
    absl::string_view value;
    if (!reader.ReadStringPieceVarInt62(&value)) {
      *error_details =
          "Failed to read length and value of transport parameter " +
          TransportParameterIdToString(param_id);
      return false;
    }
    QuicDataReader value_reader(value);
    bool parse_success = true;
    switch (param_id) {
      case TransportParameters::kOriginalDestinationConnectionId: {
        if (out->original_destination_connection_id.has_value()) {
          *error_details =
              "Received a second original_destination_connection_id";
          return false;
        }
        const size_t connection_id_length = value_reader.BytesRemaining();
        if (!QuicUtils::IsConnectionIdLengthValidForVersion(
                connection_id_length, version.transport_version)) {
          *error_details = absl::StrCat(
              "Received original_destination_connection_id of invalid length ",
              connection_id_length);
          return false;
        }
        QuicConnectionId original_destination_connection_id;
        if (!value_reader.ReadConnectionId(&original_destination_connection_id,
                                           connection_id_length)) {
          *error_details = "Failed to read original_destination_connection_id";
          return false;
        }
        out->original_destination_connection_id =
            original_destination_connection_id;
      } break;
      case TransportParameters::kMaxIdleTimeout:
        parse_success =
            out->max_idle_timeout_ms.Read(&value_reader, error_details);
        break;
      case TransportParameters::kStatelessResetToken: {
        if (!out->stateless_reset_token.empty()) {
          *error_details = "Received a second stateless_reset_token";
          return false;
        }
        absl::string_view stateless_reset_token =
            value_reader.ReadRemainingPayload();
        if (stateless_reset_token.length() != kStatelessResetTokenLength) {
          *error_details =
              absl::StrCat("Received stateless_reset_token of invalid length ",
                           stateless_reset_token.length());
          return false;
        }
        out->stateless_reset_token.assign(
            stateless_reset_token.data(),
            stateless_reset_token.data() + stateless_reset_token.length());
      } break;
      case TransportParameters::kMaxPacketSize:
        parse_success =
            out->max_udp_payload_size.Read(&value_reader, error_details);
        break;
      case TransportParameters::kInitialMaxData:
        parse_success =
            out->initial_max_data.Read(&value_reader, error_details);
        break;
      case TransportParameters::kInitialMaxStreamDataBidiLocal:
        parse_success = out->initial_max_stream_data_bidi_local.Read(
            &value_reader, error_details);
        break;
      case TransportParameters::kInitialMaxStreamDataBidiRemote:
        parse_success = out->initial_max_stream_data_bidi_remote.Read(
            &value_reader, error_details);
        break;
      case TransportParameters::kInitialMaxStreamDataUni:
        parse_success =
            out->initial_max_stream_data_uni.Read(&value_reader, error_details);
        break;
      case TransportParameters::kInitialMaxStreamsBidi:
        parse_success =
            out->initial_max_streams_bidi.Read(&value_reader, error_details);
        break;
      case TransportParameters::kInitialMaxStreamsUni:
        parse_success =
            out->initial_max_streams_uni.Read(&value_reader, error_details);
        break;
      case TransportParameters::kAckDelayExponent:
        parse_success =
            out->ack_delay_exponent.Read(&value_reader, error_details);
        break;
      case TransportParameters::kMaxAckDelay:
        parse_success = out->max_ack_delay.Read(&value_reader, error_details);
        break;
      case TransportParameters::kDisableActiveMigration:
        if (out->disable_active_migration) {
          *error_details = "Received a second disable_active_migration";
          return false;
        }
        out->disable_active_migration = true;
        break;
      case TransportParameters::kPreferredAddress: {
        TransportParameters::PreferredAddress preferred_address;
        uint16_t ipv4_port, ipv6_port;
        in_addr ipv4_address;
        in6_addr ipv6_address;
        preferred_address.stateless_reset_token.resize(
            kStatelessResetTokenLength);
        if (!value_reader.ReadBytes(&ipv4_address, sizeof(ipv4_address)) ||
            !value_reader.ReadUInt16(&ipv4_port) ||
            !value_reader.ReadBytes(&ipv6_address, sizeof(ipv6_address)) ||
            !value_reader.ReadUInt16(&ipv6_port) ||
            !value_reader.ReadLengthPrefixedConnectionId(
                &preferred_address.connection_id) ||
            !value_reader.ReadBytes(&preferred_address.stateless_reset_token[0],
                                    kStatelessResetTokenLength)) {
          *error_details = "Failed to read preferred_address";
          return false;
        }
        preferred_address.ipv4_socket_address =
            QuicSocketAddress(QuicIpAddress(ipv4_address), ipv4_port);
        preferred_address.ipv6_socket_address =
            QuicSocketAddress(QuicIpAddress(ipv6_address), ipv6_port);
        if (!preferred_address.ipv4_socket_address.host().IsIPv4() ||
            !preferred_address.ipv6_socket_address.host().IsIPv6()) {
          *error_details = "Received preferred_address of bad families " +
                           preferred_address.ToString();
          return false;
        }
        if (!QuicUtils::IsConnectionIdValidForVersion(
                preferred_address.connection_id, version.transport_version)) {
          *error_details = "Received invalid preferred_address connection ID " +
                           preferred_address.ToString();
          return false;
        }
        out->preferred_address =
            std::make_unique<TransportParameters::PreferredAddress>(
                preferred_address);
      } break;
      case TransportParameters::kActiveConnectionIdLimit:
        parse_success =
            out->active_connection_id_limit.Read(&value_reader, error_details);
        break;
      case TransportParameters::kInitialSourceConnectionId: {
        if (out->initial_source_connection_id.has_value()) {
          *error_details = "Received a second initial_source_connection_id";
          return false;
        }
        const size_t connection_id_length = value_reader.BytesRemaining();
        if (!QuicUtils::IsConnectionIdLengthValidForVersion(
                connection_id_length, version.transport_version)) {
          *error_details = absl::StrCat(
              "Received initial_source_connection_id of invalid length ",
              connection_id_length);
          return false;
        }
        QuicConnectionId initial_source_connection_id;
        if (!value_reader.ReadConnectionId(&initial_source_connection_id,
                                           connection_id_length)) {
          *error_details = "Failed to read initial_source_connection_id";
          return false;
        }
        out->initial_source_connection_id = initial_source_connection_id;
      } break;
      case TransportParameters::kRetrySourceConnectionId: {
        if (out->retry_source_connection_id.has_value()) {
          *error_details = "Received a second retry_source_connection_id";
          return false;
        }
        const size_t connection_id_length = value_reader.BytesRemaining();
        if (!QuicUtils::IsConnectionIdLengthValidForVersion(
                connection_id_length, version.transport_version)) {
          *error_details = absl::StrCat(
              "Received retry_source_connection_id of invalid length ",
              connection_id_length);
          return false;
        }
        QuicConnectionId retry_source_connection_id;
        if (!value_reader.ReadConnectionId(&retry_source_connection_id,
                                           connection_id_length)) {
          *error_details = "Failed to read retry_source_connection_id";
          return false;
        }
        out->retry_source_connection_id = retry_source_connection_id;
      } break;
      case TransportParameters::kMaxDatagramFrameSize:
        parse_success =
            out->max_datagram_frame_size.Read(&value_reader, error_details);
        break;
      case TransportParameters::kInitialRoundTripTime:
        parse_success =
            out->initial_round_trip_time_us.Read(&value_reader, error_details);
        break;
      case TransportParameters::kGoogleConnectionOptions: {
        if (out->google_connection_options.has_value()) {
          *error_details = "Received a second google_connection_options";
          return false;
        }
        out->google_connection_options = QuicTagVector{};
        while (!value_reader.IsDoneReading()) {
          QuicTag connection_option;
          if (!value_reader.ReadTag(&connection_option)) {
            *error_details = "Failed to read a google_connection_options";
            return false;
          }
          out->google_connection_options.value().push_back(connection_option);
        }
      } break;
      case TransportParameters::kGoogleUserAgentId:
        if (out->user_agent_id.has_value()) {
          *error_details = "Received a second user_agent_id";
          return false;
        }
        out->user_agent_id = std::string(value_reader.ReadRemainingPayload());
        break;
      case TransportParameters::kGoogleKeyUpdateNotYetSupported:
        if (out->key_update_not_yet_supported) {
          *error_details = "Received a second key_update_not_yet_supported";
          return false;
        }
        out->key_update_not_yet_supported = true;
        break;
      case TransportParameters::kGoogleQuicVersion: {
        if (!value_reader.ReadUInt32(&out->version)) {
          *error_details = "Failed to read Google version extension version";
          return false;
        }
        if (perspective == Perspective::IS_SERVER) {
          uint8_t versions_length;
          if (!value_reader.ReadUInt8(&versions_length)) {
            *error_details = "Failed to parse Google supported versions length";
            return false;
          }
          const uint8_t num_versions = versions_length / sizeof(uint32_t);
          for (uint8_t i = 0; i < num_versions; ++i) {
            QuicVersionLabel version;
            if (!value_reader.ReadUInt32(&version)) {
              *error_details = "Failed to parse Google supported version";
              return false;
            }
            out->supported_versions.push_back(version);
          }
        }
      } break;
      case TransportParameters::kMinAckDelay:
        parse_success =
            out->min_ack_delay_us.Read(&value_reader, error_details);
        break;
      default:
        if (out->custom_parameters.find(param_id) !=
            out->custom_parameters.end()) {
          *error_details = "Received a second unknown parameter" +
                           TransportParameterIdToString(param_id);
          return false;
        }
        out->custom_parameters[param_id] =
            std::string(value_reader.ReadRemainingPayload());
        break;
    }
    if (!parse_success) {
      QUICHE_DCHECK(!error_details->empty());
      return false;
    }
    if (!value_reader.IsDoneReading()) {
      *error_details = absl::StrCat(
          "Received unexpected ", value_reader.BytesRemaining(),
          " bytes after parsing ", TransportParameterIdToString(param_id));
      return false;
    }
  }

  if (!out->AreValid(error_details)) {
    QUICHE_DCHECK(!error_details->empty());
    return false;
  }

  QUIC_DLOG(INFO) << "Parsed transport parameters " << *out << " from "
                  << in_len << " bytes";

  return true;
}

namespace {

bool DigestUpdateIntegerParam(
    EVP_MD_CTX* hash_ctx,
    const TransportParameters::IntegerParameter& param) {
  uint64_t value = param.value();
  return EVP_DigestUpdate(hash_ctx, &value, sizeof(value));
}

}  // namespace

bool SerializeTransportParametersForTicket(
    const TransportParameters& in,
    const std::vector<uint8_t>& application_data,
    std::vector<uint8_t>* out) {
  std::string error_details;
  if (!in.AreValid(&error_details)) {
    QUIC_BUG << "Not serializing invalid transport parameters: "
             << error_details;
    return false;
  }

  out->resize(SHA256_DIGEST_LENGTH + 1);
  const uint8_t serialization_version = 0;
  (*out)[0] = serialization_version;

  bssl::ScopedEVP_MD_CTX hash_ctx;
  // Write application data:
  uint64_t app_data_len = application_data.size();
  const uint64_t parameter_version = 0;
  // The format of the input to the hash function is as follows:
  // - The application data, prefixed with a 64-bit length field.
  // - Transport parameters:
  //   - A 64-bit version field indicating which version of encoding is used
  //     for transport parameters.
  //   - A list of 64-bit integers representing the relevant parameters.
  //
  //   When changing which parameters are included, additional parameters can be
  //   added to the end of the list without changing the version field. New
  //   parameters that are variable length must be length prefixed. If
  //   parameters are removed from the list, the version field must be
  //   incremented.
  //
  // Integers happen to be written in host byte order, not network byte order.
  if (!EVP_DigestInit(hash_ctx.get(), EVP_sha256()) ||
      !EVP_DigestUpdate(hash_ctx.get(), &app_data_len, sizeof(app_data_len)) ||
      !EVP_DigestUpdate(hash_ctx.get(), application_data.data(),
                        application_data.size()) ||
      !EVP_DigestUpdate(hash_ctx.get(), &parameter_version,
                        sizeof(parameter_version))) {
    QUIC_BUG << "Unexpected failure of EVP_Digest functions when hashing "
                "Transport Parameters for ticket";
    return false;
  }

  // Write transport parameters specified by draft-ietf-quic-transport-28,
  // section 7.4.1, that are remembered for 0-RTT.
  if (!DigestUpdateIntegerParam(hash_ctx.get(), in.initial_max_data) ||
      !DigestUpdateIntegerParam(hash_ctx.get(),
                                in.initial_max_stream_data_bidi_local) ||
      !DigestUpdateIntegerParam(hash_ctx.get(),
                                in.initial_max_stream_data_bidi_remote) ||
      !DigestUpdateIntegerParam(hash_ctx.get(),
                                in.initial_max_stream_data_uni) ||
      !DigestUpdateIntegerParam(hash_ctx.get(), in.initial_max_streams_bidi) ||
      !DigestUpdateIntegerParam(hash_ctx.get(), in.initial_max_streams_uni) ||
      !DigestUpdateIntegerParam(hash_ctx.get(),
                                in.active_connection_id_limit)) {
    QUIC_BUG << "Unexpected failure of EVP_Digest functions when hashing "
                "Transport Parameters for ticket";
    return false;
  }
  uint8_t disable_active_migration = in.disable_active_migration ? 1 : 0;
  if (!EVP_DigestUpdate(hash_ctx.get(), &disable_active_migration,
                        sizeof(disable_active_migration)) ||
      !EVP_DigestFinal(hash_ctx.get(), out->data() + 1, nullptr)) {
    QUIC_BUG << "Unexpected failure of EVP_Digest functions when hashing "
                "Transport Parameters for ticket";
    return false;
  }
  return true;
}

}  // namespace quic
