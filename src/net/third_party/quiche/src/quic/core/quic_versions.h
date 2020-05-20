// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Definitions and utility functions related to handling of QUIC versions.
//
// QUIC version is a four-byte tag that can be represented in memory as a
// QuicVersionLabel type (which is an alias to uint32_t).  In actuality, all
// versions supported by this implementation have the following format:
//   [QT]0\d\d
// e.g. Q046.  Q or T distinguishes the type of handshake used (Q for QUIC
// Crypto handshake, T for TLS-based handshake), and the two digits at the end
// is the actual numeric value of transport version used by the code.

#ifndef QUICHE_QUIC_CORE_QUIC_VERSIONS_H_
#define QUICHE_QUIC_CORE_QUIC_VERSIONS_H_

#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/core/quic_tag.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// The available versions of QUIC.  The numeric value of the enum is guaranteed
// to match the number in the name.  The versions not currently supported are
// documented in comments.
//
// See go/new-quic-version for more details on how to roll out new versions.
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

  QUIC_VERSION_43 = 43,  // PRIORITY frames are sent by client and accepted by
                         // server.
  // Version 44 used IETF header format from draft-ietf-quic-invariants-05.

  // Version 45 added MESSAGE frame.

  QUIC_VERSION_46 = 46,  // Use IETF draft-17 header format with demultiplexing
                         // bit.
  // Version 47 added variable-length QUIC server connection IDs.
  QUIC_VERSION_48 = 48,  // Use CRYPTO frames for the handshake.
  QUIC_VERSION_49 = 49,  // Client connection IDs, long header lengths, IETF
                         // header format from draft-ietf-quic-invariants-06.
  QUIC_VERSION_50 = 50,  // Header protection and initial obfuscators.
  QUIC_VERSION_IETF_DRAFT_25 = 70,  // draft-ietf-quic-transport-25.
  QUIC_VERSION_IETF_DRAFT_27 = 71,  // draft-ietf-quic-transport-27.
  // Version 99 was a dumping ground for IETF QUIC changes which were not yet
  // yet ready for production between 2018-02 and 2020-02.

  // QUIC_VERSION_RESERVED_FOR_NEGOTIATION is sent over the wire as ?a?a?a?a
  // which is part of a range reserved by the IETF for version negotiation
  // testing (see the "Versions" section of draft-ietf-quic-transport).
  // This version is intentionally meant to never be supported to trigger
  // version negotiation when proposed by clients and to prevent client
  // ossification when sent by servers.
  QUIC_VERSION_RESERVED_FOR_NEGOTIATION = 999,
};

// This array contains QUIC transport versions which we currently support.
// This should be ordered such that the highest supported version is the first
// element, with subsequent elements in descending order (versions can be
// skipped as necessary).
//
// See go/new-quic-version for more details on how to roll out new versions.
constexpr std::array<QuicTransportVersion, 7> SupportedTransportVersions() {
  return {QUIC_VERSION_IETF_DRAFT_27,
          QUIC_VERSION_IETF_DRAFT_25,
          QUIC_VERSION_50,
          QUIC_VERSION_49,
          QUIC_VERSION_48,
          QUIC_VERSION_46,
          QUIC_VERSION_43};
}

// Helper function which translates from a QuicTransportVersion to a string.
// Returns strings corresponding to enum names (e.g. QUIC_VERSION_6).
QUIC_EXPORT_PRIVATE std::string QuicVersionToString(
    QuicTransportVersion transport_version);

// The crypto handshake protocols that can be used with QUIC.
enum HandshakeProtocol {
  PROTOCOL_UNSUPPORTED,
  PROTOCOL_QUIC_CRYPTO,
  PROTOCOL_TLS1_3,
};

// Helper function which translates from a HandshakeProtocol to a string.
QUIC_EXPORT_PRIVATE std::string HandshakeProtocolToString(
    HandshakeProtocol handshake_protocol);

// Returns whether |transport_version| uses CRYPTO frames for the handshake
// instead of stream 1.
QUIC_EXPORT_PRIVATE constexpr bool QuicVersionUsesCryptoFrames(
    QuicTransportVersion transport_version) {
  return transport_version >= QUIC_VERSION_48;
}

