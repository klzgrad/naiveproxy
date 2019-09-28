// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_versions.h"

#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_tag.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_endian.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {
namespace {

// Constructs a version label from the 4 bytes such that the on-the-wire
// order will be: d, c, b, a.
QuicVersionLabel MakeVersionLabel(char a, char b, char c, char d) {
  return MakeQuicTag(d, c, b, a);
}

QuicVersionLabel CreateRandomVersionLabelForNegotiation() {
  if (!GetQuicReloadableFlag(quic_version_negotiation_grease)) {
    return MakeVersionLabel(0xda, 0x5a, 0x3a, 0x3a);
  }
  QUIC_RELOADABLE_FLAG_COUNT_N(quic_version_negotiation_grease, 2, 2);
  QuicVersionLabel result;
  if (!GetQuicFlag(FLAGS_quic_disable_version_negotiation_grease_randomness)) {
    QuicRandom::GetInstance()->RandBytes(&result, sizeof(result));
  } else {
    result = MakeVersionLabel(0xd1, 0x57, 0x38, 0x3f);
  }
  result &= 0xf0f0f0f0;
  result |= 0x0a0a0a0a;
  return result;
}

}  // namespace

ParsedQuicVersion::ParsedQuicVersion(HandshakeProtocol handshake_protocol,
                                     QuicTransportVersion transport_version)
    : handshake_protocol(handshake_protocol),
      transport_version(transport_version) {}

bool ParsedQuicVersion::KnowsWhichDecrypterToUse() const {
  return transport_version >= QUIC_VERSION_47 ||
         handshake_protocol == PROTOCOL_TLS1_3;
}

bool ParsedQuicVersion::AllowsLowFlowControlLimits() const {
  return transport_version == QUIC_VERSION_99 &&
         handshake_protocol == PROTOCOL_TLS1_3;
}

bool ParsedQuicVersion::HasHeaderProtection() const {
  return transport_version == QUIC_VERSION_99;
}

bool ParsedQuicVersion::SupportsRetry() const {
  return transport_version > QUIC_VERSION_46;
}

bool ParsedQuicVersion::SendsVariableLengthPacketNumberInLongHeader() const {
  return transport_version > QUIC_VERSION_46;
}

bool ParsedQuicVersion::SupportsClientConnectionIds() const {
  if (!GetQuicRestartFlag(quic_do_not_override_connection_id)) {
    // Do not enable this feature in a production version until this flag has
    // been deprecated.
    return false;
  }
  return transport_version >= QUIC_VERSION_99;
}

bool ParsedQuicVersion::DoesNotHaveHeadersStream() const {
  return VersionLacksHeadersStream(transport_version);
}

bool VersionLacksHeadersStream(QuicTransportVersion transport_version) {
  if (GetQuicFlag(FLAGS_quic_headers_stream_id_in_v99) == 0) {
    return false;
  }
  return transport_version == QUIC_VERSION_99;
}

std::ostream& operator<<(std::ostream& os, const ParsedQuicVersion& version) {
  os << ParsedQuicVersionToString(version);
  return os;
}

QuicVersionLabel CreateQuicVersionLabel(ParsedQuicVersion parsed_version) {
  char proto = 0;
  switch (parsed_version.handshake_protocol) {
    case PROTOCOL_QUIC_CRYPTO:
      proto = 'Q';
      break;
    case PROTOCOL_TLS1_3:
      proto = 'T';
      break;
    default:
      QUIC_BUG << "Invalid HandshakeProtocol: "
               << parsed_version.handshake_protocol;
      return 0;
  }
  switch (parsed_version.transport_version) {
    case QUIC_VERSION_39:
      return MakeVersionLabel(proto, '0', '3', '9');
    case QUIC_VERSION_43:
      return MakeVersionLabel(proto, '0', '4', '3');
    case QUIC_VERSION_44:
      return MakeVersionLabel(proto, '0', '4', '4');
    case QUIC_VERSION_46:
      return MakeVersionLabel(proto, '0', '4', '6');
    case QUIC_VERSION_47:
      return MakeVersionLabel(proto, '0', '4', '7');
    case QUIC_VERSION_48:
      return MakeVersionLabel(proto, '0', '4', '8');
    case QUIC_VERSION_99:
      if (parsed_version.handshake_protocol == PROTOCOL_TLS1_3 &&
          GetQuicFlag(FLAGS_quic_ietf_draft_version) != 0) {
        return MakeVersionLabel(0xff, 0x00, 0x00,
                                GetQuicFlag(FLAGS_quic_ietf_draft_version));
      }
      return MakeVersionLabel(proto, '0', '9', '9');
    case QUIC_VERSION_RESERVED_FOR_NEGOTIATION:
      return CreateRandomVersionLabelForNegotiation();
    default:
      // This is a bug because we should never attempt to convert an invalid
      // QuicTransportVersion to be written to the wire.
      QUIC_BUG << "Unsupported QuicTransportVersion: "
               << parsed_version.transport_version;
      return 0;
  }
}

