// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_versions.h"

#include <string>

#include "absl/base/macros.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_tag.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/quiche_endian.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {
namespace {

QuicVersionLabel CreateRandomVersionLabelForNegotiation() {
  QuicVersionLabel result;
  if (!GetQuicFlag(quic_disable_version_negotiation_grease_randomness)) {
    QuicRandom::GetInstance()->RandBytes(&result, sizeof(result));
  } else {
    result = MakeVersionLabel(0xd1, 0x57, 0x38, 0x3f);
  }
  result &= 0xf0f0f0f0;
  result |= 0x0a0a0a0a;
  return result;
}

void SetVersionFlag(const ParsedQuicVersion& version, bool should_enable) {
  static_assert(SupportedVersions().size() == 5u,
                "Supported versions out of sync");
  const bool enable = should_enable;
  const bool disable = !should_enable;
  if (version == ParsedQuicVersion::RFCv2()) {
    SetQuicReloadableFlag(quic_enable_version_rfcv2, enable);
  } else if (version == ParsedQuicVersion::RFCv1()) {
    SetQuicReloadableFlag(quic_disable_version_rfcv1, disable);
  } else if (version == ParsedQuicVersion::Draft29()) {
    SetQuicReloadableFlag(quic_disable_version_draft_29, disable);
  } else if (version == ParsedQuicVersion::Q050()) {
    SetQuicReloadableFlag(quic_disable_version_q050, disable);
  } else if (version == ParsedQuicVersion::Q046()) {
    SetQuicReloadableFlag(quic_disable_version_q046, disable);
  } else {
    QUIC_BUG(quic_bug_10589_1)
        << "Cannot " << (enable ? "en" : "dis") << "able version " << version;
  }
}

}  // namespace

bool ParsedQuicVersion::IsKnown() const {
  QUICHE_DCHECK(ParsedQuicVersionIsValid(handshake_protocol, transport_version))
      << QuicVersionToString(transport_version) << " "
      << HandshakeProtocolToString(handshake_protocol);
  return transport_version != QUIC_VERSION_UNSUPPORTED;
}

bool ParsedQuicVersion::KnowsWhichDecrypterToUse() const {
  QUICHE_DCHECK(IsKnown());
  return transport_version > QUIC_VERSION_46;
}

bool ParsedQuicVersion::UsesInitialObfuscators() const {
  QUICHE_DCHECK(IsKnown());
  // Initial obfuscators were added in version 50.
  return transport_version > QUIC_VERSION_46;
}

bool ParsedQuicVersion::AllowsLowFlowControlLimits() const {
  QUICHE_DCHECK(IsKnown());
  // Low flow-control limits are used for all IETF versions.
  return UsesHttp3();
}

bool ParsedQuicVersion::HasHeaderProtection() const {
  QUICHE_DCHECK(IsKnown());
  // Header protection was added in version 50.
  return transport_version > QUIC_VERSION_46;
}

bool ParsedQuicVersion::SupportsRetry() const {
  QUICHE_DCHECK(IsKnown());
  // Retry was added in version 47.
  return transport_version > QUIC_VERSION_46;
}

bool ParsedQuicVersion::SendsVariableLengthPacketNumberInLongHeader() const {
  QUICHE_DCHECK(IsKnown());
  return transport_version > QUIC_VERSION_46;
}

bool ParsedQuicVersion::AllowsVariableLengthConnectionIds() const {
  QUICHE_DCHECK(IsKnown());
  return VersionAllowsVariableLengthConnectionIds(transport_version);
}

bool ParsedQuicVersion::SupportsClientConnectionIds() const {
  QUICHE_DCHECK(IsKnown());
  // Client connection IDs were added in version 49.
  return transport_version > QUIC_VERSION_46;
}

bool ParsedQuicVersion::HasLengthPrefixedConnectionIds() const {
  QUICHE_DCHECK(IsKnown());
  return VersionHasLengthPrefixedConnectionIds(transport_version);
}

bool ParsedQuicVersion::SupportsAntiAmplificationLimit() const {
  QUICHE_DCHECK(IsKnown());
  // The anti-amplification limit is used for all IETF versions.
  return UsesHttp3();
}

