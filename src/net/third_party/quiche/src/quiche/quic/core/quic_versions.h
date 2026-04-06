// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Definitions and utility functions related to handling of QUIC versions.
//
// QUIC versions are encoded over the wire as an opaque 32bit field. The wire
// encoding is represented in memory as a QuicVersionLabel type (which is an
// alias to uint32_t). Conceptual versions are represented in memory as
// ParsedQuicVersion.
//
// We currently support two kinds of QUIC versions, GoogleQUIC and IETF QUIC.
//
// All GoogleQUIC versions use a wire encoding that matches the following regex
// when converted to ASCII: "[QT]0\d\d" (e.g. Q046). Q or T distinguishes the
// type of handshake used (Q for the QUIC_CRYPTO handshake, T for the QUIC+TLS
// handshake), and the two digits at the end contain the numeric value of
// the transport version used.
//
// All IETF QUIC versions use the wire encoding described in:
// https://tools.ietf.org/html/draft-ietf-quic-transport

#ifndef QUICHE_QUIC_CORE_QUIC_VERSIONS_H_
#define QUICHE_QUIC_CORE_QUIC_VERSIONS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

// The list of existing QUIC transport versions. Note that QUIC versions are
// sent over the wire as an encoding of ParsedQuicVersion, which requires a
// QUIC transport version and handshake protocol. For transport versions of the
// form QUIC_VERSION_XX where XX is decimal, the enum numeric value is
// guaranteed to match the name. Older deprecated transport versions are
// documented in comments below.
enum QuicTransportVersion {
  // Special case to indicate unknown/unsupported QUIC version.
  QUIC_VERSION_UNSUPPORTED = 0,

  // Version 1 was the first version of QUIC that supported versioning.
  // Version 2 decoupled versioning of non-cryptographic parameters from the
  //           SCFG.
  // Version 3 moved public flags into the beginning of the packet.
  // Version 4 added support for variable-length connection IDs.
  // Version 5 made specifying FEC groups optional.
  // Version 6 introduced variable-length packet numbers.
  // Version 7 introduced a lower-overhead encoding for stream frames.
  // Version 8 made salt length equal to digest length for the RSA-PSS
  //           signatures.
  // Version 9 added stream priority.
  // Version 10 redid the frame type numbering.
  // Version 11 reduced the length of null encryption authentication tag
  //            from 16 to 12 bytes.
  // Version 12 made the sequence numbers in the ACK frames variable-sized.
  // Version 13 added the dedicated header stream.
  // Version 14 added byte_offset to RST_STREAM frame.
  // Version 15 added a list of packets recovered using FEC to the ACK frame.
  // Version 16 added STOP_WAITING frame.
  // Version 17 added per-stream flow control.
  // Version 18 added PING frame.
  // Version 19 added connection-level flow control
  // Version 20 allowed to set stream- and connection-level flow control windows
  //            to different values.
  // Version 21 made header and crypto streams flow-controlled.
  // Version 22 added support for SCUP (server config update) messages.
  // Version 23 added timestamps into the ACK frame.
  // Version 24 added SPDY/4 header compression.
  // Version 25 added support for SPDY/4 header keys and removed error_details
  //            from RST_STREAM frame.
  // Version 26 added XLCT (expected leaf certificate) tag into CHLO.
  // Version 27 added a nonce into SHLO.
  // Version 28 allowed receiver to refuse creating a requested stream.
  // Version 29 added support for QUIC_STREAM_NO_ERROR.
  // Version 30 added server-side support for certificate transparency.
  // Version 31 incorporated the hash of CHLO into the crypto proof supplied by
  //            the server.
  // Version 32 removed FEC-related fields from wire format.
  // Version 33 added diversification nonces.
  // Version 34 removed entropy bits from packets and ACK frames, removed
  //            private flag from packet header and changed the ACK format to
  //            specify ranges of packets acknowledged rather than missing
  //            ranges.
  // Version 35 allows endpoints to independently set stream limit.
  // Version 36 added support for forced head-of-line blocking experiments.
  // Version 37 added perspective into null encryption.
  // Version 38 switched to IETF padding frame format and support for NSTP (no
  //            stop waiting frame) connection option.

