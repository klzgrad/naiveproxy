// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_versions.h"

#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_tag.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {
namespace {

// Constructs a version label from the 4 bytes such that the on-the-wire
// order will be: d, c, b, a.
QuicVersionLabel MakeVersionLabel(char a, char b, char c, char d) {
  return MakeQuicTag(d, c, b, a);
}

QuicVersionLabel CreateRandomVersionLabelForNegotiation() {
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

bool ParsedQuicVersion::IsKnown() const {
  DCHECK(ParsedQuicVersionIsValid(handshake_protocol, transport_version))
      << QuicVersionToString(transport_version) << " "
      << HandshakeProtocolToString(handshake_protocol);
  return transport_version != QUIC_VERSION_UNSUPPORTED;
}

bool ParsedQuicVersion::KnowsWhichDecrypterToUse() const {
  DCHECK(IsKnown());
  return transport_version > QUIC_VERSION_46 ||
         handshake_protocol == PROTOCOL_TLS1_3;
}

bool ParsedQuicVersion::UsesInitialObfuscators() const {
  DCHECK(IsKnown());
  return transport_version > QUIC_VERSION_49 ||
         handshake_protocol == PROTOCOL_TLS1_3;
}

bool ParsedQuicVersion::AllowsLowFlowControlLimits() const {
  DCHECK(IsKnown());
  return transport_version >= QUIC_VERSION_IETF_DRAFT_25 &&
         handshake_protocol == PROTOCOL_TLS1_3;
}

bool ParsedQuicVersion::HasHeaderProtection() const {
  DCHECK(IsKnown());
  return transport_version > QUIC_VERSION_49;
}

bool ParsedQuicVersion::SupportsRetry() const {
  DCHECK(IsKnown());
  return transport_version > QUIC_VERSION_46;
}

bool ParsedQuicVersion::HasRetryIntegrityTag() const {
  DCHECK(IsKnown());
  return handshake_protocol == PROTOCOL_TLS1_3;
}

bool ParsedQuicVersion::SendsVariableLengthPacketNumberInLongHeader() const {
  DCHECK(IsKnown());
  return transport_version > QUIC_VERSION_46;
}

bool ParsedQuicVersion::AllowsVariableLengthConnectionIds() const {
  DCHECK(IsKnown());
  return VersionAllowsVariableLengthConnectionIds(transport_version);
}

bool ParsedQuicVersion::SupportsClientConnectionIds() const {
  DCHECK(IsKnown());
  return transport_version > QUIC_VERSION_48;
}

bool ParsedQuicVersion::HasLengthPrefixedConnectionIds() const {
  DCHECK(IsKnown());
  return VersionHasLengthPrefixedConnectionIds(transport_version);
}

bool ParsedQuicVersion::SupportsAntiAmplificationLimit() const {
  DCHECK(IsKnown());
  return transport_version >= QUIC_VERSION_IETF_DRAFT_25 &&
         handshake_protocol == PROTOCOL_TLS1_3;
}

bool ParsedQuicVersion::CanSendCoalescedPackets() const {
  DCHECK(IsKnown());
  return QuicVersionHasLongHeaderLengths(transport_version) &&
         handshake_protocol == PROTOCOL_TLS1_3;
}

bool ParsedQuicVersion::SupportsGoogleAltSvcFormat() const {
  DCHECK(IsKnown());
  return VersionSupportsGoogleAltSvcFormat(transport_version);
}

bool ParsedQuicVersion::HasIetfInvariantHeader() const {
  DCHECK(IsKnown());
  return VersionHasIetfInvariantHeader(transport_version);
}

bool ParsedQuicVersion::SupportsMessageFrames() const {
  DCHECK(IsKnown());
  return VersionSupportsMessageFrames(transport_version);
}

bool ParsedQuicVersion::UsesHttp3() const {
  DCHECK(IsKnown());
  return VersionUsesHttp3(transport_version);
}

bool ParsedQuicVersion::HasLongHeaderLengths() const {
  DCHECK(IsKnown());
  return QuicVersionHasLongHeaderLengths(transport_version);
}

bool ParsedQuicVersion::UsesCryptoFrames() const {
  DCHECK(IsKnown());
  return QuicVersionUsesCryptoFrames(transport_version);
}

bool ParsedQuicVersion::HasIetfQuicFrames() const {
  DCHECK(IsKnown());
  return VersionHasIetfQuicFrames(transport_version);
}

bool ParsedQuicVersion::HasHandshakeDone() const {
  DCHECK(IsKnown());
  return HasIetfQuicFrames() && handshake_protocol == PROTOCOL_TLS1_3;
}

bool ParsedQuicVersion::HasVarIntTransportParams() const {
  DCHECK(IsKnown());
  return transport_version >= QUIC_VERSION_IETF_DRAFT_27;
}

bool VersionHasLengthPrefixedConnectionIds(
    QuicTransportVersion transport_version) {
  DCHECK(transport_version != QUIC_VERSION_UNSUPPORTED);
  return transport_version > QUIC_VERSION_48;
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
  static_assert(SupportedVersions().size() == 8u,
                "Supported versions out of sync");
  switch (parsed_version.transport_version) {
    case QUIC_VERSION_43:
      return MakeVersionLabel(proto, '0', '4', '3');
    case QUIC_VERSION_46:
      return MakeVersionLabel(proto, '0', '4', '6');
    case QUIC_VERSION_48:
      return MakeVersionLabel(proto, '0', '4', '8');
    case QUIC_VERSION_49:
      return MakeVersionLabel(proto, '0', '4', '9');
    case QUIC_VERSION_50:
      return MakeVersionLabel(proto, '0', '5', '0');
    case QUIC_VERSION_IETF_DRAFT_25:
      if (parsed_version.handshake_protocol == PROTOCOL_TLS1_3) {
        return MakeVersionLabel(0xff, 0x00, 0x00, 25);
      }
      QUIC_BUG << "QUIC_VERSION_IETF_DRAFT_25 requires TLS";
      return 0;
    case QUIC_VERSION_IETF_DRAFT_27:
      if (parsed_version.handshake_protocol == PROTOCOL_TLS1_3) {
        return MakeVersionLabel(0xff, 0x00, 0x00, 27);
      }
      QUIC_BUG << "QUIC_VERSION_IETF_DRAFT_27 requires TLS";
      return 0;
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

ParsedQuicVersionVector AllSupportedVersionsWithQuicCrypto() {
  ParsedQuicVersionVector versions;
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    if (version.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
      versions.push_back(version);
    }
  }
  QUIC_BUG_IF(versions.empty()) << "No version with QUIC crypto found.";
  return versions;
}

ParsedQuicVersionVector CurrentSupportedVersionsWithQuicCrypto() {
  ParsedQuicVersionVector versions;
  for (const ParsedQuicVersion& version : CurrentSupportedVersions()) {
    if (version.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
      versions.push_back(version);
    }
  }
  QUIC_BUG_IF(versions.empty()) << "No version with QUIC crypto found.";
  return versions;
}

ParsedQuicVersionVector CurrentSupportedVersionsWithTls() {
  ParsedQuicVersionVector versions;
  for (const ParsedQuicVersion& version : CurrentSupportedVersions()) {
    if (version.handshake_protocol == PROTOCOL_TLS1_3) {
      versions.push_back(version);
    }
  }
  QUIC_BUG_IF(versions.empty()) << "No version with TLS handshake found.";
  return versions;
}

ParsedQuicVersion ParseQuicVersionLabel(QuicVersionLabel version_label) {
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    if (version_label == CreateQuicVersionLabel(version)) {
      return version;
    }
  }
  // Reading from the client so this should not be considered an ERROR.
  QUIC_DLOG(INFO) << "Unsupported QuicVersionLabel version: "
                  << QuicVersionLabelToString(version_label);
  return UnsupportedQuicVersion();
}

ParsedQuicVersion ParseQuicVersionString(
    quiche::QuicheStringPiece version_string) {
  if (version_string.empty()) {
    return UnsupportedQuicVersion();
  }
  int quic_version_number = 0;
  if (quiche::QuicheTextUtils::StringToInt(version_string,
                                           &quic_version_number) &&
      quic_version_number > 0) {
    QuicTransportVersion transport_version =
        static_cast<QuicTransportVersion>(quic_version_number);
    bool transport_version_is_supported = false;
    for (QuicTransportVersion transport_vers : SupportedTransportVersions()) {
      if (transport_vers == transport_version) {
        transport_version_is_supported = true;
        break;
      }
    }
    if (!transport_version_is_supported ||
        !ParsedQuicVersionIsValid(PROTOCOL_QUIC_CRYPTO, transport_version)) {
      return UnsupportedQuicVersion();
    }
    return ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, transport_version);
  }
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    if (version_string == ParsedQuicVersionToString(version) ||
        version_string == AlpnForVersion(version) ||
        (version.handshake_protocol == PROTOCOL_QUIC_CRYPTO &&
         version_string == QuicVersionToString(version.transport_version))) {
      return version;
    }
  }
  // Reading from the client so this should not be considered an ERROR.
  QUIC_DLOG(INFO) << "Unsupported QUIC version string: \"" << version_string
                  << "\".";
  return UnsupportedQuicVersion();
}