bool ParsedQuicVersion::CanSendCoalescedPackets() const {
  QUICHE_DCHECK(IsKnown());
  return HasLongHeaderLengths() && UsesTls();
}

bool ParsedQuicVersion::SupportsGoogleAltSvcFormat() const {
  QUICHE_DCHECK(IsKnown());
  return VersionSupportsGoogleAltSvcFormat(transport_version);
}

bool ParsedQuicVersion::UsesHttp3() const {
  QUICHE_DCHECK(IsKnown());
  return VersionUsesHttp3(transport_version);
}

bool ParsedQuicVersion::HasLongHeaderLengths() const {
  QUICHE_DCHECK(IsKnown());
  return QuicVersionHasLongHeaderLengths(transport_version);
}

bool ParsedQuicVersion::UsesCryptoFrames() const {
  QUICHE_DCHECK(IsKnown());
  return QuicVersionUsesCryptoFrames(transport_version);
}

bool ParsedQuicVersion::HasIetfQuicFrames() const {
  QUICHE_DCHECK(IsKnown());
  return VersionHasIetfQuicFrames(transport_version);
}

bool ParsedQuicVersion::UsesLegacyTlsExtension() const {
  QUICHE_DCHECK(IsKnown());
  return UsesTls() && transport_version <= QUIC_VERSION_IETF_DRAFT_29;
}

bool ParsedQuicVersion::UsesTls() const {
  QUICHE_DCHECK(IsKnown());
  return handshake_protocol == PROTOCOL_TLS1_3;
}

bool ParsedQuicVersion::UsesQuicCrypto() const {
  QUICHE_DCHECK(IsKnown());
  return handshake_protocol == PROTOCOL_QUIC_CRYPTO;
}

bool ParsedQuicVersion::UsesV2PacketTypes() const {
  QUICHE_DCHECK(IsKnown());
  return transport_version == QUIC_VERSION_IETF_RFC_V2;
}

bool ParsedQuicVersion::AlpnDeferToRFCv1() const {
  QUICHE_DCHECK(IsKnown());
  return transport_version == QUIC_VERSION_IETF_RFC_V2;
}

bool VersionHasLengthPrefixedConnectionIds(
    QuicTransportVersion transport_version) {
  QUICHE_DCHECK(transport_version != QUIC_VERSION_UNSUPPORTED);
  // Length-prefixed connection IDs were added in version 49.
  return transport_version > QUIC_VERSION_46;
}

std::ostream& operator<<(std::ostream& os, const ParsedQuicVersion& version) {
  os << ParsedQuicVersionToString(version);
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const ParsedQuicVersionVector& versions) {
  os << ParsedQuicVersionVectorToString(versions);
  return os;
}

QuicVersionLabel MakeVersionLabel(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return MakeQuicTag(d, c, b, a);
}

std::ostream& operator<<(std::ostream& os,
                         const QuicVersionLabelVector& version_labels) {
  os << QuicVersionLabelVectorToString(version_labels);
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const QuicTransportVersionVector& transport_versions) {
  os << QuicTransportVersionVectorToString(transport_versions);
  return os;
}

QuicVersionLabel CreateQuicVersionLabel(ParsedQuicVersion parsed_version) {
  static_assert(SupportedVersions().size() == 5u,
                "Supported versions out of sync");
  if (parsed_version == ParsedQuicVersion::RFCv2()) {
    return MakeVersionLabel(0x6b, 0x33, 0x43, 0xcf);
  } else if (parsed_version == ParsedQuicVersion::RFCv1()) {
    return MakeVersionLabel(0x00, 0x00, 0x00, 0x01);
  } else if (parsed_version == ParsedQuicVersion::Draft29()) {
    return MakeVersionLabel(0xff, 0x00, 0x00, 29);
  } else if (parsed_version == ParsedQuicVersion::Q050()) {
    return MakeVersionLabel('Q', '0', '5', '0');
  } else if (parsed_version == ParsedQuicVersion::Q046()) {
    return MakeVersionLabel('Q', '0', '4', '6');
  } else if (parsed_version == ParsedQuicVersion::ReservedForNegotiation()) {
    return CreateRandomVersionLabelForNegotiation();
  }
  QUIC_BUG(quic_bug_10589_2)
      << "Unsupported version "
      << QuicVersionToString(parsed_version.transport_version) << " "
      << HandshakeProtocolToString(parsed_version.handshake_protocol);
  return 0;
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
  QUIC_BUG_IF(quic_bug_10589_3, versions.empty())
      << "No version with QUIC crypto found.";
  return versions;
}