  // Version 39 writes integers and floating numbers in big endian, stops acking
  // acks, sends a connection level WINDOW_UPDATE every 20 sent packets which do
  // not contain retransmittable frames.

  // Version 40 was an attempt to convert QUIC to IETF frame format; it was
  //            never shipped due to a bug.
  // Version 41 was a bugfix for version 40.  The working group changed the wire
  //            format before it shipped, which caused it to be never shipped
  //            and all the changes from it to be reverted.  No changes from v40
  //            or v41 are present in subsequent versions.
  // Version 42 allowed receiving overlapping stream data.

  // Version 43 PRIORITY frames are sent by client and accepted by server.
  // Version 44 used IETF header format from draft-ietf-quic-invariants-05.

  // Version 45 added MESSAGE frame.

  QUIC_VERSION_46 = 46,  // Use IETF draft-17 header format with demultiplexing
                         // bit.
  // Version 47 added variable-length QUIC server connection IDs.
  // Version 48 added CRYPTO frames for the handshake.
  // Version 49 added client connection IDs, long header lengths, and the IETF
  // header format from draft-ietf-quic-invariants-06
  // Number 50 added header protection and initial obfuscators.
  // Number 51 was T051 which used draft-29 features but with GoogleQUIC frames.
  // Number 70 used to represent draft-ietf-quic-transport-25.
  // Number 71 used to represent draft-ietf-quic-transport-27.
  // Number 72 used to represent draft-ietf-quic-transport-28.
  QUIC_VERSION_IETF_DRAFT_29 = 73,  // draft-ietf-quic-transport-29.
  QUIC_VERSION_IETF_RFC_V1 = 80,    // RFC 9000.
  // Number 81 used to represent draft-ietf-quic-v2-01.
  QUIC_VERSION_IETF_RFC_V2 = 82,  // RFC 9369.
  // Version 99 was a dumping ground for IETF QUIC changes which were not yet
  // ready for production between 2018-02 and 2020-02.

  // QUIC_VERSION_RESERVED_FOR_NEGOTIATION is sent over the wire as ?a?a?a?a
  // which is part of a range reserved by the IETF for version negotiation
  // testing (see the "Versions" section of draft-ietf-quic-transport).
  // This version is intentionally meant to never be supported to trigger
  // version negotiation when proposed by clients and to prevent client
  // ossification when sent by servers.
  QUIC_VERSION_RESERVED_FOR_NEGOTIATION = 999,
  QUIC_VERSION_MAX_VALUE = QUIC_VERSION_RESERVED_FOR_NEGOTIATION,
};

// Helper function which translates from a QuicTransportVersion to a string.
// Returns strings corresponding to enum names (e.g. QUIC_VERSION_6).
QUICHE_EXPORT std::string QuicVersionToString(
    QuicTransportVersion transport_version);

// Returns whether this version is documented by an IETF internet-draft or RFC.
QUICHE_EXPORT constexpr bool VersionIsIetfQuic(
    QuicTransportVersion transport_version) {
  return transport_version > QUIC_VERSION_46;
}

QUICHE_EXPORT constexpr bool TransportVersionIsValid(
    QuicTransportVersion transport_version) {
  constexpr QuicTransportVersion valid_transport_versions[] = {
      QUIC_VERSION_IETF_RFC_V2,
      QUIC_VERSION_IETF_RFC_V1,
      QUIC_VERSION_IETF_DRAFT_29,
      QUIC_VERSION_46,
      QUIC_VERSION_RESERVED_FOR_NEGOTIATION,
      QUIC_VERSION_UNSUPPORTED,
  };
  for (size_t i = 0; i < ABSL_ARRAYSIZE(valid_transport_versions); ++i) {
    if (transport_version == valid_transport_versions[i]) {
      return true;
    }
  }
  return false;
}

// A parsed QUIC version label which determines that handshake protocol
// and the transport version.
struct QUICHE_EXPORT ParsedQuicVersion {
  QuicTransportVersion transport_version;