ParsedQuicVersionVector ParseQuicVersionVectorString(
    quiche::QuicheStringPiece versions_string) {
  ParsedQuicVersionVector versions;
  std::vector<quiche::QuicheStringPiece> version_strings =
      quiche::QuicheTextUtils::Split(versions_string, ',');
  for (quiche::QuicheStringPiece version_string : version_strings) {
    quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(
        &version_string);
    ParsedQuicVersion version = ParseQuicVersionString(version_string);
    if (version.transport_version == QUIC_VERSION_UNSUPPORTED ||
        std::find(versions.begin(), versions.end(), version) !=
            versions.end()) {
      continue;
    }
    versions.push_back(version);
  }
  return versions;
}

QuicTransportVersionVector AllSupportedTransportVersions() {
  constexpr auto supported_transport_versions = SupportedTransportVersions();
  QuicTransportVersionVector supported_versions(
      supported_transport_versions.begin(), supported_transport_versions.end());
  return supported_versions;
}

ParsedQuicVersionVector AllSupportedVersions() {
  constexpr auto supported_versions = SupportedVersions();
  return ParsedQuicVersionVector(supported_versions.begin(),
                                 supported_versions.end());
}

ParsedQuicVersionVector CurrentSupportedVersions() {
  return FilterSupportedVersions(AllSupportedVersions());
}