ParsedQuicVersionVector CurrentSupportedVersionsWithQuicCrypto() {
  ParsedQuicVersionVector versions;
  for (const ParsedQuicVersion& version : CurrentSupportedVersions()) {
    if (version.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
      versions.push_back(version);
    }
  }
  QUIC_BUG_IF(quic_bug_10589_4, versions.empty())
      << "No version with QUIC crypto found.";
  return versions;
}

ParsedQuicVersionVector AllSupportedVersionsWithTls() {
  ParsedQuicVersionVector versions;
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    if (version.UsesTls()) {
      versions.push_back(version);
    }
  }
  QUIC_BUG_IF(quic_bug_10589_5, versions.empty())
      << "No version with TLS handshake found.";
  return versions;
}

ParsedQuicVersionVector CurrentSupportedVersionsWithTls() {
  ParsedQuicVersionVector versions;
  for (const ParsedQuicVersion& version : CurrentSupportedVersions()) {
    if (version.UsesTls()) {
      versions.push_back(version);
    }
  }
  QUIC_BUG_IF(quic_bug_10589_6, versions.empty())
      << "No version with TLS handshake found.";
  return versions;
}

ParsedQuicVersionVector ObsoleteSupportedVersions() {
  return ParsedQuicVersionVector{quic::ParsedQuicVersion::Q046(),
                                 quic::ParsedQuicVersion::Q050(),
                                 quic::ParsedQuicVersion::Draft29()};
}

bool IsObsoleteSupportedVersion(ParsedQuicVersion version) {
  static const ParsedQuicVersionVector obsolete_versions =
      ObsoleteSupportedVersions();
  for (const ParsedQuicVersion& obsolete_version : obsolete_versions) {
    if (version == obsolete_version) {
      return true;
    }
  }
  return false;
}

ParsedQuicVersionVector CurrentSupportedVersionsForClients() {
  ParsedQuicVersionVector versions;
  for (const ParsedQuicVersion& version : CurrentSupportedVersionsWithTls()) {
    QUICHE_DCHECK_EQ(version.handshake_protocol, PROTOCOL_TLS1_3);
    if (version.transport_version >= QUIC_VERSION_IETF_RFC_V1) {
      versions.push_back(version);
    }
  }
  QUIC_BUG_IF(quic_bug_10589_8, versions.empty())
      << "No supported client versions found.";
  return versions;
}