  constexpr ParsedQuicVersion(QuicTransportVersion transport_version)
      : transport_version(transport_version) {}

  constexpr ParsedQuicVersion(const ParsedQuicVersion& other)
      : ParsedQuicVersion(other.transport_version) {}

  ParsedQuicVersion& operator=(const ParsedQuicVersion& other) {
    if (this != &other) {
      transport_version = other.transport_version;
    }
    return *this;
  }

  bool operator==(const ParsedQuicVersion& other) const = default;
  bool operator!=(const ParsedQuicVersion& other) const = default;

  static constexpr ParsedQuicVersion RFCv2() {
    return ParsedQuicVersion(QUIC_VERSION_IETF_RFC_V2);
  }

  static constexpr ParsedQuicVersion RFCv1() {
    return ParsedQuicVersion(QUIC_VERSION_IETF_RFC_V1);
  }

  static constexpr ParsedQuicVersion Draft29() {
    return ParsedQuicVersion(QUIC_VERSION_IETF_DRAFT_29);
  }

  static constexpr ParsedQuicVersion Q046() {
    return ParsedQuicVersion(QUIC_VERSION_46);
  }

  static constexpr ParsedQuicVersion Unsupported() {
    return ParsedQuicVersion(QUIC_VERSION_UNSUPPORTED);
  }

  static constexpr ParsedQuicVersion ReservedForNegotiation() {
    return ParsedQuicVersion(QUIC_VERSION_RESERVED_FOR_NEGOTIATION);
  }

  // Returns whether our codebase understands this version. This should only be
  // called on valid versions, see TransportVersionIsValid. Assuming the
  // version is valid, IsKnown returns whether the version is not
  // UnsupportedQuicVersion.
  bool IsKnown() const;

  // Returns true if the version is not Q046. Q046 is the only supported version
  // that is not documented by an IETF internet-draft or RFC, and has numerous
  // unique properties.
  bool IsIetfQuic() const;

  // Returns whether this version uses the legacy TLS extension codepoint.
  bool UsesLegacyTlsExtension() const;

  // Returns whether this version uses the QUICv2 Long Header Packet Types.
  bool UsesV2PacketTypes() const;

  // Returns true if this shares ALPN codes with RFCv1, and endpoints should
  // choose RFCv1 when presented with a v1 ALPN. Note that this is false for
  // RFCv1.
  bool AlpnDeferToRFCv1() const;
};

QUICHE_EXPORT ParsedQuicVersion UnsupportedQuicVersion();

QUICHE_EXPORT ParsedQuicVersion QuicVersionReservedForNegotiation();

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const ParsedQuicVersion& version);

using ParsedQuicVersionVector = std::vector<ParsedQuicVersion>;

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const ParsedQuicVersionVector& versions);

// Representation of the on-the-wire QUIC version number. Will be written/read
// to the wire in network-byte-order.
using QuicVersionLabel = uint32_t;
using QuicVersionLabelVector = std::vector<QuicVersionLabel>;

// Constructs a version label from the 4 bytes such that the on-the-wire
// order will be: d, c, b, a.
QUICHE_EXPORT QuicVersionLabel MakeVersionLabel(uint8_t a, uint8_t b, uint8_t c,
                                                uint8_t d);

QUICHE_EXPORT std::ostream& operator<<(
    std::ostream& os, const QuicVersionLabelVector& version_labels);

constexpr std::array<ParsedQuicVersion, 4> SupportedVersions() {
  return {
      ParsedQuicVersion::RFCv2(),
      ParsedQuicVersion::RFCv1(),
      ParsedQuicVersion::Draft29(),
      ParsedQuicVersion::Q046(),
  };
}

using QuicTransportVersionVector = std::vector<QuicTransportVersion>;

QUICHE_EXPORT std::ostream& operator<<(
    std::ostream& os, const QuicTransportVersionVector& transport_versions);

// Returns a vector of supported QUIC versions.
QUICHE_EXPORT ParsedQuicVersionVector AllSupportedVersions();

