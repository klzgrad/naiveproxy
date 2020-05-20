// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"

#include <cstdint>
#include <cstring>
#include <forward_list>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {

// Values of the TransportParameterId enum as defined in the
// "Transport Parameter Encoding" section of draft-ietf-quic-transport.
// When parameters are encoded, one of these enum values is used to indicate
// which parameter is encoded. The supported draft version is noted in
// transport_parameters.h.
enum TransportParameters::TransportParameterId : uint64_t {
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
  return "Unknown(" + quiche::QuicheTextUtils::Uint64ToString(param_id) + ")";
}

bool WriteTransportParameterId(
    QuicDataWriter* writer,
    TransportParameters::TransportParameterId param_id,
    ParsedQuicVersion version) {
  if (version.HasVarIntTransportParams()) {
    if (!writer->WriteVarInt62(param_id)) {
      QUIC_BUG << "Failed to write param_id for "
               << TransportParameterIdToString(param_id);
      return false;
    }
  } else {
    if (static_cast<uint64_t>(param_id) >
        std::numeric_limits<uint16_t>::max()) {
      QUIC_BUG << "Cannot serialize transport parameter "
               << TransportParameterIdToString(param_id) << " with version "
               << version;
      return false;
    }
    if (!writer->WriteUInt16(param_id)) {
      QUIC_BUG << "Failed to write param_id16 for "
               << TransportParameterIdToString(param_id);
      return false;
    }
  }
  return true;
}

bool WriteTransportParameterLength(QuicDataWriter* writer,
                                   uint64_t length,
                                   ParsedQuicVersion version) {
  if (version.HasVarIntTransportParams()) {
    return writer->WriteVarInt62(length);
  }
  if (length > std::numeric_limits<uint16_t>::max()) {
    QUIC_BUG << "Cannot serialize transport parameter length " << length
             << " with version " << version;
    return false;
  }
  return writer->WriteUInt16(length);
}

bool WriteTransportParameterStringPiece(QuicDataWriter* writer,
                                        quiche::QuicheStringPiece value,
                                        ParsedQuicVersion version) {
  if (version.HasVarIntTransportParams()) {
    return writer->WriteStringPieceVarInt62(value);
  }
  return writer->WriteStringPiece16(value);
}

bool ReadTransportParameterId(
    QuicDataReader* reader,
    ParsedQuicVersion version,
    TransportParameters::TransportParameterId* out_param_id) {
  if (version.HasVarIntTransportParams()) {
    uint64_t param_id64;
    if (!reader->ReadVarInt62(&param_id64)) {
      return false;
    }
    *out_param_id =
        static_cast<TransportParameters::TransportParameterId>(param_id64);
  } else {
    uint16_t param_id16;
    if (!reader->ReadUInt16(&param_id16)) {
      return false;
    }
    *out_param_id =
        static_cast<TransportParameters::TransportParameterId>(param_id16);
  }
  return true;
}

