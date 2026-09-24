// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_
#define QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// List of QUIC versions that support MASQUE. Currently restricted to IETF QUIC.
QUICHE_EXPORT ParsedQuicVersionVector MasqueSupportedVersions();

inline constexpr QuicByteCount kMasqueIpPacketBufferSize = 1501;
// Enough for a VLAN tag, but not Stacked VLANs.
inline constexpr QuicByteCount kMasqueEthernetFrameBufferSize = 1523;

// Mode that MASQUE is operating in.
enum class MasqueMode : uint8_t {
  kInvalid = 0,  // Should never be used.
  kOpen = 2,  // Open mode uses the MASQUE HTTP CONNECT-UDP method as documented
  // in <https://www.rfc-editor.org/rfc/rfc9298.html>. This mode allows
  // unauthenticated clients (a more restricted mode will be added to this enum
  // at a later date).
  kConnectIp =
      1,  // ConnectIp mode uses MASQUE HTTP CONNECT-IP as documented in
  // <https://datatracker.ietf.org/doc/html/draft-ietf-masque-connect-ip>. This
  // mode also allows unauthenticated clients.
  kConnectEthernet =
      3,  // ConnectEthernet mode uses MASQUE HTTP CONNECT-ETHERNET.
  // <https://datatracker.ietf.org/doc/draft-asedeno-masque-connect-ethernet/>
  // This mode also allows unauthenticated clients.
  kConnectUdpBind = 4,
  // Bind a UDP socket on the proxy and forward all packets to the client.
  // This mode uses MASQUE HTTP CONNECT-UDP-BIND.
  // We define a custom handler for this. Our default handler simply echoes
  // back the UDP Payload.
  // <https://datatracker.ietf.org/doc/draft-ietf-masque-connect-udp-listen/>
};

QUICHE_EXPORT std::string MasqueModeToString(MasqueMode masque_mode);
QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const MasqueMode& masque_mode);

// Create a TUN interface, with the specified `client_address`. Requires root.
int CreateTunInterface(const QuicIpAddress& client_address, bool server = true);

// Create a TAP interface. Requires root.
int CreateTapInterface();

inline constexpr size_t kConcealedAuthSignatureInputSize = 32;
inline constexpr size_t kConcealedAuthVerificationSize = 16;
inline constexpr size_t kConcealedAuthExporterSize =
    kConcealedAuthSignatureInputSize + kConcealedAuthVerificationSize;
inline constexpr uint16_t kEd25519SignatureScheme = 0x0807;
inline constexpr absl::string_view kConcealedAuthLabel =
    "EXPORTER-HTTP-Concealed-Authentication";

// Returns the signature auth TLS key exporter context.
QUICHE_EXPORT std::string ComputeConcealedAuthContext(
    uint16_t signature_scheme, absl::string_view key_id,
    absl::string_view public_key, absl::string_view scheme,
    absl::string_view host, uint16_t port, absl::string_view realm);

// Returns the data covered by signature auth signatures, computed by
// concatenating a fixed prefix from the specification and the signature input.
QUICHE_EXPORT std::string ConcealedAuthDataCoveredBySignature(
    absl::string_view signature_input);

// Capsule types for draft-ietf-masque-connect-ip-dns-06.
// Provisional values registered in Section 6 (Table 1).
inline constexpr uint64_t kDnsAssignCapsuleType = 0x1ACE79EC;
inline constexpr uint64_t kPref64CapsuleType = 0x274C0FBC;

// Represents a Domain structure (Section 3.1), which carries a domain name
// in format of an IDNA A-label.
struct QUICHE_EXPORT DnsDomain {
  // Fully Qualified Domain Name in DNS presentation format (ASCII/A-label).
  std::string domain_name;

  bool operator==(const DnsDomain& other) const {
    return domain_name == other.domain_name;
  }
};

// Represents a Nameserver structure (Section 3.2), detailing how to reach a
// particular DNS resolver using unencrypted or encrypted transports.
struct QUICHE_EXPORT DnsNameserver {
  // The priority of this nameserver compared to others
  // (Section 2.4.1 of [SVCB]).
  // Note: MUST NOT be set to 0 per Section 3.2 of the CONNECT-IP DNS draft.
  uint16_t service_priority = 0;
  // Sequence of IPv4 addresses that can be used to reach this nameserver.
  std::vector<QuicIpAddress> ipv4_addresses;
  // Sequence of IPv6 addresses that can be used to reach this nameserver.
  std::vector<QuicIpAddress> ipv6_addresses;
  // Domain representing the domain name of the nameserver. This may be empty
  // if the nameserver only supports unencrypted DNS over port 53.
  DnsDomain authentication_domain_name;
  // Set of service parameters applying to this nameserver encoded using the
  // wire format specified in Section 2.2 of [SVCB] (RFC 9460).
  // Use EncodeSvcParams() / DecodeSvcParams() to convert between presentation
  // format and wire format.
  std::string service_parameters;