// Returns a vector of supported QUIC versions, with any versions disabled by
// flags excluded.
QUICHE_EXPORT ParsedQuicVersionVector CurrentSupportedVersions();

// Obsolete QUIC supported versions are versions that are supported in
// QUICHE but which should not be used by by modern clients.
QUICHE_EXPORT ParsedQuicVersionVector ObsoleteSupportedVersions();

// Returns true if `version` is in `ObsoleteSupportedVersions`.
QUICHE_EXPORT bool IsObsoleteSupportedVersion(ParsedQuicVersion version);

// Returns a vector of supported QUIC versions which should be used by clients.
// Server need to support old clients, but new client should only be using
// QUIC versions in this list.
QUICHE_EXPORT ParsedQuicVersionVector CurrentSupportedVersionsForClients();

// Returns a vector of QUIC versions from |versions| which exclude any versions
// which are disabled by flags.
QUICHE_EXPORT ParsedQuicVersionVector
FilterSupportedVersions(ParsedQuicVersionVector versions);

// Returns a subset of AllSupportedVersions() that are gQUIC.
QUICHE_EXPORT ParsedQuicVersionVector AllSupportedVersionsWithQuicCrypto();

// Returns a subset of CurrentSupportedVersions() that are gQUIC.
QUICHE_EXPORT ParsedQuicVersionVector CurrentSupportedVersionsWithQuicCrypto();

// Returns a subset of AllSupportedVersions() that are IETF QUIC.
QUICHE_EXPORT ParsedQuicVersionVector AllSupportedVersionsWithTls();

// Returns a subset of CurrentSupportedVersions() that are IETF QUIC.
QUICHE_EXPORT ParsedQuicVersionVector CurrentSupportedVersionsWithTls();

// Returns a subset of CurrentSupportedVersions() using HTTP/3 at the HTTP
// layer.
QUICHE_EXPORT ParsedQuicVersionVector CurrentSupportedHttp3Versions();

// Returns QUIC version of |index| in result of |versions|. Returns
// UnsupportedQuicVersion() if |index| is out of bounds.
QUICHE_EXPORT ParsedQuicVersionVector
ParsedVersionOfIndex(const ParsedQuicVersionVector& versions, int index);

// QuicVersionLabel is written to and read from the wire, but we prefer to use
// the more readable ParsedQuicVersion at other levels.
// Helper function which translates from a QuicVersionLabel to a
// ParsedQuicVersion.
QUICHE_EXPORT ParsedQuicVersion
ParseQuicVersionLabel(QuicVersionLabel version_label);

// Helper function that translates from a QuicVersionLabelVector to a
// ParsedQuicVersionVector.
QUICHE_EXPORT ParsedQuicVersionVector
ParseQuicVersionLabelVector(const QuicVersionLabelVector& version_labels);

// Parses a QUIC version string such as "Q043" or "T051". Also supports parsing
// ALPN such as "h3-29" or "h3-Q046". For PROTOCOL_QUIC_CRYPTO versions, also
// supports parsing numbers such as "46".
QUICHE_EXPORT ParsedQuicVersion
ParseQuicVersionString(absl::string_view version_string);

// Parses a comma-separated list of QUIC version strings. Supports parsing by
// label, ALPN and numbers for PROTOCOL_QUIC_CRYPTO. Skips unknown versions.
// For example: "h3-29,Q046,46".
QUICHE_EXPORT ParsedQuicVersionVector
ParseQuicVersionVectorString(absl::string_view versions_string);

// Constructs a QuicVersionLabel from the provided ParsedQuicVersion.
// QuicVersionLabel is written to and read from the wire, but we prefer to use
// the more readable ParsedQuicVersion at other levels.
// Helper function which translates from a ParsedQuicVersion to a
// QuicVersionLabel. Returns 0 if |parsed_version| is unsupported.
QUICHE_EXPORT QuicVersionLabel
CreateQuicVersionLabel(ParsedQuicVersion parsed_version);