ParsedQuicVersionVector FilterSupportedVersions(
    ParsedQuicVersionVector versions) {
  ParsedQuicVersionVector filtered_versions;
  filtered_versions.reserve(versions.size());
  for (ParsedQuicVersion version : versions) {
    if (version.transport_version == QUIC_VERSION_IETF_DRAFT_27) {
      QUIC_BUG_IF(version.handshake_protocol != PROTOCOL_TLS1_3);
      if (GetQuicReloadableFlag(quic_enable_version_draft_27)) {
        filtered_versions.push_back(version);
      }
    } else if (version.transport_version == QUIC_VERSION_IETF_DRAFT_25) {
      QUIC_BUG_IF(version.handshake_protocol != PROTOCOL_TLS1_3);
      if (GetQuicReloadableFlag(quic_enable_version_draft_25_v3)) {
        filtered_versions.push_back(version);
      }
    } else if (version.transport_version == QUIC_VERSION_50) {
      if (version.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
        if (!GetQuicReloadableFlag(quic_disable_version_q050)) {
          filtered_versions.push_back(version);
        }
      } else {
        if (GetQuicReloadableFlag(quic_enable_version_t050)) {
          filtered_versions.push_back(version);
        }
      }
    } else if (version.transport_version == QUIC_VERSION_49) {
      if (!GetQuicReloadableFlag(quic_disable_version_q049)) {
        filtered_versions.push_back(version);
      }
    } else if (version.transport_version == QUIC_VERSION_48) {
      if (!GetQuicReloadableFlag(quic_disable_version_q048)) {
        filtered_versions.push_back(version);
      }
    } else if (version.transport_version == QUIC_VERSION_46) {
      QUIC_BUG_IF(version.handshake_protocol != PROTOCOL_QUIC_CRYPTO);
      if (!GetQuicReloadableFlag(quic_disable_version_q046)) {
        filtered_versions.push_back(version);
      }
    } else if (version.transport_version == QUIC_VERSION_43) {
      QUIC_BUG_IF(version.handshake_protocol != PROTOCOL_QUIC_CRYPTO);
      if (!GetQuicReloadableFlag(quic_disable_version_q043)) {
        filtered_versions.push_back(version);
      }
    } else {
      QUIC_BUG << "QUIC version " << version << " has no flag protection";
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
  return QuicTagToString(quiche::QuicheEndian::HostToNet32(version_label));
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
  static_assert(SupportedTransportVersions().size() == 7u,
                "Supported versions out of sync");
  switch (transport_version) {
    RETURN_STRING_LITERAL(QUIC_VERSION_43);
    RETURN_STRING_LITERAL(QUIC_VERSION_46);
    RETURN_STRING_LITERAL(QUIC_VERSION_48);
    RETURN_STRING_LITERAL(QUIC_VERSION_49);
    RETURN_STRING_LITERAL(QUIC_VERSION_50);
    RETURN_STRING_LITERAL(QUIC_VERSION_IETF_DRAFT_25);
    RETURN_STRING_LITERAL(QUIC_VERSION_IETF_DRAFT_27);
    RETURN_STRING_LITERAL(QUIC_VERSION_UNSUPPORTED);
    RETURN_STRING_LITERAL(QUIC_VERSION_RESERVED_FOR_NEGOTIATION);
  }
  return quiche::QuicheStrCat("QUIC_VERSION_UNKNOWN(",
                              static_cast<int>(transport_version), ")");
}

std::string HandshakeProtocolToString(HandshakeProtocol handshake_protocol) {
  switch (handshake_protocol) {
    RETURN_STRING_LITERAL(PROTOCOL_UNSUPPORTED);
    RETURN_STRING_LITERAL(PROTOCOL_QUIC_CRYPTO);
    RETURN_STRING_LITERAL(PROTOCOL_TLS1_3);
  }
  return quiche::QuicheStrCat("PROTOCOL_UNKNOWN(",
                              static_cast<int>(handshake_protocol), ")");
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

bool VersionSupportsGoogleAltSvcFormat(QuicTransportVersion transport_version) {
  return transport_version <= QUIC_VERSION_46;
}

bool VersionAllowsVariableLengthConnectionIds(
    QuicTransportVersion transport_version) {
  DCHECK_NE(transport_version, QUIC_VERSION_UNSUPPORTED);
  return transport_version > QUIC_VERSION_46;
}

bool QuicVersionLabelUses4BitConnectionIdLength(
    QuicVersionLabel version_label) {
  // As we deprecate old versions, we still need the ability to send valid
  // version negotiation packets for those versions. This function keeps track
  // of the versions that ever supported the 4bit connection ID length encoding
  // that we know about. Google QUIC 43 and earlier used a different encoding,
  // and Google QUIC 49 and later use the new length prefixed encoding.
  // Similarly, only IETF drafts 11 to 21 used this encoding.

  // Check Q044, Q045, Q046, Q047 and Q048.
  for (uint8_t c = '4'; c <= '8'; ++c) {
    if (version_label == MakeVersionLabel('Q', '0', '4', c)) {
      return true;
    }
  }
  // Check T048.
  if (version_label == MakeVersionLabel('T', '0', '4', '8')) {
    return true;
  }
  // Check IETF draft versions in [11,21].
  for (uint8_t draft_number = 11; draft_number <= 21; ++draft_number) {
    if (version_label == MakeVersionLabel(0xff, 0x00, 0x00, draft_number)) {
      return true;
    }
  }
  return false;
}

ParsedQuicVersion UnsupportedQuicVersion() {
  return ParsedQuicVersion(PROTOCOL_UNSUPPORTED, QUIC_VERSION_UNSUPPORTED);
}

ParsedQuicVersion QuicVersionReservedForNegotiation() {
  return ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO,
                           QUIC_VERSION_RESERVED_FOR_NEGOTIATION);
}

std::string AlpnForVersion(ParsedQuicVersion parsed_version) {
  if (parsed_version.handshake_protocol == PROTOCOL_TLS1_3) {
    if (parsed_version.transport_version == QUIC_VERSION_IETF_DRAFT_25) {
      return "h3-25";
    }
    if (parsed_version.transport_version == QUIC_VERSION_IETF_DRAFT_27) {
      return "h3-27";
    }
  }
  return "h3-" + ParsedQuicVersionToString(parsed_version);
}

void QuicVersionInitializeSupportForIetfDraft() {
  // Enable necessary flags.
}

void QuicEnableVersion(ParsedQuicVersion parsed_version) {
  static_assert(SupportedVersions().size() == 8u,
                "Supported versions out of sync");
  if (parsed_version.transport_version == QUIC_VERSION_IETF_DRAFT_27) {
    QUIC_BUG_IF(parsed_version.handshake_protocol != PROTOCOL_TLS1_3);
    SetQuicReloadableFlag(quic_enable_version_draft_27, true);
  } else if (parsed_version.transport_version == QUIC_VERSION_IETF_DRAFT_25) {
    QUIC_BUG_IF(parsed_version.handshake_protocol != PROTOCOL_TLS1_3);
    SetQuicReloadableFlag(quic_enable_version_draft_25_v3, true);
  } else if (parsed_version.transport_version == QUIC_VERSION_50) {
    if (parsed_version.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
      SetQuicReloadableFlag(quic_disable_version_q050, false);
    } else {
      SetQuicReloadableFlag(quic_enable_version_t050, true);
    }
  } else if (parsed_version.transport_version == QUIC_VERSION_49) {
    SetQuicReloadableFlag(quic_disable_version_q049, false);
  } else if (parsed_version.transport_version == QUIC_VERSION_48) {
    SetQuicReloadableFlag(quic_disable_version_q048, false);
  } else if (parsed_version.transport_version == QUIC_VERSION_46) {
    SetQuicReloadableFlag(quic_disable_version_q046, false);
  } else if (parsed_version.transport_version == QUIC_VERSION_43) {
    SetQuicReloadableFlag(quic_disable_version_q043, false);
  }
}

#undef RETURN_STRING_LITERAL  // undef for jumbo builds
}  // namespace quic