// Returns whether this combination of handshake protocol and transport
// version is allowed. For example, {PROTOCOL_TLS1_3, QUIC_VERSION_43} is NOT
// allowed as TLS requires crypto frames which v43 does not support. Note that
// UnsupportedQuicVersion is a valid version.
QUIC_EXPORT_PRIVATE constexpr bool ParsedQuicVersionIsValid(
    HandshakeProtocol handshake_protocol,
    QuicTransportVersion transport_version) {
  bool transport_version_is_valid =
      transport_version == QUIC_VERSION_UNSUPPORTED ||
      transport_version == QUIC_VERSION_RESERVED_FOR_NEGOTIATION;
  if (!transport_version_is_valid) {
    // Iterators are not constexpr in C++14 which Chrome uses.
    constexpr auto supported_transport_versions = SupportedTransportVersions();
    for (size_t i = 0; i < supported_transport_versions.size(); ++i) {
      const QuicTransportVersion& trans_vers = supported_transport_versions[i];
      if (trans_vers == transport_version) {
        transport_version_is_valid = true;
        break;
      }
    }
  }
  if (!transport_version_is_valid) {
    return false;
  }
  switch (handshake_protocol) {
    case PROTOCOL_UNSUPPORTED:
      return transport_version == QUIC_VERSION_UNSUPPORTED;
    case PROTOCOL_QUIC_CRYPTO:
      return transport_version != QUIC_VERSION_UNSUPPORTED &&
             transport_version != QUIC_VERSION_IETF_DRAFT_25 &&
             transport_version != QUIC_VERSION_IETF_DRAFT_27;
    case PROTOCOL_TLS1_3:
      // The TLS handshake is only deployable if CRYPTO frames are also used.
      // We explicitly removed support for T048 and T049 to reduce test load.
      return transport_version != QUIC_VERSION_UNSUPPORTED &&
             QuicVersionUsesCryptoFrames(transport_version) &&
             transport_version > QUIC_VERSION_49;
  }
  return false;
}

// A parsed QUIC version label which determines that handshake protocol
// and the transport version.
struct QUIC_EXPORT_PRIVATE ParsedQuicVersion {
  HandshakeProtocol handshake_protocol;
  QuicTransportVersion transport_version;

  constexpr ParsedQuicVersion(HandshakeProtocol handshake_protocol,
                              QuicTransportVersion transport_version)
      : handshake_protocol(handshake_protocol),
        transport_version(transport_version) {
    DCHECK(ParsedQuicVersionIsValid(handshake_protocol, transport_version))
        << QuicVersionToString(transport_version) << " "
        << HandshakeProtocolToString(handshake_protocol);
  }

  constexpr ParsedQuicVersion(const ParsedQuicVersion& other)
      : ParsedQuicVersion(other.handshake_protocol, other.transport_version) {}

  ParsedQuicVersion& operator=(const ParsedQuicVersion& other) {
    DCHECK(ParsedQuicVersionIsValid(other.handshake_protocol,
                                    other.transport_version))
        << QuicVersionToString(other.transport_version) << " "
        << HandshakeProtocolToString(other.handshake_protocol);
    if (this != &other) {
      handshake_protocol = other.handshake_protocol;
      transport_version = other.transport_version;
    }
    return *this;
  }

  bool operator==(const ParsedQuicVersion& other) const {
    return handshake_protocol == other.handshake_protocol &&
           transport_version == other.transport_version;
  }

  bool operator!=(const ParsedQuicVersion& other) const {
    return handshake_protocol != other.handshake_protocol ||
           transport_version != other.transport_version;
  }

  // Returns whether our codebase understands this version. This should only be
  // called on valid versions, see ParsedQuicVersionIsValid. Assuming the
  // version is valid, IsKnown returns whether the version is not
  // UnsupportedQuicVersion.
  bool IsKnown() const;

  bool KnowsWhichDecrypterToUse() const;

  // Returns whether this version uses keys derived from the Connection ID for
  // ENCRYPTION_INITIAL keys (instead of NullEncrypter/NullDecrypter).
  bool UsesInitialObfuscators() const;

  // Indicates that this QUIC version does not have an enforced minimum value
  // for flow control values negotiated during the handshake.
  bool AllowsLowFlowControlLimits() const;

  // Returns whether header protection is used in this version of QUIC.
  bool HasHeaderProtection() const;

  // Returns whether this version supports IETF RETRY packets.
  bool SupportsRetry() const;

  // Returns whether RETRY packets carry the Retry Integrity Tag field.
  bool HasRetryIntegrityTag() const;

  // Returns true if this version sends variable length packet number in long
  // header.
  bool SendsVariableLengthPacketNumberInLongHeader() const;

  // Returns whether this version allows server connection ID lengths
  // that are not 64 bits.
  bool AllowsVariableLengthConnectionIds() const;

  // Returns whether this version supports client connection ID.
  bool SupportsClientConnectionIds() const;