bool ReadTransportParameterLengthAndValue(
    QuicDataReader* reader,
    ParsedQuicVersion version,
    quiche::QuicheStringPiece* out_value) {
  if (version.HasVarIntTransportParams()) {
    return reader->ReadStringPieceVarInt62(out_value);
  }
  return reader->ReadStringPiece16(out_value);
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

bool TransportParameters::IntegerParameter::Write(
    QuicDataWriter* writer,
    ParsedQuicVersion version) const {
  DCHECK(IsValid());
  if (value_ == default_value_) {
    // Do not write if the value is default.
    return true;
  }
  if (!WriteTransportParameterId(writer, param_id_, version)) {
    QUIC_BUG << "Failed to write param_id for " << *this;
    return false;
  }
  const QuicVariableLengthIntegerLength value_length =
      QuicDataWriter::GetVarInt62Len(value_);
  if (version.HasVarIntTransportParams()) {
    if (!writer->WriteVarInt62(value_length)) {
      QUIC_BUG << "Failed to write value_length for " << *this;
      return false;
    }
  } else {
    if (!writer->WriteUInt16(value_length)) {
      QUIC_BUG << "Failed to write value_length16 for " << *this;
      return false;
    }
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
        quiche::QuicheStrCat("Received unexpected ", reader->BytesRemaining(),
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
         quiche::QuicheTextUtils::HexEncode(
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
          quiche::QuicheTextUtils::HexEncode(
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
    rv += " 0x" + quiche::QuicheTextUtils::Hex(static_cast<uint32_t>(kv.first));
    rv += "=" + quiche::QuicheTextUtils::HexEncode(kv.second);
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

bool TransportParameters::AreValid(std::string* error_details) const {
  DCHECK(perspective == Perspective::IS_CLIENT ||
         perspective == Perspective::IS_SERVER);
  if (perspective == Perspective::IS_CLIENT && !stateless_reset_token.empty()) {
    *error_details = "Client cannot send stateless reset token";
    return false;
  }
  if (perspective == Perspective::IS_CLIENT &&
      !original_connection_id.IsEmpty()) {
    *error_details = "Client cannot send original connection ID";
    return false;
  }
  if (!stateless_reset_token.empty() &&
      stateless_reset_token.size() != kStatelessResetTokenLength) {
    *error_details = quiche::QuicheStrCat(
        "Stateless reset token has bad length ", stateless_reset_token.size());
    return false;
  }
  if (perspective == Perspective::IS_CLIENT && preferred_address) {
    *error_details = "Client cannot send preferred address";
    return false;
  }
  if (preferred_address && preferred_address->stateless_reset_token.size() !=
                               kStatelessResetTokenLength) {
    *error_details = quiche::QuicheStrCat(
        "Preferred address stateless reset token has bad length ",
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
    *error_details = "Invalid transport parameters " + this->ToString();
  }
  return ok;
}

TransportParameters::~TransportParameters() = default;

bool SerializeTransportParameters(ParsedQuicVersion version,
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

  // Empirically transport parameters generally fit within 128 bytes.
  // For now we hope this will be enough.
  // TODO(dschinazi) make this grow if needed.
  static const size_t kMaxTransportParametersLength = 4096;
  out->resize(kMaxTransportParametersLength);
  QuicDataWriter writer(out->size(), reinterpret_cast<char*>(out->data()));

  if (!version.HasVarIntTransportParams()) {
    // Versions that do not use variable integer transport parameters carry
    // a 16-bit length of the remaining transport parameters. We write 0 here
    // to reserve 16 bits, and we fill it in at the end of this function.
    // TODO(b/150465921) add support for doing this in QuicDataWriter.
    if (!writer.WriteUInt16(0)) {
      QUIC_BUG << "Failed to write transport parameter fake length prefix for "
               << in;
      return false;
    }
  }

  // original_connection_id
  if (!in.original_connection_id.IsEmpty()) {
    DCHECK_EQ(Perspective::IS_SERVER, in.perspective);
    if (!WriteTransportParameterId(
            &writer, TransportParameters::kOriginalConnectionId, version) ||
        !WriteTransportParameterStringPiece(
            &writer,
            quiche::QuicheStringPiece(in.original_connection_id.data(),
                                      in.original_connection_id.length()),
            version)) {
      QUIC_BUG << "Failed to write original_connection_id "
               << in.original_connection_id << " for " << in;
      return false;
    }
  }

  if (!in.idle_timeout_milliseconds.Write(&writer, version)) {
    QUIC_BUG << "Failed to write idle_timeout for " << in;
    return false;
  }

  // stateless_reset_token
  if (!in.stateless_reset_token.empty()) {
    DCHECK_EQ(kStatelessResetTokenLength, in.stateless_reset_token.size());
    DCHECK_EQ(Perspective::IS_SERVER, in.perspective);
    if (!WriteTransportParameterId(
            &writer, TransportParameters::kStatelessResetToken, version) ||
        !WriteTransportParameterStringPiece(
            &writer,
            quiche::QuicheStringPiece(
                reinterpret_cast<const char*>(in.stateless_reset_token.data()),
                in.stateless_reset_token.size()),
            version)) {
      QUIC_BUG << "Failed to write stateless_reset_token of length "
               << in.stateless_reset_token.size() << " for " << in;
      return false;
    }
  }

  if (!in.max_packet_size.Write(&writer, version) ||
      !in.initial_max_data.Write(&writer, version) ||
      !in.initial_max_stream_data_bidi_local.Write(&writer, version) ||
      !in.initial_max_stream_data_bidi_remote.Write(&writer, version) ||
      !in.initial_max_stream_data_uni.Write(&writer, version) ||
      !in.initial_max_streams_bidi.Write(&writer, version) ||
      !in.initial_max_streams_uni.Write(&writer, version) ||
      !in.ack_delay_exponent.Write(&writer, version) ||
      !in.max_ack_delay.Write(&writer, version) ||
      !in.active_connection_id_limit.Write(&writer, version) ||
      !in.max_datagram_frame_size.Write(&writer, version)) {
    QUIC_BUG << "Failed to write integers for " << in;
    return false;
  }

  // disable_migration
  if (in.disable_migration) {
    if (!WriteTransportParameterId(
            &writer, TransportParameters::kDisableMigration, version) ||
        !WriteTransportParameterLength(&writer, /*length=*/0, version)) {
      QUIC_BUG << "Failed to write disable_migration for " << in;
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
    if (!WriteTransportParameterId(
            &writer, TransportParameters::kPreferredAddress, version) ||
        !WriteTransportParameterLength(&writer, preferred_address_length,
                                       version) ||
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

  // Google-specific non-standard parameter.
  if (in.google_quic_params) {
    const QuicData& serialized_google_quic_params =
        in.google_quic_params->GetSerialized();
    if (!WriteTransportParameterId(
            &writer, TransportParameters::kGoogleQuicParam, version) ||
        !WriteTransportParameterStringPiece(
            &writer, serialized_google_quic_params.AsStringPiece(), version)) {
      QUIC_BUG << "Failed to write Google params of length "
               << serialized_google_quic_params.length() << " for " << in;
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
  if (!WriteTransportParameterId(
          &writer, TransportParameters::kGoogleQuicVersion, version) ||
      !WriteTransportParameterLength(&writer, google_version_length, version) ||
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
    QUIC_BUG_IF(static_cast<uint64_t>(kv.first) < 0xff00)
        << "custom_parameters should not be used "
           "for non-private use parameters";
    if (!WriteTransportParameterId(&writer, kv.first, version) ||
        !WriteTransportParameterStringPiece(&writer, kv.second, version)) {
      QUIC_BUG << "Failed to write custom parameter " << kv.first;
      return false;
    }
  }

  if (!version.HasVarIntTransportParams()) {
    // Fill in the length prefix at the start of the transport parameters.
    if (writer.length() < sizeof(uint16_t) ||
        writer.length() - sizeof(uint16_t) >
            std::numeric_limits<uint16_t>::max()) {
      QUIC_BUG << "Cannot write length " << writer.length() << " for " << in;
      return false;
    }
    const uint16_t length_prefix = writer.length() - sizeof(uint16_t);
    QuicDataWriter length_prefix_writer(out->size(),
                                        reinterpret_cast<char*>(out->data()));
    if (!length_prefix_writer.WriteUInt16(length_prefix)) {
      QUIC_BUG << "Failed to write length prefix for " << in;
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

  if (!version.HasVarIntTransportParams()) {
    uint16_t full_length;
    if (!reader.ReadUInt16(&full_length)) {
      *error_details = "Failed to parse the transport parameter full length";
      return false;
    }
    if (full_length != reader.BytesRemaining()) {
      *error_details =
          quiche::QuicheStrCat("Invalid transport parameter full length ",
                               full_length, " != ", reader.BytesRemaining());
      return false;
    }
  }

  while (!reader.IsDoneReading()) {
    TransportParameters::TransportParameterId param_id;
    if (!ReadTransportParameterId(&reader, version, &param_id)) {
      *error_details = "Failed to parse transport parameter ID";
      return false;
    }
    quiche::QuicheStringPiece value;
    if (!ReadTransportParameterLengthAndValue(&reader, version, &value)) {
      *error_details =
          "Failed to read length and value of transport parameter " +
          TransportParameterIdToString(param_id);
      return false;
    }
    QuicDataReader value_reader(value);
    bool parse_success = true;
    switch (param_id) {
      case TransportParameters::kOriginalConnectionId: {
        if (!out->original_connection_id.IsEmpty()) {
          *error_details = "Received a second original connection ID";
          return false;
        }
        const size_t connection_id_length = value_reader.BytesRemaining();
        if (!QuicUtils::IsConnectionIdLengthValidForVersion(
                connection_id_length, version.transport_version)) {
          *error_details = quiche::QuicheStrCat(
              "Received original connection ID of invalid length ",
              connection_id_length);
          return false;
        }
        if (!value_reader.ReadConnectionId(&out->original_connection_id,
                                           connection_id_length)) {
          *error_details = "Failed to read original connection ID";
          return false;
        }
      } break;
      case TransportParameters::kIdleTimeout:
        parse_success =
            out->idle_timeout_milliseconds.Read(&value_reader, error_details);
        break;
      case TransportParameters::kStatelessResetToken: {
        if (!out->stateless_reset_token.empty()) {
          *error_details = "Received a second stateless reset token";
          return false;
        }
        quiche::QuicheStringPiece stateless_reset_token =
            value_reader.ReadRemainingPayload();
        if (stateless_reset_token.length() != kStatelessResetTokenLength) {
          *error_details = quiche::QuicheStrCat(
              "Received stateless reset token of invalid length ",
              stateless_reset_token.length());
          return false;
        }
        out->stateless_reset_token.assign(
            stateless_reset_token.data(),
            stateless_reset_token.data() + stateless_reset_token.length());
      } break;
      case TransportParameters::kMaxPacketSize:
        parse_success = out->max_packet_size.Read(&value_reader, error_details);
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
      case TransportParameters::kDisableMigration:
        if (out->disable_migration) {
          *error_details = "Received a second disable migration";
          return false;
        }
        out->disable_migration = true;
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
          *error_details = "Failed to read preferred address";
          return false;
        }
        preferred_address.ipv4_socket_address =
            QuicSocketAddress(QuicIpAddress(ipv4_address), ipv4_port);
        preferred_address.ipv6_socket_address =
            QuicSocketAddress(QuicIpAddress(ipv6_address), ipv6_port);
        if (!preferred_address.ipv4_socket_address.host().IsIPv4() ||
            !preferred_address.ipv6_socket_address.host().IsIPv6()) {
          *error_details = "Received preferred addresses of bad families " +
                           preferred_address.ToString();
          return false;
        }
        if (!QuicUtils::IsConnectionIdValidForVersion(
                preferred_address.connection_id, version.transport_version)) {
          *error_details = "Received invalid preferred address connection ID " +
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
      case TransportParameters::kMaxDatagramFrameSize:
        parse_success =
            out->max_datagram_frame_size.Read(&value_reader, error_details);
        break;
      case TransportParameters::kGoogleQuicParam: {
        if (out->google_quic_params) {
          *error_details = "Received a second Google parameter";
          return false;
        }
        out->google_quic_params =
            CryptoFramer::ParseMessage(value_reader.ReadRemainingPayload());
      } break;
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
      DCHECK(!error_details->empty());
      return false;
    }
    if (!value_reader.IsDoneReading()) {
      *error_details = quiche::QuicheStrCat(
          "Received unexpected ", value_reader.BytesRemaining(),
          " bytes after parsing ", TransportParameterIdToString(param_id));
      return false;
    }
  }

  if (!out->AreValid(error_details)) {
    DCHECK(!error_details->empty());
    return false;
  }

  QUIC_DLOG(INFO) << "Parsed transport parameters " << *out << " from "
                  << in_len << " bytes";

  return true;
}

}  // namespace quic