  bool operator==(const DnsNameserver& other) const {
    return service_priority == other.service_priority &&
           ipv4_addresses == other.ipv4_addresses &&
           ipv6_addresses == other.ipv6_addresses &&
           authentication_domain_name == other.authentication_domain_name &&
           service_parameters == other.service_parameters;
  }
  std::string ToString() const;
};

// Encodes service parameters from DNS presentation format (e.g.,
// "alpn=h2,h3 dohpath=/dns-query{?dns}") to RFC 9460 Section 2.2 SVCB wire
// format. Returns std::nullopt if parsing fails.
QUICHE_EXPORT std::optional<std::string> EncodeSvcParams(
    absl::string_view presentation);

// Decodes service parameters from RFC 9460 Section 2.2 SVCB wire format to
// DNS presentation format. Returns std::nullopt if parsing fails.
QUICHE_EXPORT std::optional<std::string> DecodeSvcParams(
    absl::string_view wire_format);

// Represents a DNS Configuration structure (Section 3.3) describing a set of
// nameservers responsible for resolving specific internal and search domains.
struct QUICHE_EXPORT DnsConfiguration {
  // Series of Nameserver structures representing how to reach the resolvers.
  std::vector<DnsNameserver> nameservers;
  // Series of Domain structures representing internal domain names and their
  // subdomains that the nameservers are authoritative for. An empty string
  // indicates the DNS root (authoritative for all domain names).
  std::vector<DnsDomain> internal_domains;
  // Series of Domain structures representing DNS search domains.
  std::vector<DnsDomain> search_domains;

  bool operator==(const DnsConfiguration& other) const {
    return nameservers == other.nameservers &&
           internal_domains == other.internal_domains &&
           search_domains == other.search_domains;
  }
  std::string ToString() const;
};

// Represents a DNS_ASSIGN capsule (Section 3.4) allowing an endpoint to send
// one or more DNS configurations to its peer over a CONNECT-IP stream.
struct QUICHE_EXPORT DnsAssignCapsule {
  // Multiple DNS configurations may be included if different DNS servers are
  // responsible for separate internal domains.
  std::vector<DnsConfiguration> dns_configurations;

  bool operator==(const DnsAssignCapsule& other) const {
    return dns_configurations == other.dns_configurations;
  }
  std::string ToString() const;
};

// Represents an individual NAT64 prefix (Section 4.1) used for IPv6/IPv4
// address synthesis in IPv6-only environments.
struct QUICHE_EXPORT Pref64Prefix {
  // Length of the NAT64 prefix in bits. Valid values are 32, 40, 48, 56, 64,
  // and 96.
  uint8_t prefix_length = 0;
  // The highest 96 bits (12 bytes) of the IPv6 prefix. Stored as an IPv6
  // address where the trailing 32 bits are zeroed.
  QuicIpAddress prefix;

  bool operator==(const Pref64Prefix& other) const {
    return prefix_length == other.prefix_length && prefix == other.prefix;
  }
  std::string ToString() const;
};

// Represents a PREF64 capsule (Section 4.1) conveying zero or more NAT64
// prefixes. An empty PREF64 capsule informs that NAT64 prefixes are not
// available.
struct QUICHE_EXPORT Pref64Capsule {
  std::vector<Pref64Prefix> nat64_prefixes;

  bool operator==(const Pref64Capsule& other) const {
    return nat64_prefixes == other.nat64_prefixes;
  }
  std::string ToString() const;
};

// Parses the payload of a DNS_ASSIGN capsule into |capsule|.
// Returns true if parsing succeeds, false otherwise (e.g., malformed format,
// priority is 0, or trailing bytes remain).
QUICHE_EXPORT bool ParseDnsAssignCapsulePayload(absl::string_view payload,
                                                DnsAssignCapsule* capsule);

// Parses the payload of a PREF64 capsule into |capsule|.
// Returns true on success, false if malformed (e.g., payload length is not a
// multiple of 13 bytes or prefix_length is invalid).
QUICHE_EXPORT bool ParsePref64CapsulePayload(absl::string_view payload,
                                             Pref64Capsule* capsule);

// Serializes the payload of |capsule| into wire format bytes for a DNS_ASSIGN
// capsule.
QUICHE_EXPORT std::string SerializeDnsAssignCapsulePayload(
    const DnsAssignCapsule& capsule);

// Serializes the payload of |capsule| into wire format bytes for a PREF64
// capsule.
QUICHE_EXPORT std::string SerializePref64CapsulePayload(
    const Pref64Capsule& capsule);

// Serializes and writes a DNS_ASSIGN capsule to |stream|. |fin| indicates
// whether to close the stream write side after sending.
QUICHE_EXPORT void WriteDnsAssignCapsule(QuicSpdyStream* stream,
                                         const DnsAssignCapsule& capsule,
                                         bool fin = false);

// Serializes and writes a PREF64 capsule to |stream|. |fin| indicates whether
// to close the stream write side after sending.
QUICHE_EXPORT void WritePref64Capsule(QuicSpdyStream* stream,
                                      const Pref64Capsule& capsule,
                                      bool fin = false);

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_