  // Returns whether this version supports long header 8-bit encoded
  // connection ID lengths as described in draft-ietf-quic-invariants-06 and
  // draft-ietf-quic-transport-22.
  bool HasLengthPrefixedConnectionIds() const;

  // Returns whether this version supports IETF style anti-amplification limit,
  // i.e., server will send no more than FLAGS_quic_anti_amplification_factor
  // times received bytes until address can be validated.
  bool SupportsAntiAmplificationLimit() const;

  // Returns true if this version can send coalesced packets.
  bool CanSendCoalescedPackets() const;

  // Returns true if this version supports the old Google-style Alt-Svc
  // advertisement format.
  bool SupportsGoogleAltSvcFormat() const;

  // Returns true if |transport_version| uses IETF invariant headers.
  bool HasIetfInvariantHeader() const;

  // Returns true if |transport_version| supports MESSAGE frames.
  bool SupportsMessageFrames() const;

  // If true, HTTP/3 instead of gQUIC will be used at the HTTP layer.
  // Notable changes are:
  // * Headers stream no longer exists.
  // * PRIORITY, HEADERS are moved from headers stream to HTTP/3 control stream.
  // * PUSH_PROMISE is moved to request stream.
  // * Unidirectional streams will have their first byte as a stream type.
  // * HEADERS frames are compressed using QPACK.
  // * DATA frame has frame headers.
  // * GOAWAY is moved to HTTP layer.
  bool UsesHttp3() const;

  // Returns whether the transport_version supports the variable length integer
  // length field as defined by IETF QUIC draft-13 and later.
  bool HasLongHeaderLengths() const;

  // Returns whether |transport_version| uses CRYPTO frames for the handshake
  // instead of stream 1.
  bool UsesCryptoFrames() const;

  // Returns whether |transport_version| makes use of IETF QUIC
  // frames or not.
  bool HasIetfQuicFrames() const;

  // Returns true if this parsed version supports handshake done.
  bool HasHandshakeDone() const;

  // Returns true if this version uses variable-length integers when
  // encoding transport parameter types and lengths.
  bool HasVarIntTransportParams() const;
};

QUIC_EXPORT_PRIVATE ParsedQuicVersion UnsupportedQuicVersion();

QUIC_EXPORT_PRIVATE ParsedQuicVersion QuicVersionReservedForNegotiation();

QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                             const ParsedQuicVersion& version);

using ParsedQuicVersionVector = std::vector<ParsedQuicVersion>;

// Representation of the on-the-wire QUIC version number. Will be written/read
// to the wire in network-byte-order.
using QuicVersionLabel = uint32_t;
using QuicVersionLabelVector = std::vector<QuicVersionLabel>;

// This vector contains all crypto handshake protocols that are supported.
constexpr std::array<HandshakeProtocol, 2> SupportedHandshakeProtocols() {
  return {PROTOCOL_QUIC_CRYPTO, PROTOCOL_TLS1_3};
}

constexpr std::array<ParsedQuicVersion, 8> SupportedVersions() {
  return {
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_50),
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_49),
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_48),
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_46),
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, QUIC_VERSION_43),
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_27),
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_IETF_DRAFT_25),
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_50),
  };
}

using QuicTransportVersionVector = std::vector<QuicTransportVersion>;

// Returns a vector of QUIC versions in kSupportedTransportVersions.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector AllSupportedTransportVersions();

// Returns a vector of QUIC versions that is the cartesian product of
// kSupportedTransportVersions and kSupportedHandshakeProtocols.
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector AllSupportedVersions();

// Returns a vector of QUIC versions that is the cartesian product of
// kSupportedTransportVersions and kSupportedHandshakeProtocols, with any
// versions disabled by flags excluded.
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector CurrentSupportedVersions();

// Returns a vector of QUIC versions from |versions| which exclude any versions
// which are disabled by flags.
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector
FilterSupportedVersions(ParsedQuicVersionVector versions);

// Returns a subset of AllSupportedVersions() with
// handshake_protocol == PROTOCOL_QUIC_CRYPTO, in the same order.
// Deprecated; only to be used in components that do not yet support
// PROTOCOL_TLS1_3.
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector
AllSupportedVersionsWithQuicCrypto();

// Returns a subset of CurrentSupportedVersions() with
// handshake_protocol == PROTOCOL_QUIC_CRYPTO, in the same order.
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector
CurrentSupportedVersionsWithQuicCrypto();

// Returns a subset of CurrentSupportedVersions() with handshake_protocol ==
// PROTOCOL_TLS1_3.
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector CurrentSupportedVersionsWithTls();