// Constructs a QuicVersionLabelVector from the provided
// ParsedQuicVersionVector.
QUICHE_EXPORT QuicVersionLabelVector
CreateQuicVersionLabelVector(const ParsedQuicVersionVector& versions);

// Helper function which translates from a QuicVersionLabel to a string.
QUICHE_EXPORT std::string QuicVersionLabelToString(
    QuicVersionLabel version_label);

// Helper function which translates from a QuicVersionLabel string to a
// ParsedQuicVersion. The version label string must be of the form returned
// by QuicVersionLabelToString, for example, "00000001" or "Q046", but not
// "51303433" (the hex encoding of the Q064 version label). Returns
// the ParsedQuicVersion which matches the label or UnsupportedQuicVersion()
// otherwise.
QUICHE_EXPORT ParsedQuicVersion
ParseQuicVersionLabelString(absl::string_view version_label_string);

// Returns |separator|-separated list of string representations of
// QuicVersionLabel values in the supplied |version_labels| vector. The values
// after the (0-based) |skip_after_nth_version|'th are skipped.
QUICHE_EXPORT std::string QuicVersionLabelVectorToString(
    const QuicVersionLabelVector& version_labels, const std::string& separator,
    size_t skip_after_nth_version);

// Returns comma separated list of string representations of QuicVersionLabel
// values in the supplied |version_labels| vector.
QUICHE_EXPORT inline std::string QuicVersionLabelVectorToString(
    const QuicVersionLabelVector& version_labels) {
  return QuicVersionLabelVectorToString(version_labels, ",",
                                        std::numeric_limits<size_t>::max());
}

// Helper function which translates from a ParsedQuicVersion to a string.
// Returns strings corresponding to the on-the-wire tag.
QUICHE_EXPORT std::string ParsedQuicVersionToString(ParsedQuicVersion version);

// Returns a vector of supported QUIC transport versions. DEPRECATED, use
// AllSupportedVersions instead.
QUICHE_EXPORT QuicTransportVersionVector AllSupportedTransportVersions();

// Returns comma separated list of string representations of
// QuicTransportVersion enum values in the supplied |versions| vector.
QUICHE_EXPORT std::string QuicTransportVersionVectorToString(
    const QuicTransportVersionVector& versions);

// Returns comma separated list of string representations of ParsedQuicVersion
// values in the supplied |versions| vector.
QUICHE_EXPORT std::string ParsedQuicVersionVectorToString(
    const ParsedQuicVersionVector& versions);

// Returns |separator|-separated list of string representations of
// ParsedQuicVersion values in the supplied |versions| vector. The values after
// the (0-based) |skip_after_nth_version|'th are skipped.
QUICHE_EXPORT std::string ParsedQuicVersionVectorToString(
    const ParsedQuicVersionVector& versions, const std::string& separator,
    size_t skip_after_nth_version);

// Returns comma separated list of string representations of ParsedQuicVersion
// values in the supplied |versions| vector.
QUICHE_EXPORT inline std::string ParsedQuicVersionVectorToString(
    const ParsedQuicVersionVector& versions) {
  return ParsedQuicVersionVectorToString(versions, ",",
                                         std::numeric_limits<size_t>::max());
}

// Returns whether this version label supports long header 4-bit encoded
// connection ID lengths as described in draft-ietf-quic-invariants-05 and
// draft-ietf-quic-transport-21.
QUICHE_EXPORT bool QuicVersionLabelUses4BitConnectionIdLength(
    QuicVersionLabel version_label);

// Returns the ALPN string to use in TLS for this version of QUIC.
QUICHE_EXPORT std::string AlpnForVersion(ParsedQuicVersion parsed_version);

// Configures the flags required to enable support for this version of QUIC.
QUICHE_EXPORT void QuicEnableVersion(const ParsedQuicVersion& version);

// Configures the flags required to disable support for this version of QUIC.
QUICHE_EXPORT void QuicDisableVersion(const ParsedQuicVersion& version);

// Returns whether support for this version of QUIC is currently enabled.
QUICHE_EXPORT bool QuicVersionIsEnabled(const ParsedQuicVersion& version);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_VERSIONS_H_
