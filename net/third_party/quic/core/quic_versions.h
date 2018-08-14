// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_VERSIONS_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_VERSIONS_H_

#include <vector>

#include "net/third_party/quic/core/quic_tag.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

// The available versions of QUIC. Guaranteed that the integer value of the enum
// will match the version number.
// When adding a new version to this enum you should add it to
// kSupportedTransportVersions (if appropriate), and also add a new case to the
// helper methods QuicVersionToQuicVersionLabel, QuicVersionLabelToQuicVersion,
// and QuicVersionToString.
enum QuicTransportVersion {
  // Special case to indicate unknown/unsupported QUIC version.
  QUIC_VERSION_UNSUPPORTED = 0,

  QUIC_VERSION_35 = 35,  // Allows endpoints to independently set stream limit.
  QUIC_VERSION_39 = 39,  // Integers and floating numbers are written in big
                         // endian. Dot not ack acks. Send a connection level
                         // WINDOW_UPDATE every 20 sent packets which do not
                         // contain retransmittable frames.
  QUIC_VERSION_41 = 41,  // RST_STREAM, ACK and STREAM frames match IETF format.
  QUIC_VERSION_42 = 42,  // Allows receiving overlapping stream data.
  QUIC_VERSION_43 = 43,  // PRIORITY frames are sent by client and accepted by
                         // server.
  QUIC_VERSION_44 = 44,  // Use IETF header format.
  QUIC_VERSION_99 = 99,  // Dumping ground for IETF QUIC changes which are not
                         // yet ready for production.

  // IMPORTANT: if you are adding to this list, follow the instructions at
  // http://sites/quic/adding-and-removing-versions
};

// The crypto handshake protocols that can be used with QUIC.
enum HandshakeProtocol {
  PROTOCOL_UNSUPPORTED,
  PROTOCOL_QUIC_CRYPTO,
  PROTOCOL_TLS1_3,
};

// A parsed QUIC version label which determines that handshake protocol
// and the transport version.
struct QUIC_EXPORT_PRIVATE ParsedQuicVersion {
  HandshakeProtocol handshake_protocol;
  QuicTransportVersion transport_version;

  ParsedQuicVersion(HandshakeProtocol handshake_protocol,
                    QuicTransportVersion transport_version);

  ParsedQuicVersion(const ParsedQuicVersion& other)
      : handshake_protocol(other.handshake_protocol),
        transport_version(other.transport_version) {}

  ParsedQuicVersion& operator=(const ParsedQuicVersion& other) {
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
};

QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                             const ParsedQuicVersion& version);

using ParsedQuicVersionVector = std::vector<ParsedQuicVersion>;

// Representation of the on-the-wire QUIC version number. Will be written/read
// to the wire in network-byte-order.
using QuicVersionLabel = uint32_t;
using QuicVersionLabelVector = std::vector<QuicVersionLabel>;

// This vector contains QUIC versions which we currently support.
// This should be ordered such that the highest supported version is the first
// element, with subsequent elements in descending order (versions can be
// skipped as necessary).
//
// IMPORTANT: if you are adding to this list, follow the instructions at
// http://sites/quic/adding-and-removing-versions
static const QuicTransportVersion kSupportedTransportVersions[] = {
    QUIC_VERSION_99, QUIC_VERSION_44, QUIC_VERSION_43, QUIC_VERSION_42,
    QUIC_VERSION_41, QUIC_VERSION_39, QUIC_VERSION_35};

// This vector contains all crypto handshake protocols that are supported.
static const HandshakeProtocol kSupportedHandshakeProtocols[] = {
    PROTOCOL_QUIC_CRYPTO, PROTOCOL_TLS1_3};

typedef std::vector<QuicTransportVersion> QuicTransportVersionVector;

// Returns a vector of QUIC versions in kSupportedTransportVersions.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector AllSupportedTransportVersions();