// Returns QUIC version of |index| in result of |versions|. Returns
// QUIC_VERSION_UNSUPPORTED if |index| is out of bounds.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector
VersionOfIndex(const QuicTransportVersionVector& versions, int index);

// Returns QUIC version of |index| in result of |versions|. Returns
// UnsupportedQuicVersion() if |index| is out of bounds.
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector
ParsedVersionOfIndex(const ParsedQuicVersionVector& versions, int index);

// Returns a vector of QuicTransportVersions corresponding to just the transport
// versions in |versions|. If the input vector contains multiple parsed versions
// with different handshake protocols (but the same transport version), that
// transport version will appear in the resulting vector multiple times.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector
ParsedVersionsToTransportVersions(const ParsedQuicVersionVector& versions);

// QuicVersionLabel is written to and read from the wire, but we prefer to use
// the more readable ParsedQuicVersion at other levels.
// Helper function which translates from a QuicVersionLabel to a
// ParsedQuicVersion.
QUIC_EXPORT_PRIVATE ParsedQuicVersion
ParseQuicVersionLabel(QuicVersionLabel version_label);

// Parses a QUIC version string such as "Q043" or "T050". Also supports parsing
// ALPN such as "h3-25" or "h3-Q050". For PROTOCOL_QUIC_CRYPTO versions, also
// supports parsing numbers such as "46".
QUIC_EXPORT_PRIVATE ParsedQuicVersion
ParseQuicVersionString(quiche::QuicheStringPiece version_string);

// Parses a comma-separated list of QUIC version strings. Supports parsing by
// label, ALPN and numbers for PROTOCOL_QUIC_CRYPTO. Skips unknown versions.
// For example: "h3-25,Q050,46".
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector
ParseQuicVersionVectorString(quiche::QuicheStringPiece versions_string);

// Constructs a QuicVersionLabel from the provided ParsedQuicVersion.
QUIC_EXPORT_PRIVATE QuicVersionLabel
CreateQuicVersionLabel(ParsedQuicVersion parsed_version);

// Constructs a QuicVersionLabelVector from the provided
// ParsedQuicVersionVector.
QUIC_EXPORT_PRIVATE QuicVersionLabelVector
CreateQuicVersionLabelVector(const ParsedQuicVersionVector& versions);

// QuicVersionLabel is written to and read from the wire, but we prefer to use
// the more readable QuicTransportVersion at other levels.
// Helper function which translates from a QuicTransportVersion to a
// QuicVersionLabel. Returns 0 if |version| is unsupported.
QUIC_EXPORT_PRIVATE QuicVersionLabel
QuicVersionToQuicVersionLabel(QuicTransportVersion transport_version);

// Helper function which translates from a QuicVersionLabel to a string.
QUIC_EXPORT_PRIVATE std::string QuicVersionLabelToString(
    QuicVersionLabel version_label);

// Returns |separator|-separated list of string representations of
// QuicVersionLabel values in the supplied |version_labels| vector. The values
// after the (0-based) |skip_after_nth_version|'th are skipped.
QUIC_EXPORT_PRIVATE std::string QuicVersionLabelVectorToString(
    const QuicVersionLabelVector& version_labels,
    const std::string& separator,
    size_t skip_after_nth_version);

// Returns comma separated list of string representations of QuicVersionLabel
// values in the supplied |version_labels| vector.
QUIC_EXPORT_PRIVATE inline std::string QuicVersionLabelVectorToString(
    const QuicVersionLabelVector& version_labels) {
  return QuicVersionLabelVectorToString(version_labels, ",",
                                        std::numeric_limits<size_t>::max());
}

// Returns appropriate QuicTransportVersion from a QuicVersionLabel.
// Returns QUIC_VERSION_UNSUPPORTED if |version_label| cannot be understood.
QUIC_EXPORT_PRIVATE QuicTransportVersion
QuicVersionLabelToQuicVersion(QuicVersionLabel version_label);

// Returns the HandshakeProtocol used with the given |version_label|, returning
// PROTOCOL_UNSUPPORTED if it is unknown.
QUIC_EXPORT_PRIVATE HandshakeProtocol
QuicVersionLabelToHandshakeProtocol(QuicVersionLabel version_label);

// Helper function which translates from a ParsedQuicVersion to a string.
// Returns strings corresponding to the on-the-wire tag.
QUIC_EXPORT_PRIVATE std::string ParsedQuicVersionToString(
    ParsedQuicVersion version);