QuicVersionLabelVector CreateQuicVersionLabelVector(
    const ParsedQuicVersionVector& versions) {
  QuicVersionLabelVector out;
  out.reserve(versions.size());
  for (const auto& version : versions) {
    out.push_back(CreateQuicVersionLabel(version));
  }
  return out;
}

ParsedQuicVersion ParseQuicVersionLabel(QuicVersionLabel version_label) {
  std::vector<HandshakeProtocol> protocols = {PROTOCOL_QUIC_CRYPTO,
                                              PROTOCOL_TLS1_3};
  for (QuicTransportVersion version : kSupportedTransportVersions) {
    for (HandshakeProtocol handshake : protocols) {
      if (version_label ==
          CreateQuicVersionLabel(ParsedQuicVersion(handshake, version))) {
        return ParsedQuicVersion(handshake, version);
      }
    }
  }
  // Reading from the client so this should not be considered an ERROR.
  QUIC_DLOG(INFO) << "Unsupported QuicVersionLabel version: "
                  << QuicVersionLabelToString(version_label);
  return UnsupportedQuicVersion();
}

ParsedQuicVersion ParseQuicVersionString(std::string version_string) {
  if (version_string.empty()) {
    return UnsupportedQuicVersion();
  }
  int quic_version_number = 0;
  if (QuicTextUtils::StringToInt(version_string, &quic_version_number) &&
      quic_version_number > 0) {
    return ParsedQuicVersion(
        PROTOCOL_QUIC_CRYPTO,
        static_cast<QuicTransportVersion>(quic_version_number));
  }

  std::vector<HandshakeProtocol> protocols = {PROTOCOL_QUIC_CRYPTO};
  if (GetQuicFlag(FLAGS_quic_supports_tls_handshake)) {
    protocols.push_back(PROTOCOL_TLS1_3);
  }
  for (QuicTransportVersion version : kSupportedTransportVersions) {
    for (HandshakeProtocol handshake : protocols) {
      const ParsedQuicVersion parsed_version =
          ParsedQuicVersion(handshake, version);
      if (version_string == ParsedQuicVersionToString(parsed_version)) {
        return parsed_version;
      }
    }
  }
  // Still recognize T099 even if flag quic_ietf_draft_version has been changed.
  if (GetQuicFlag(FLAGS_quic_supports_tls_handshake) &&
      version_string == "T099") {
    return ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_99);
  }
  // Reading from the client so this should not be considered an ERROR.
  QUIC_DLOG(INFO) << "Unsupported QUIC version string: \"" << version_string
                  << "\".";
  return UnsupportedQuicVersion();
}

QuicTransportVersionVector AllSupportedTransportVersions() {
  QuicTransportVersionVector supported_versions;
  for (QuicTransportVersion version : kSupportedTransportVersions) {
    supported_versions.push_back(version);
  }
  return supported_versions;
}

ParsedQuicVersionVector AllSupportedVersions() {
  ParsedQuicVersionVector supported_versions;
  for (HandshakeProtocol protocol : kSupportedHandshakeProtocols) {
    for (QuicTransportVersion version : kSupportedTransportVersions) {
      if (protocol == PROTOCOL_TLS1_3 &&
          !QuicVersionUsesCryptoFrames(version)) {
        // The TLS handshake is only deployable if CRYPTO frames are also used.
        continue;
      }
      supported_versions.push_back(ParsedQuicVersion(protocol, version));
    }
  }
  return supported_versions;
}

// TODO(nharper): Remove this function when it is no longer in use.
QuicTransportVersionVector CurrentSupportedTransportVersions() {
  return FilterSupportedTransportVersions(AllSupportedTransportVersions());
}

ParsedQuicVersionVector CurrentSupportedVersions() {
  return FilterSupportedVersions(AllSupportedVersions());
}