ParsedQuicVersionVector CurrentSupportedHttp3Versions() {
  ParsedQuicVersionVector versions;
  for (const ParsedQuicVersion& version : CurrentSupportedVersions()) {
    if (version.UsesHttp3()) {
      versions.push_back(version);
    }
  }
  QUIC_BUG_IF(no_version_uses_http3, versions.empty())
      << "No version speaking Http3 found.";
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

ParsedQuicVersionVector ParseQuicVersionLabelVector(
    const QuicVersionLabelVector& version_labels) {
  ParsedQuicVersionVector parsed_versions;
  for (const QuicVersionLabel& version_label : version_labels) {
    ParsedQuicVersion parsed_version = ParseQuicVersionLabel(version_label);
    if (parsed_version.IsKnown()) {
      parsed_versions.push_back(parsed_version);
    }
  }
  return parsed_versions;
}

ParsedQuicVersion ParseQuicVersionString(absl::string_view version_string) {
  if (version_string.empty()) {
    return UnsupportedQuicVersion();
  }
  const ParsedQuicVersionVector supported_versions = AllSupportedVersions();
  for (const ParsedQuicVersion& version : supported_versions) {
    if (version_string == ParsedQuicVersionToString(version) ||
        (version_string == AlpnForVersion(version) &&
         !version.AlpnDeferToRFCv1()) ||
        (version.handshake_protocol == PROTOCOL_QUIC_CRYPTO &&
         version_string == QuicVersionToString(version.transport_version))) {
      return version;
    }
  }
  for (const ParsedQuicVersion& version : supported_versions) {
    if (version.UsesHttp3() &&
        version_string ==
            QuicVersionLabelToString(CreateQuicVersionLabel(version))) {
      return version;
    }
  }
  int quic_version_number = 0;
  if (absl::SimpleAtoi(version_string, &quic_version_number) &&
      quic_version_number > 0) {
    QuicTransportVersion transport_version =
        static_cast<QuicTransportVersion>(quic_version_number);
    if (!ParsedQuicVersionIsValid(PROTOCOL_QUIC_CRYPTO, transport_version)) {
      return UnsupportedQuicVersion();
    }
    ParsedQuicVersion version(PROTOCOL_QUIC_CRYPTO, transport_version);
    if (std::find(supported_versions.begin(), supported_versions.end(),
                  version) != supported_versions.end()) {
      return version;
    }
    return UnsupportedQuicVersion();
  }
  // Reading from the client so this should not be considered an ERROR.
  QUIC_DLOG(INFO) << "Unsupported QUIC version string: \"" << version_string
                  << "\".";
  return UnsupportedQuicVersion();
}

ParsedQuicVersionVector ParseQuicVersionVectorString(
    absl::string_view versions_string) {
  ParsedQuicVersionVector versions;
  std::vector<absl::string_view> version_strings =
      absl::StrSplit(versions_string, ',');
  for (absl::string_view version_string : version_strings) {
    quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(
        &version_string);
    ParsedQuicVersion version = ParseQuicVersionString(version_string);
    if (!version.IsKnown() || std::find(versions.begin(), versions.end(),
                                        version) != versions.end()) {
      continue;
    }
    versions.push_back(version);
  }
  return versions;
}

QuicTransportVersionVector AllSupportedTransportVersions() {
  QuicTransportVersionVector transport_versions;
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    if (std::find(transport_versions.begin(), transport_versions.end(),
                  version.transport_version) == transport_versions.end()) {
      transport_versions.push_back(version.transport_version);
    }
  }
  return transport_versions;
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
  static_assert(SupportedVersions().size() == 5u,
                "Supported versions out of sync");
  ParsedQuicVersionVector filtered_versions;
  filtered_versions.reserve(versions.size());
  for (const ParsedQuicVersion& version : versions) {
    if (version == ParsedQuicVersion::RFCv2()) {
      if (GetQuicReloadableFlag(quic_enable_version_rfcv2)) {
        filtered_versions.push_back(version);
      }
    } else if (version == ParsedQuicVersion::RFCv1()) {
      if (!GetQuicReloadableFlag(quic_disable_version_rfcv1)) {
        filtered_versions.push_back(version);
      }
    } else if (version == ParsedQuicVersion::Draft29()) {
      if (!GetQuicReloadableFlag(quic_disable_version_draft_29)) {
        filtered_versions.push_back(version);
      }
    } else if (version == ParsedQuicVersion::Q050()) {
      if (!GetQuicReloadableFlag(quic_disable_version_q050)) {
        filtered_versions.push_back(version);
      }
    } else if (version == ParsedQuicVersion::Q046()) {
      if (!GetQuicReloadableFlag(quic_disable_version_q046)) {
        filtered_versions.push_back(version);
      }
    } else {
      QUIC_BUG(quic_bug_10589_7)
          << "QUIC version " << version << " has no flag protection";
      filtered_versions.push_back(version);
    }
  }
  return filtered_versions;
}

ParsedQuicVersionVector ParsedVersionOfIndex(
    const ParsedQuicVersionVector& versions, int index) {
  ParsedQuicVersionVector version;
  int version_count = versions.size();
  if (index >= 0 && index < version_count) {
    version.push_back(versions[index]);
  } else {
    version.push_back(UnsupportedQuicVersion());
  }
  return version;
}

std::string QuicVersionLabelToString(QuicVersionLabel version_label) {
  return QuicTagToString(quiche::QuicheEndian::HostToNet32(version_label));
}