// Returns comma separated list of string representations of
// QuicTransportVersion enum values in the supplied |versions| vector.
QUIC_EXPORT_PRIVATE std::string QuicTransportVersionVectorToString(
    const QuicTransportVersionVector& versions);

// Returns comma separated list of string representations of ParsedQuicVersion
// values in the supplied |versions| vector.
QUIC_EXPORT_PRIVATE std::string ParsedQuicVersionVectorToString(
    const ParsedQuicVersionVector& versions);

// Returns |separator|-separated list of string representations of
// ParsedQuicVersion values in the supplied |versions| vector. The values after
// the (0-based) |skip_after_nth_version|'th are skipped.
QUIC_EXPORT_PRIVATE std::string ParsedQuicVersionVectorToString(
    const ParsedQuicVersionVector& versions,
    const std::string& separator,
    size_t skip_after_nth_version);

// Returns comma separated list of string representations of ParsedQuicVersion
// values in the supplied |versions| vector.
QUIC_EXPORT_PRIVATE inline std::string ParsedQuicVersionVectorToString(
    const ParsedQuicVersionVector& versions) {
  return ParsedQuicVersionVectorToString(versions, ",",
                                         std::numeric_limits<size_t>::max());
}

// Returns true if |transport_version| uses IETF invariant headers.
QUIC_EXPORT_PRIVATE constexpr bool VersionHasIetfInvariantHeader(
    QuicTransportVersion transport_version) {
  return transport_version > QUIC_VERSION_43;
}

// Returns true if |transport_version| supports MESSAGE frames.
QUIC_EXPORT_PRIVATE constexpr bool VersionSupportsMessageFrames(
    QuicTransportVersion transport_version) {
  return transport_version >= QUIC_VERSION_46;
}

// If true, HTTP/3 instead of gQUIC will be used at the HTTP layer.
// Notable changes are:
// * Headers stream no longer exists.
// * PRIORITY, HEADERS are moved from headers stream to HTTP/3 control stream.
// * PUSH_PROMISE is moved to request stream.
// * Unidirectional streams will have their first byte as a stream type.
// * HEADERS frames are compressed using QPACK.
// * DATA frame has frame headers.
// * GOAWAY is moved to HTTP layer.
QUIC_EXPORT_PRIVATE constexpr bool VersionUsesHttp3(
    QuicTransportVersion transport_version) {
  return transport_version >= QUIC_VERSION_IETF_DRAFT_25;
}

// Returns whether the transport_version supports the variable length integer
// length field as defined by IETF QUIC draft-13 and later.
QUIC_EXPORT_PRIVATE constexpr bool QuicVersionHasLongHeaderLengths(
    QuicTransportVersion transport_version) {
  return transport_version >= QUIC_VERSION_49;
}

// Returns whether |transport_version| makes use of IETF QUIC
// frames or not.
QUIC_EXPORT_PRIVATE constexpr bool VersionHasIetfQuicFrames(
    QuicTransportVersion transport_version) {
  return transport_version >= QUIC_VERSION_IETF_DRAFT_25;
}

// Returns whether this version supports long header 8-bit encoded
// connection ID lengths as described in draft-ietf-quic-invariants-06 and
// draft-ietf-quic-transport-22.
QUIC_EXPORT_PRIVATE bool VersionHasLengthPrefixedConnectionIds(
    QuicTransportVersion transport_version);

// Returns true if this version supports the old Google-style Alt-Svc
// advertisement format.
QUIC_EXPORT_PRIVATE bool VersionSupportsGoogleAltSvcFormat(
    QuicTransportVersion transport_version);

// Returns whether this version allows server connection ID lengths that are
// not 64 bits.
QUIC_EXPORT_PRIVATE bool VersionAllowsVariableLengthConnectionIds(
    QuicTransportVersion transport_version);

// Returns whether this version label supports long header 4-bit encoded
// connection ID lengths as described in draft-ietf-quic-invariants-05 and
// draft-ietf-quic-transport-21.
QUIC_EXPORT_PRIVATE bool QuicVersionLabelUses4BitConnectionIdLength(
    QuicVersionLabel version_label);

// Returns the ALPN string to use in TLS for this version of QUIC.
QUIC_EXPORT_PRIVATE std::string AlpnForVersion(
    ParsedQuicVersion parsed_version);

// Initializes support for the provided IETF draft version by setting the
// correct flags.
QUIC_EXPORT_PRIVATE void QuicVersionInitializeSupportForIetfDraft();

// Enables the flags required to support this version of QUIC.
QUIC_EXPORT_PRIVATE void QuicEnableVersion(ParsedQuicVersion parsed_version);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_VERSIONS_H_