// TODO(nharper): Remove this function when it is no longer in use.
QuicTransportVersionVector FilterSupportedTransportVersions(
    QuicTransportVersionVector versions) {
  ParsedQuicVersionVector parsed_versions;
  for (QuicTransportVersion version : versions) {
    parsed_versions.push_back(ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, version));
  }
  ParsedQuicVersionVector filtered_parsed_versions =
      FilterSupportedVersions(parsed_versions);
  QuicTransportVersionVector filtered_versions;
  for (ParsedQuicVersion version : filtered_parsed_versions) {
    filtered_versions.push_back(version.transport_version);
  }
  return filtered_versions;
}

ParsedQuicVersionVector FilterSupportedVersions(
    ParsedQuicVersionVector versions) {
  ParsedQuicVersionVector filtered_versions;
  filtered_versions.reserve(versions.size());
  for (ParsedQuicVersion version : versions) {
    if (version.handshake_protocol == PROTOCOL_TLS1_3 &&
        !GetQuicFlag(FLAGS_quic_supports_tls_handshake)) {
      continue;
    }
    if (version.transport_version == QUIC_VERSION_99) {
      if (GetQuicReloadableFlag(quic_enable_version_99)) {
        filtered_versions.push_back(version);
      }
    } else if (version.transport_version == QUIC_VERSION_48) {
      if (GetQuicReloadableFlag(quic_enable_version_48)) {
        filtered_versions.push_back(version);
      }
    } else if (version.transport_version == QUIC_VERSION_47) {
      if (GetQuicReloadableFlag(quic_enable_version_47)) {
        filtered_versions.push_back(version);
      }
    } else if (version.transport_version == QUIC_VERSION_44) {
      if (!GetQuicReloadableFlag(quic_disable_version_44)) {
        filtered_versions.push_back(version);
      }
    } else if (version.transport_version == QUIC_VERSION_39) {
      if (!GetQuicReloadableFlag(quic_disable_version_39)) {
        filtered_versions.push_back(version);
      }
    } else {
      filtered_versions.push_back(version);
    }
  }
  return filtered_versions;
}

QuicTransportVersionVector VersionOfIndex(
    const QuicTransportVersionVector& versions,
    int index) {
  QuicTransportVersionVector version;
  int version_count = versions.size();
  if (index >= 0 && index < version_count) {
    version.push_back(versions[index]);
  } else {
    version.push_back(QUIC_VERSION_UNSUPPORTED);
  }
  return version;
}

ParsedQuicVersionVector ParsedVersionOfIndex(
    const ParsedQuicVersionVector& versions,
    int index) {
  ParsedQuicVersionVector version;
  int version_count = versions.size();
  if (index >= 0 && index < version_count) {
    version.push_back(versions[index]);
  } else {
    version.push_back(UnsupportedQuicVersion());
  }
  return version;
}

QuicTransportVersionVector ParsedVersionsToTransportVersions(
    const ParsedQuicVersionVector& versions) {
  QuicTransportVersionVector transport_versions;
  transport_versions.resize(versions.size());
  for (size_t i = 0; i < versions.size(); ++i) {
    transport_versions[i] = versions[i].transport_version;
  }
  return transport_versions;
}

QuicVersionLabel QuicVersionToQuicVersionLabel(
    QuicTransportVersion transport_version) {
  return CreateQuicVersionLabel(
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, transport_version));
}

std::string QuicVersionLabelToString(QuicVersionLabel version_label) {
  return QuicTagToString(QuicEndian::HostToNet32(version_label));
}

std::string QuicVersionLabelVectorToString(
    const QuicVersionLabelVector& version_labels,
    const std::string& separator,
    size_t skip_after_nth_version) {
  std::string result;
  for (size_t i = 0; i < version_labels.size(); ++i) {
    if (i != 0) {
      result.append(separator);
    }

    if (i > skip_after_nth_version) {
      result.append("...");
      break;
    }
    result.append(QuicVersionLabelToString(version_labels[i]));
  }
  return result;
}

QuicTransportVersion QuicVersionLabelToQuicVersion(
    QuicVersionLabel version_label) {
  return ParseQuicVersionLabel(version_label).transport_version;
}

HandshakeProtocol QuicVersionLabelToHandshakeProtocol(
    QuicVersionLabel version_label) {
  return ParseQuicVersionLabel(version_label).handshake_protocol;
}

#define RETURN_STRING_LITERAL(x) \
  case x:                        \
    return #x

