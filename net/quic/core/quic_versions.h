// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_VERSIONS_H_
#define NET_QUIC_CORE_QUIC_VERSIONS_H_

#include <string>
#include <vector>

#include "net/quic/core/quic_tag.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

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
  QUIC_VERSION_37 = 37,  // Add perspective into null encryption.
  QUIC_VERSION_38 = 38,  // PADDING frame is a 1-byte frame with type 0x00.
                         // Respect NSTP connection option.
  QUIC_VERSION_39 = 39,  // Integers and floating numbers are written in big
                         // endian. Dot not ack acks. Send a connection level
                         // WINDOW_UPDATE every 20 sent packets which do not
                         // contain retransmittable frames.
  QUIC_VERSION_41 = 41,  // RST_STREAM, ACK and STREAM frames match IETF format.
  QUIC_VERSION_42 = 42,  // Use IETF packet header format.

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
struct ParsedQuicVersion {
  HandshakeProtocol handshake_protocol;
  QuicTransportVersion transport_version;

  ParsedQuicVersion(HandshakeProtocol handshake_protocol,
                    QuicTransportVersion transport_version)
      : handshake_protocol(handshake_protocol),
        transport_version(transport_version) {}

  bool operator==(const ParsedQuicVersion& other) const {
    return handshake_protocol == other.handshake_protocol &&
           transport_version == other.transport_version;
  }
};

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
    QUIC_VERSION_42, QUIC_VERSION_41, QUIC_VERSION_39,
    QUIC_VERSION_38, QUIC_VERSION_37, QUIC_VERSION_35};

typedef std::vector<QuicTransportVersion> QuicTransportVersionVector;

// Returns a vector of QUIC versions in kSupportedTransportVersions.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector AllSupportedTransportVersions();

// Returns a vector of QUIC versions from kSupportedTransportVersions which
// exclude any versions which are disabled by flags.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector
CurrentSupportedTransportVersions();

// Returns a vector of QUIC versions from |versions| which exclude any versions
// which are disabled by flags.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector
FilterSupportedTransportVersions(QuicTransportVersionVector versions);

// Returns QUIC version of |index| in result of |versions|. Returns
// QUIC_VERSION_UNSUPPORTED if |index| is out of bounds.
QUIC_EXPORT_PRIVATE QuicTransportVersionVector
VersionOfIndex(const QuicTransportVersionVector& versions, int index);

// QuicVersionLabel is written to and read from the wire, but we prefer to use
// the more readable ParsedQuicVersion at other levels.
// Helper function which translates from a QuicVersionLabel to a
// ParsedQuicVersion.
QUIC_EXPORT_PRIVATE ParsedQuicVersion
ParseQuicVersionLabel(QuicVersionLabel version_label);

// Constructs a QuicVersionLabel from the provided ParsedQuicVersion.
QUIC_EXPORT_PRIVATE QuicVersionLabel
CreateQuicVersionLabel(ParsedQuicVersion parsed_version);

// QuicVersionLabel is written to and read from the wire, but we prefer to use
// the more readable QuicTransportVersion at other levels.
// Helper function which translates from a QuicTransportVersion to a
// QuicVersionLabel. Returns 0 if |version| is unsupported.
QUIC_EXPORT_PRIVATE QuicVersionLabel
QuicVersionToQuicVersionLabel(QuicTransportVersion transport_version);

// Helper function which translates from a QuicVersionLabel to a std::string.
QUIC_EXPORT_PRIVATE std::string QuicVersionLabelToString(
    QuicVersionLabel version_label);

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
QUIC_EXPORT_PRIVATE std::string QuicVersionToString(
    QuicTransportVersion transport_version);

// Returns comma separated list of string representations of QuicVersion enum
// values in the supplied |versions| vector.
QUIC_EXPORT_PRIVATE std::string QuicTransportVersionVectorToString(
    const QuicTransportVersionVector& versions);

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_VERSIONS_H_
