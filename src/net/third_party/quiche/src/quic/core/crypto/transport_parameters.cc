// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"

#include <cstdint>
#include <cstring>
#include <forward_list>
#include <utility>

#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {

// Values of the TransportParameterId enum as defined in the
// "Transport Parameter Encoding" section of draft-ietf-quic-transport.
// When parameters are encoded, one of these enum values is used to indicate
// which parameter is encoded. The supported draft version is noted in
// transport_parameters.h.
enum TransportParameters::TransportParameterId : uint16_t {
  kOriginalConnectionId = 0,
  kIdleTimeout = 1,
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
  kDisableMigration = 0xc,
  kPreferredAddress = 0xd,
  kActiveConnectionIdLimit = 0xe,

  kMaxDatagramFrameSize = 0x20,

  kGoogleQuicParam = 18257,  // Used for non-standard Google-specific params.
  kGoogleQuicVersion =
      18258,  // Used to transmit version and supported_versions.
};

namespace {

// The following constants define minimum and maximum allowed values for some of
// the parameters. These come from the "Transport Parameter Definitions"
// section of draft-ietf-quic-transport.
const uint64_t kMinMaxPacketSizeTransportParam = 1200;
const uint64_t kDefaultMaxPacketSizeTransportParam = 65527;
const uint64_t kMaxAckDelayExponentTransportParam = 20;
const uint64_t kDefaultAckDelayExponentTransportParam = 3;
const uint64_t kMaxMaxAckDelayTransportParam = 16383;
const uint64_t kDefaultMaxAckDelayTransportParam = 25;
const size_t kStatelessResetTokenLength = 16;

std::string TransportParameterIdToString(
    TransportParameters::TransportParameterId param_id) {
  switch (param_id) {
    case TransportParameters::kOriginalConnectionId:
      return "original_connection_id";
    case TransportParameters::kIdleTimeout:
      return "idle_timeout";
    case TransportParameters::kStatelessResetToken:
      return "stateless_reset_token";
    case TransportParameters::kMaxPacketSize:
      return "max_packet_size";
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
    case TransportParameters::kDisableMigration:
      return "disable_migration";
    case TransportParameters::kPreferredAddress:
      return "preferred_address";
    case TransportParameters::kActiveConnectionIdLimit:
      return "active_connection_id_limit";
    case TransportParameters::kMaxDatagramFrameSize:
      return "max_datagram_frame_size";
    case TransportParameters::kGoogleQuicParam:
      return "google";
    case TransportParameters::kGoogleQuicVersion:
      return "google-version";
  }
  return "Unknown(" + QuicTextUtils::Uint64ToString(param_id) + ")";
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
      has_been_read_from_cbs_(false) {
  DCHECK_LE(min_value, default_value);
  DCHECK_LE(default_value, max_value);
  DCHECK_LE(max_value, kVarInt62MaxValue);
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

bool TransportParameters::IntegerParameter::WriteToCbb(CBB* parent_cbb) const {
  DCHECK(IsValid());
  if (value_ == default_value_) {
    // Do not write if the value is default.
    return true;
  }
  uint8_t encoded_data[sizeof(uint64_t)] = {};
  QuicDataWriter writer(sizeof(encoded_data),
                        reinterpret_cast<char*>(encoded_data));
  writer.WriteVarInt62(value_);
  const uint16_t value_length = writer.length();
  DCHECK_LE(value_length, sizeof(encoded_data));
  const bool ok = CBB_add_u16(parent_cbb, param_id_) &&
                  CBB_add_u16(parent_cbb, value_length) &&
                  CBB_add_bytes(parent_cbb, encoded_data, value_length);
  QUIC_BUG_IF(!ok) << "Failed to write " << this;
  return ok;
}

bool TransportParameters::IntegerParameter::ReadFromCbs(CBS* const value_cbs) {
  if (has_been_read_from_cbs_) {
    QUIC_DLOG(ERROR) << "Received a second "
                     << TransportParameterIdToString(param_id_);
    return false;
  }
  has_been_read_from_cbs_ = true;
  QuicDataReader reader(reinterpret_cast<const char*>(CBS_data(value_cbs)),
                        CBS_len(value_cbs));
  QuicVariableLengthIntegerLength value_length = reader.PeekVarInt62Length();
  if (value_length == 0 || !reader.ReadVarInt62(&value_)) {
    QUIC_DLOG(ERROR) << "Failed to parse value for "
                     << TransportParameterIdToString(param_id_);
    return false;
  }
  if (!reader.IsDoneReading()) {
    QUIC_DLOG(ERROR) << "Received unexpected " << reader.BytesRemaining()
                     << " bytes after parsing " << this;
    return false;
  }
  if (!CBS_skip(value_cbs, value_length)) {
    QUIC_BUG << "Failed to advance CBS past value for " << this;
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
  rv += QuicTextUtils::Uint64ToString(value_);
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
         QuicTextUtils::HexEncode(
             reinterpret_cast<const char*>(stateless_reset_token.data()),
             stateless_reset_token.size()) +
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
  if (!original_connection_id.IsEmpty()) {
    rv += " " + TransportParameterIdToString(kOriginalConnectionId) + " " +
          original_connection_id.ToString();
  }
  rv += idle_timeout_milliseconds.ToString(/*for_use_in_list=*/true);
  if (!stateless_reset_token.empty()) {
    rv += " " + TransportParameterIdToString(kStatelessResetToken) + " " +
          QuicTextUtils::HexEncode(
              reinterpret_cast<const char*>(stateless_reset_token.data()),
              stateless_reset_token.size());
  }
  rv += max_packet_size.ToString(/*for_use_in_list=*/true);
  rv += initial_max_data.ToString(/*for_use_in_list=*/true);
  rv += initial_max_stream_data_bidi_local.ToString(/*for_use_in_list=*/true);
  rv += initial_max_stream_data_bidi_remote.ToString(/*for_use_in_list=*/true);
  rv += initial_max_stream_data_uni.ToString(/*for_use_in_list=*/true);
  rv += initial_max_streams_bidi.ToString(/*for_use_in_list=*/true);
  rv += initial_max_streams_uni.ToString(/*for_use_in_list=*/true);
  rv += ack_delay_exponent.ToString(/*for_use_in_list=*/true);
  rv += max_ack_delay.ToString(/*for_use_in_list=*/true);
  if (disable_migration) {
    rv += " " + TransportParameterIdToString(kDisableMigration);
  }
  if (preferred_address) {
    rv += " " + TransportParameterIdToString(kPreferredAddress) + " " +
          preferred_address->ToString();
  }
  rv += active_connection_id_limit.ToString(/*for_use_in_list=*/true);
  rv += max_datagram_frame_size.ToString(/*for_use_in_list=*/true);
  if (google_quic_params) {
    rv += " " + TransportParameterIdToString(kGoogleQuicParam);
  }
  rv += "]";
  for (const auto& kv : custom_parameters) {
    rv += " 0x" + QuicTextUtils::Hex(static_cast<uint32_t>(kv.first));
    rv += "=" + QuicTextUtils::HexEncode(kv.second);
  }
  return rv;
}

TransportParameters::TransportParameters()
    : version(0),
      original_connection_id(EmptyQuicConnectionId()),
      idle_timeout_milliseconds(kIdleTimeout),
      max_packet_size(kMaxPacketSize,
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
      disable_migration(false),
      active_connection_id_limit(kActiveConnectionIdLimit),
      max_datagram_frame_size(kMaxDatagramFrameSize)
// Important note: any new transport parameters must be added
// to TransportParameters::AreValid, SerializeTransportParameters and
// ParseTransportParameters.
{}

bool TransportParameters::AreValid() const {
  DCHECK(perspective == Perspective::IS_CLIENT ||
         perspective == Perspective::IS_SERVER);
  if (perspective == Perspective::IS_CLIENT && !stateless_reset_token.empty()) {
    QUIC_DLOG(ERROR) << "Client cannot send stateless reset token";
    return false;
  }
  if (perspective == Perspective::IS_CLIENT &&
      !original_connection_id.IsEmpty()) {
    QUIC_DLOG(ERROR) << "Client cannot send original connection ID";
    return false;
  }
  if (!stateless_reset_token.empty() &&
      stateless_reset_token.size() != kStatelessResetTokenLength) {
    QUIC_DLOG(ERROR) << "Stateless reset token has bad length "
                     << stateless_reset_token.size();
    return false;
  }
  if (perspective == Perspective::IS_CLIENT && preferred_address) {
    QUIC_DLOG(ERROR) << "Client cannot send preferred address";
    return false;
  }
  if (preferred_address && preferred_address->stateless_reset_token.size() !=
                               kStatelessResetTokenLength) {
    QUIC_DLOG(ERROR)
        << "Preferred address stateless reset token has bad length "
        << preferred_address->stateless_reset_token.size();
    return false;
  }
  if (preferred_address &&
      (!preferred_address->ipv4_socket_address.host().IsIPv4() ||
       !preferred_address->ipv6_socket_address.host().IsIPv6())) {
    QUIC_BUG << "Preferred address family failure";
    return false;
  }
  const bool ok =
      idle_timeout_milliseconds.IsValid() && max_packet_size.IsValid() &&
      initial_max_data.IsValid() &&
      initial_max_stream_data_bidi_local.IsValid() &&
      initial_max_stream_data_bidi_remote.IsValid() &&
      initial_max_stream_data_uni.IsValid() &&
      initial_max_streams_bidi.IsValid() && initial_max_streams_uni.IsValid() &&
      ack_delay_exponent.IsValid() && max_ack_delay.IsValid() &&
      active_connection_id_limit.IsValid() && max_datagram_frame_size.IsValid();
  if (!ok) {
    QUIC_DLOG(ERROR) << "Invalid transport parameters " << *this;
  }
  return ok;
}

TransportParameters::~TransportParameters() = default;

bool SerializeTransportParameters(ParsedQuicVersion /*version*/,
                                  const TransportParameters& in,
                                  std::vector<uint8_t>* out) {
  if (!in.AreValid()) {
    QUIC_DLOG(ERROR) << "Not serializing invalid transport parameters " << in;
    return false;
  }
  if (in.version == 0 || (in.perspective == Perspective::IS_SERVER &&
                          in.supported_versions.empty())) {
    QUIC_DLOG(ERROR) << "Refusing to serialize without versions";
    return false;
  }

  bssl::ScopedCBB cbb;
  // Empirically transport parameters generally fit within 128 bytes.
  // The CBB will grow to fit larger serializations if required.
  if (!CBB_init(cbb.get(), 128)) {
    QUIC_BUG << "Failed to initialize CBB for " << in;
    return false;
  }

  CBB params;
  // Add length of the transport parameters list.
  if (!CBB_add_u16_length_prefixed(cbb.get(), &params)) {
    QUIC_BUG << "Failed to write parameter length for " << in;
    return false;
  }

  // original_connection_id
  CBB original_connection_id_param;
  if (!in.original_connection_id.IsEmpty()) {
    DCHECK_EQ(Perspective::IS_SERVER, in.perspective);
    if (!CBB_add_u16(&params, TransportParameters::kOriginalConnectionId) ||
        !CBB_add_u16_length_prefixed(&params, &original_connection_id_param) ||
        !CBB_add_bytes(
            &original_connection_id_param,
            reinterpret_cast<const uint8_t*>(in.original_connection_id.data()),
            in.original_connection_id.length())) {
      QUIC_BUG << "Failed to write original_connection_id "
               << in.original_connection_id << " for " << in;
      return false;
    }
  }

  if (!in.idle_timeout_milliseconds.WriteToCbb(&params)) {
    QUIC_BUG << "Failed to write idle_timeout for " << in;
    return false;
  }

  // stateless_reset_token
  CBB stateless_reset_token_param;
  if (!in.stateless_reset_token.empty()) {
    DCHECK_EQ(kStatelessResetTokenLength, in.stateless_reset_token.size());
    DCHECK_EQ(Perspective::IS_SERVER, in.perspective);
    if (!CBB_add_u16(&params, TransportParameters::kStatelessResetToken) ||
        !CBB_add_u16_length_prefixed(&params, &stateless_reset_token_param) ||
        !CBB_add_bytes(&stateless_reset_token_param,
                       in.stateless_reset_token.data(),
                       in.stateless_reset_token.size())) {
      QUIC_BUG << "Failed to write stateless_reset_token of length "
               << in.stateless_reset_token.size() << " for " << in;
      return false;
    }
  }

  if (!in.max_packet_size.WriteToCbb(&params) ||
      !in.initial_max_data.WriteToCbb(&params) ||
      !in.initial_max_stream_data_bidi_local.WriteToCbb(&params) ||
      !in.initial_max_stream_data_bidi_remote.WriteToCbb(&params) ||
      !in.initial_max_stream_data_uni.WriteToCbb(&params) ||
      !in.initial_max_streams_bidi.WriteToCbb(&params) ||
      !in.initial_max_streams_uni.WriteToCbb(&params) ||
      !in.ack_delay_exponent.WriteToCbb(&params) ||
      !in.max_ack_delay.WriteToCbb(&params) ||
      !in.active_connection_id_limit.WriteToCbb(&params) ||
      !in.max_datagram_frame_size.WriteToCbb(&params)) {
    QUIC_BUG << "Failed to write integers for " << in;
    return false;
  }

  // disable_migration
  if (in.disable_migration) {
    if (!CBB_add_u16(&params, TransportParameters::kDisableMigration) ||
        !CBB_add_u16(&params, 0u)) {  // 0 is the length of this parameter.
      QUIC_BUG << "Failed to write disable_migration for " << in;
      return false;
    }
  }

  // preferred_address
  CBB preferred_address_params, preferred_address_connection_id_param;
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
    if (!CBB_add_u16(&params, TransportParameters::kPreferredAddress) ||
        !CBB_add_u16_length_prefixed(&params, &preferred_address_params) ||
        !CBB_add_bytes(
            &preferred_address_params,
            reinterpret_cast<const uint8_t*>(v4_address_bytes.data()),
            v4_address_bytes.length()) ||
        !CBB_add_u16(&preferred_address_params,
                     in.preferred_address->ipv4_socket_address.port()) ||
        !CBB_add_bytes(
            &preferred_address_params,
            reinterpret_cast<const uint8_t*>(v6_address_bytes.data()),
            v6_address_bytes.length()) ||
        !CBB_add_u16(&preferred_address_params,
                     in.preferred_address->ipv6_socket_address.port()) ||
        !CBB_add_u8_length_prefixed(&preferred_address_params,
                                    &preferred_address_connection_id_param) ||
        !CBB_add_bytes(&preferred_address_connection_id_param,
                       reinterpret_cast<const uint8_t*>(
                           in.preferred_address->connection_id.data()),
                       in.preferred_address->connection_id.length()) ||
        !CBB_add_bytes(&preferred_address_params,
                       in.preferred_address->stateless_reset_token.data(),
                       in.preferred_address->stateless_reset_token.size())) {
      QUIC_BUG << "Failed to write preferred_address for " << in;
      return false;
    }
  }

  // Google-specific non-standard parameter.
  CBB google_quic_params;
  if (in.google_quic_params) {
    const QuicData& serialized_google_quic_params =
        in.google_quic_params->GetSerialized();
    if (!CBB_add_u16(&params, TransportParameters::kGoogleQuicParam) ||
        !CBB_add_u16_length_prefixed(&params, &google_quic_params) ||
        !CBB_add_bytes(&google_quic_params,
                       reinterpret_cast<const uint8_t*>(
                           serialized_google_quic_params.data()),
                       serialized_google_quic_params.length())) {
      QUIC_BUG << "Failed to write Google params of length "
               << serialized_google_quic_params.length() << " for " << in;
      return false;
    }
  }

  // Google-specific version extension.
  CBB google_version_params;
  if (!CBB_add_u16(&params, TransportParameters::kGoogleQuicVersion) ||
      !CBB_add_u16_length_prefixed(&params, &google_version_params) ||
      !CBB_add_u32(&google_version_params, in.version)) {
    QUIC_BUG << "Failed to write Google version extension for " << in;
    return false;
  }
  CBB versions;
  if (in.perspective == Perspective::IS_SERVER) {
    if (!CBB_add_u8_length_prefixed(&google_version_params, &versions)) {
      QUIC_BUG << "Failed to write versions length for " << in;
      return false;
    }
    for (QuicVersionLabel version : in.supported_versions) {
      if (!CBB_add_u32(&versions, version)) {
        QUIC_BUG << "Failed to write supported version for " << in;
        return false;
      }
    }
  }

  auto custom_parameters = std::make_unique<CBB[]>(in.custom_parameters.size());
  int i = 0;
  for (const auto& kv : in.custom_parameters) {
    CBB* custom_parameter = &custom_parameters[i++];
    QUIC_BUG_IF(kv.first < 0xff00) << "custom_parameters should not be used "
                                      "for non-private use parameters";
    if (!CBB_add_u16(&params, kv.first) ||
        !CBB_add_u16_length_prefixed(&params, custom_parameter) ||
        !CBB_add_bytes(custom_parameter,
                       reinterpret_cast<const uint8_t*>(kv.second.data()),
                       kv.second.size())) {
      QUIC_BUG << "Failed to write custom parameter "
               << static_cast<int>(kv.first);
      return false;
    }
  }

  if (!CBB_flush(cbb.get())) {
    QUIC_BUG << "Failed to flush CBB for " << in;
    return false;
  }
  out->resize(CBB_len(cbb.get()));
  memcpy(out->data(), CBB_data(cbb.get()), CBB_len(cbb.get()));
  QUIC_DLOG(INFO) << "Serialized " << in << " as " << CBB_len(cbb.get())
                  << " bytes";
  return true;
}

bool ParseTransportParameters(ParsedQuicVersion version,
                              Perspective perspective,
                              const uint8_t* in,
                              size_t in_len,
                              TransportParameters* out) {
  out->perspective = perspective;
  CBS cbs;
  CBS_init(&cbs, in, in_len);

  CBS params;
  if (!CBS_get_u16_length_prefixed(&cbs, &params)) {
    QUIC_DLOG(ERROR) << "Failed to parse the number of transport parameters";
    return false;
  }

  while (CBS_len(&params) > 0) {
    TransportParameters::TransportParameterId param_id;
    CBS value;
    static_assert(sizeof(param_id) == sizeof(uint16_t), "bad size");
    if (!CBS_get_u16(&params, reinterpret_cast<uint16_t*>(&param_id))) {
      QUIC_DLOG(ERROR) << "Failed to parse transport parameter ID";
      return false;
    }
    if (!CBS_get_u16_length_prefixed(&params, &value)) {
      QUIC_DLOG(ERROR) << "Failed to parse length of transport parameter "
                       << TransportParameterIdToString(param_id);
      return false;
    }
    bool parse_success = true;
    switch (param_id) {
      case TransportParameters::kOriginalConnectionId: {
        if (!out->original_connection_id.IsEmpty()) {
          QUIC_DLOG(ERROR) << "Received a second original connection ID";
          return false;
        }
        const size_t connection_id_length = CBS_len(&value);
        if (!QuicUtils::IsConnectionIdLengthValidForVersion(
                connection_id_length, version.transport_version)) {
          QUIC_DLOG(ERROR) << "Received original connection ID of "
                           << "invalid length " << connection_id_length;
          return false;
        }
        out->original_connection_id.set_length(
            static_cast<uint8_t>(connection_id_length));
        if (out->original_connection_id.length() != 0) {
          memcpy(out->original_connection_id.mutable_data(), CBS_data(&value),
                 out->original_connection_id.length());
        }
      } break;
      case TransportParameters::kIdleTimeout:
        parse_success = out->idle_timeout_milliseconds.ReadFromCbs(&value);
        break;
      case TransportParameters::kStatelessResetToken:
        if (!out->stateless_reset_token.empty()) {
          QUIC_DLOG(ERROR) << "Received a second stateless reset token";
          return false;
        }
        if (CBS_len(&value) != kStatelessResetTokenLength) {
          QUIC_DLOG(ERROR) << "Received stateless reset token of "
                           << "invalid length " << CBS_len(&value);
          return false;
        }
        out->stateless_reset_token.assign(CBS_data(&value),
                                          CBS_data(&value) + CBS_len(&value));
        break;
      case TransportParameters::kMaxPacketSize:
        parse_success = out->max_packet_size.ReadFromCbs(&value);
        break;
      case TransportParameters::kInitialMaxData:
        parse_success = out->initial_max_data.ReadFromCbs(&value);
        break;
      case TransportParameters::kInitialMaxStreamDataBidiLocal:
        parse_success =
            out->initial_max_stream_data_bidi_local.ReadFromCbs(&value);
        break;
      case TransportParameters::kInitialMaxStreamDataBidiRemote:
        parse_success =
            out->initial_max_stream_data_bidi_remote.ReadFromCbs(&value);
        break;
      case TransportParameters::kInitialMaxStreamDataUni:
        parse_success = out->initial_max_stream_data_uni.ReadFromCbs(&value);
        break;
      case TransportParameters::kInitialMaxStreamsBidi:
        parse_success = out->initial_max_streams_bidi.ReadFromCbs(&value);
        break;
      case TransportParameters::kInitialMaxStreamsUni:
        parse_success = out->initial_max_streams_uni.ReadFromCbs(&value);
        break;
      case TransportParameters::kAckDelayExponent:
        parse_success = out->ack_delay_exponent.ReadFromCbs(&value);
        break;
      case TransportParameters::kMaxAckDelay:
        parse_success = out->max_ack_delay.ReadFromCbs(&value);
        break;
      case TransportParameters::kDisableMigration:
        if (out->disable_migration) {
          QUIC_DLOG(ERROR) << "Received a second disable migration";
          return false;
        }
        if (CBS_len(&value) != 0) {
          QUIC_DLOG(ERROR) << "Received disable migration of invalid length "
                           << CBS_len(&value);
          return false;
        }
        out->disable_migration = true;
        break;
      case TransportParameters::kPreferredAddress: {
        uint16_t ipv4_port, ipv6_port;
        in_addr ipv4_address;
        in6_addr ipv6_address;
        if (!CBS_copy_bytes(&value, reinterpret_cast<uint8_t*>(&ipv4_address),
                            sizeof(ipv4_address)) ||
            !CBS_get_u16(&value, &ipv4_port) ||
            !CBS_copy_bytes(&value, reinterpret_cast<uint8_t*>(&ipv6_address),
                            sizeof(ipv6_address)) ||
            !CBS_get_u16(&value, &ipv6_port)) {
          QUIC_DLOG(ERROR) << "Failed to parse preferred address IPs and ports";
          return false;
        }
        TransportParameters::PreferredAddress preferred_address;
        preferred_address.ipv4_socket_address =
            QuicSocketAddress(QuicIpAddress(ipv4_address), ipv4_port);
        preferred_address.ipv6_socket_address =
            QuicSocketAddress(QuicIpAddress(ipv6_address), ipv6_port);
        if (!preferred_address.ipv4_socket_address.host().IsIPv4() ||
            !preferred_address.ipv6_socket_address.host().IsIPv6()) {
          QUIC_DLOG(ERROR) << "Received preferred addresses of bad families "
                           << preferred_address;
          return false;
        }
        CBS connection_id_cbs;
        if (!CBS_get_u8_length_prefixed(&value, &connection_id_cbs)) {
          QUIC_DLOG(ERROR)
              << "Failed to parse length of preferred address connection ID";
          return false;
        }
        const size_t connection_id_length = CBS_len(&connection_id_cbs);
        if (!QuicUtils::IsConnectionIdLengthValidForVersion(
                connection_id_length, version.transport_version)) {
          QUIC_DLOG(ERROR) << "Received preferred address connection ID of "
                           << "invalid length " << connection_id_length;
          return false;
        }
        preferred_address.connection_id.set_length(
            static_cast<uint8_t>(connection_id_length));
        if (preferred_address.connection_id.length() > 0 &&
            !CBS_copy_bytes(&connection_id_cbs,
                            reinterpret_cast<uint8_t*>(
                                preferred_address.connection_id.mutable_data()),
                            preferred_address.connection_id.length())) {
          QUIC_DLOG(ERROR) << "Failed to read preferred address connection ID";
          return false;
        }
        if (CBS_len(&value) != kStatelessResetTokenLength) {
          QUIC_DLOG(ERROR) << "Received preferred address with "
                           << "invalid remaining length " << CBS_len(&value);
          return false;
        }
        preferred_address.stateless_reset_token.assign(
            CBS_data(&value), CBS_data(&value) + CBS_len(&value));
        out->preferred_address =
            std::make_unique<TransportParameters::PreferredAddress>(
                preferred_address);
      } break;
      case TransportParameters::kActiveConnectionIdLimit:
        parse_success = out->active_connection_id_limit.ReadFromCbs(&value);
        break;
      case TransportParameters::kMaxDatagramFrameSize:
        parse_success = out->max_datagram_frame_size.ReadFromCbs(&value);
        break;
      case TransportParameters::kGoogleQuicParam: {
        if (out->google_quic_params) {
          QUIC_DLOG(ERROR) << "Received a second Google parameter";
          return false;
        }
        QuicStringPiece serialized_params(
            reinterpret_cast<const char*>(CBS_data(&value)), CBS_len(&value));
        out->google_quic_params = CryptoFramer::ParseMessage(serialized_params);
      } break;
      case TransportParameters::kGoogleQuicVersion: {
        if (!CBS_get_u32(&value, &out->version)) {
          QUIC_DLOG(ERROR) << "Failed to parse Google version extension";
          return false;
        }
        if (perspective == Perspective::IS_SERVER) {
          CBS versions;
          if (!CBS_get_u8_length_prefixed(&value, &versions) ||
              CBS_len(&versions) % 4 != 0) {
            QUIC_DLOG(ERROR)
                << "Failed to parse Google supported versions length";
            return false;
          }
          while (CBS_len(&versions) > 0) {
            QuicVersionLabel version;
            if (!CBS_get_u32(&versions, &version)) {
              QUIC_DLOG(ERROR) << "Failed to parse Google supported version";
              return false;
            }
            out->supported_versions.push_back(version);
          }
        }
      } break;
      default:
        out->custom_parameters[param_id] = std::string(
            reinterpret_cast<const char*>(CBS_data(&value)), CBS_len(&value));
        break;
    }
    if (!parse_success) {
      return false;
    }
  }

  const bool ok = out->AreValid();
  if (ok) {
    QUIC_DLOG(INFO) << "Parsed transport parameters " << *out << " from "
                    << in_len << " bytes";
  } else {
    QUIC_DLOG(ERROR) << "Transport parameter validity check failed " << *out
                     << " from " << in_len << " bytes";
  }
  return ok;
}

}  // namespace quic