std::string QuicVersionToString(QuicTransportVersion transport_version) {
  switch (transport_version) {
    RETURN_STRING_LITERAL(QUIC_VERSION_39);
    RETURN_STRING_LITERAL(QUIC_VERSION_43);
    RETURN_STRING_LITERAL(QUIC_VERSION_44);
    RETURN_STRING_LITERAL(QUIC_VERSION_46);
    RETURN_STRING_LITERAL(QUIC_VERSION_47);
    RETURN_STRING_LITERAL(QUIC_VERSION_48);
    RETURN_STRING_LITERAL(QUIC_VERSION_99);
    default:
      return "QUIC_VERSION_UNSUPPORTED";
  }
}

std::string ParsedQuicVersionToString(ParsedQuicVersion version) {
  if (version == UnsupportedQuicVersion()) {
    return "0";
  }
  return QuicVersionLabelToString(CreateQuicVersionLabel(version));
}

std::string QuicTransportVersionVectorToString(
    const QuicTransportVersionVector& versions) {
  std::string result = "";
  for (size_t i = 0; i < versions.size(); ++i) {
    if (i != 0) {
      result.append(",");
    }
    result.append(QuicVersionToString(versions[i]));
  }
  return result;
}

std::string ParsedQuicVersionVectorToString(
    const ParsedQuicVersionVector& versions,
    const std::string& separator,
    size_t skip_after_nth_version) {
  std::string result;
  for (size_t i = 0; i < versions.size(); ++i) {
    if (i != 0) {
      result.append(separator);
    }
    if (i > skip_after_nth_version) {
      result.append("...");
      break;
    }
    result.append(ParsedQuicVersionToString(versions[i]));
  }
  return result;
}

ParsedQuicVersion UnsupportedQuicVersion() {
  return ParsedQuicVersion(PROTOCOL_UNSUPPORTED, QUIC_VERSION_UNSUPPORTED);
}

ParsedQuicVersion QuicVersionReservedForNegotiation() {
  return ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO,
                           QUIC_VERSION_RESERVED_FOR_NEGOTIATION);
}

std::string AlpnForVersion(ParsedQuicVersion parsed_version) {
  if (parsed_version.handshake_protocol == PROTOCOL_TLS1_3 &&
      parsed_version.transport_version == QUIC_VERSION_99 &&
      GetQuicFlag(FLAGS_quic_ietf_draft_version) != 0) {
    return "h3-" + QuicTextUtils::Uint64ToString(
                       GetQuicFlag(FLAGS_quic_ietf_draft_version));
  }
  return "h3-google-" + ParsedQuicVersionToString(parsed_version);
}

void QuicVersionInitializeSupportForIetfDraft(int32_t draft_version) {
  if (draft_version < 0 || draft_version >= 256) {
    QUIC_LOG(FATAL) << "Invalid IETF draft version " << draft_version;
    return;
  }

  SetQuicFlag(FLAGS_quic_ietf_draft_version, draft_version);

  if (draft_version == 0) {
    return;
  }

  // Enable necessary flags.
  SetQuicFlag(FLAGS_quic_supports_tls_handshake, true);
  SetQuicFlag(FLAGS_quic_headers_stream_id_in_v99, 60);
  SetQuicReloadableFlag(quic_simplify_stop_waiting, true);
  SetQuicRestartFlag(quic_do_not_override_connection_id, true);
  SetQuicRestartFlag(quic_use_allocated_connection_ids, true);
  SetQuicRestartFlag(quic_dispatcher_hands_chlo_extractor_one_version, true);
}

void QuicEnableVersion(ParsedQuicVersion parsed_version) {
  if (parsed_version.handshake_protocol == PROTOCOL_TLS1_3) {
    SetQuicFlag(FLAGS_quic_supports_tls_handshake, true);
  }
  static_assert(QUIC_ARRAYSIZE(kSupportedTransportVersions) == 7u,
                "Supported versions out of sync");
  if (parsed_version.transport_version == QUIC_VERSION_99) {
    SetQuicReloadableFlag(quic_enable_version_99, true);
  }
  if (parsed_version.transport_version == QUIC_VERSION_48) {
    SetQuicReloadableFlag(quic_enable_version_48, true);
  }
  if (parsed_version.transport_version == QUIC_VERSION_47) {
    SetQuicReloadableFlag(quic_enable_version_47, true);
  }
}

#undef RETURN_STRING_LITERAL  // undef for jumbo builds
}  // namespace quic