// Returns a vector of QUIC versions that is the cartesian product of
// kSupportedTransportVersions and kSupportedHandshakeProtocols.
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector AllSupportedVersions();

// Returns a vector of QUIC versions from kSupportedTransportVersions which
// exclude any versions which are disabled by flags.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector
CurrentSupportedTransportVersions();

// Returns a vector of QUIC versions that is the cartesian product of
// kSupportedTransportVersions and kSupportedHandshakeProtocols, with any
// versions disabled by flags excluded.
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector CurrentSupportedVersions();

// Returns a vector of QUIC versions from |versions| which exclude any versions
// which are disabled by flags.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector
FilterSupportedTransportVersions(QuicTransportVersionVector versions);

// Returns a vector of QUIC versions from |versions| which exclude any versions
// which are disabled by flags.
QUIC_EXPORT_PRIVATE ParsedQuicVersionVector
FilterSupportedVersions(ParsedQuicVersionVector versions);

// Returns QUIC version of |index| in result of |versions|. Returns
// QUIC_VERSION_UNSUPPORTED if |index| is out of bounds.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector
VersionOfIndex(const QuicTransportVersionVector& versions, int index);

// Returns QUIC version of |index| in result of |versions|. Returns
// ParsedQuicVersion(PROTOCOL_UNSUPPORTED, QUIC_VERSION_UNSUPPORTED) if |index|
// is out of bounds.
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
QUIC_EXPORT_PRIVATE QuicString
QuicVersionLabelToString(QuicVersionLabel version_label);

// Returns |separator|-separated list of string representations of
// QuicVersionLabel values in the supplied |version_labels| vector. The values
// after the (0-based) |skip_after_nth_version|'th are skipped.
QUIC_EXPORT_PRIVATE QuicString
QuicVersionLabelVectorToString(const QuicVersionLabelVector& version_labels,
                               const QuicString& separator,
                               size_t skip_after_nth_version);

// Returns comma separated list of string representations of QuicVersionLabel
// values in the supplied |version_labels| vector.
QUIC_EXPORT_PRIVATE inline QuicString QuicVersionLabelVectorToString(
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

// Helper function which translates from a QuicTransportVersion to a string.
// Returns strings corresponding to enum names (e.g. QUIC_VERSION_6).
QUIC_EXPORT_PRIVATE QuicString
QuicVersionToString(QuicTransportVersion transport_version);

// Helper function which translates from a ParsedQuicVersion to a string.
// Returns strings corresponding to the on-the-wire tag.
QUIC_EXPORT_PRIVATE QuicString
ParsedQuicVersionToString(ParsedQuicVersion version);

// Returns comma separated list of string representations of
// QuicTransportVersion enum values in the supplied |versions| vector.
QUIC_EXPORT_PRIVATE QuicString
QuicTransportVersionVectorToString(const QuicTransportVersionVector& versions);

// Returns comma separated list of string representations of ParsedQuicVersion
// values in the supplied |versions| vector.
QUIC_EXPORT_PRIVATE QuicString
ParsedQuicVersionVectorToString(const ParsedQuicVersionVector& versions);

// Returns |separator|-separated list of string representations of
// ParsedQuicVersion values in the supplied |versions| vector. The values after
// the (0-based) |skip_after_nth_version|'th are skipped.
QUIC_EXPORT_PRIVATE QuicString
ParsedQuicVersionVectorToString(const ParsedQuicVersionVector& versions,
                                const QuicString& separator,
                                size_t skip_after_nth_version);

// Returns comma separated list of string representations of ParsedQuicVersion
// values in the supplied |versions| vector.
QUIC_EXPORT_PRIVATE inline QuicString ParsedQuicVersionVectorToString(
    const ParsedQuicVersionVector& versions) {
  return ParsedQuicVersionVectorToString(versions, ",",
                                         std::numeric_limits<size_t>::max());
}

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_VERSIONS_H_