ParsedQuicVersion ParseQuicVersionLabelString(
    absl::string_view version_label_string) {
  const ParsedQuicVersionVector supported_versions = AllSupportedVersions();
  for (const ParsedQuicVersion& version : supported_versions) {
    if (version_label_string ==
        QuicVersionLabelToString(CreateQuicVersionLabel(version))) {
      return version;
    }
  }
  return UnsupportedQuicVersion();
}

std::string QuicVersionLabelVectorToString(
    const QuicVersionLabelVector& version_labels, const std::string& separator,
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

#define RETURN_STRING_LITERAL(x) \
  case x:                        \
    return #x

std::string QuicVersionToString(QuicTransportVersion transport_version) {
  switch (transport_version) {
    RETURN_STRING_LITERAL(QUIC_VERSION_46);
    RETURN_STRING_LITERAL(QUIC_VERSION_50);
    RETURN_STRING_LITERAL(QUIC_VERSION_IETF_DRAFT_29);
    RETURN_STRING_LITERAL(QUIC_VERSION_IETF_RFC_V1);
    RETURN_STRING_LITERAL(QUIC_VERSION_IETF_RFC_V2);
    RETURN_STRING_LITERAL(QUIC_VERSION_UNSUPPORTED);
    RETURN_STRING_LITERAL(QUIC_VERSION_RESERVED_FOR_NEGOTIATION);
  }
  return absl::StrCat("QUIC_VERSION_UNKNOWN(",
                      static_cast<int>(transport_version), ")");
}

std::string HandshakeProtocolToString(HandshakeProtocol handshake_protocol) {
  switch (handshake_protocol) {
    RETURN_STRING_LITERAL(PROTOCOL_UNSUPPORTED);
    RETURN_STRING_LITERAL(PROTOCOL_QUIC_CRYPTO);
    RETURN_STRING_LITERAL(PROTOCOL_TLS1_3);
  }
  return absl::StrCat("PROTOCOL_UNKNOWN(", static_cast<int>(handshake_protocol),
                      ")");
}

std::string ParsedQuicVersionToString(ParsedQuicVersion version) {
  static_assert(SupportedVersions().size() == 5u,
                "Supported versions out of sync");
  if (version == UnsupportedQuicVersion()) {
    return "0";
  } else if (version == ParsedQuicVersion::RFCv2()) {
    QUICHE_DCHECK(version.UsesHttp3());
    return "RFCv2";
  } else if (version == ParsedQuicVersion::RFCv1()) {
    QUICHE_DCHECK(version.UsesHttp3());
    return "RFCv1";
  } else if (version == ParsedQuicVersion::Draft29()) {
    QUICHE_DCHECK(version.UsesHttp3());
    return "draft29";
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
    const ParsedQuicVersionVector& versions, const std::string& separator,
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
  QUICHE_DCHECK_NE(transport_version, QUIC_VERSION_UNSUPPORTED);
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

  // Check Q043, Q044, Q045, Q046, Q047 and Q048.
  for (uint8_t c = '3'; c <= '8'; ++c) {
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
  return ParsedQuicVersion::Unsupported();
}

ParsedQuicVersion QuicVersionReservedForNegotiation() {
  return ParsedQuicVersion::ReservedForNegotiation();
}

std::string AlpnForVersion(ParsedQuicVersion parsed_version) {
  if (parsed_version == ParsedQuicVersion::RFCv2()) {
    return "h3";
  } else if (parsed_version == ParsedQuicVersion::RFCv1()) {
    return "h3";
  } else if (parsed_version == ParsedQuicVersion::Draft29()) {
    return "h3-29";
  }
  return "h3-" + ParsedQuicVersionToString(parsed_version);
}

void QuicEnableVersion(const ParsedQuicVersion& version) {
  SetVersionFlag(version, /*should_enable=*/true);
}

void QuicDisableVersion(const ParsedQuicVersion& version) {
  SetVersionFlag(version, /*should_enable=*/false);
}

bool QuicVersionIsEnabled(const ParsedQuicVersion& version) {
  ParsedQuicVersionVector current = CurrentSupportedVersions();
  return std::find(current.begin(), current.end(), version) != current.end();
}

#undef RETURN_STRING_LITERAL  // undef for jumbo builds
}  // namespace quic
