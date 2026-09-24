// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_utils.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/capsule.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"

#if defined(__linux__)
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#endif  // defined(__linux__)

#include "absl/cleanup/cleanup.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, tap_bridge_interface, "",
    "Bridge tap interfaces created by CONNECT-ETHERNET mode to be bridged to "
    "the specified interface, if any.");

namespace quic {

ParsedQuicVersionVector MasqueSupportedVersions() {
  ParsedQuicVersionVector versions;
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    // Use all versions that support IETF QUIC except QUICv2.
    if (version.IsIetfQuic() && !version.AlpnDeferToRFCv1()) {
      QuicEnableVersion(version);
      versions.push_back(version);
    }
  }
  QUICHE_CHECK(!versions.empty());
  return versions;
}

std::string MasqueModeToString(MasqueMode masque_mode) {
  switch (masque_mode) {
    case MasqueMode::kInvalid:
      return "Invalid";
    case MasqueMode::kOpen:
      return "Open";
    case MasqueMode::kConnectIp:
      return "CONNECT-IP";
    case MasqueMode::kConnectEthernet:
      return "CONNECT-ETHERNET";
    case MasqueMode::kConnectUdpBind:
      return "CONNECT-UDP-BIND";
  }
  return absl::StrCat("Unknown(", static_cast<int>(masque_mode), ")");
}

std::ostream& operator<<(std::ostream& os, const MasqueMode& masque_mode) {
  os << MasqueModeToString(masque_mode);
  return os;
}

#if defined(__linux__)
int CreateTunInterface(const QuicIpAddress& client_address, bool server) {
  if (!client_address.IsIPv4()) {
    QUIC_LOG(ERROR) << "CreateTunInterface currently only supports IPv4";
    return -1;
  }
  // TODO(b/281517862): add test to validate O_NONBLOCK
  int tun_fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
  if (tun_fd < 0) {
    QUIC_PLOG(ERROR) << "Failed to open clone device";
    return -1;
  }
  absl::Cleanup tun_fd_closer = [tun_fd] { close(tun_fd); };

  struct ifreq ifr = {};
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  // If we want to pick a specific device name, we can set it via
  // ifr.ifr_name. Otherwise, the kernel will pick the next available tunX
  // name.
  int err = ioctl(tun_fd, TUNSETIFF, &ifr);
  if (err < 0) {
    QUIC_PLOG(ERROR) << "TUNSETIFF failed";
    return -1;
  }
  int ip_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (ip_fd < 0) {
    QUIC_PLOG(ERROR) << "Failed to open IP configuration socket";
    return -1;
  }
  absl::Cleanup ip_fd_closer = [ip_fd] { close(ip_fd); };

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  // Local address, unused but needs to be set. We use the same address as the
  // client address, but with last byte set to 1.
  addr.sin_addr = client_address.GetIPv4();
  if (server) {
    addr.sin_addr.s_addr &= htonl(0xffffff00);
    addr.sin_addr.s_addr |= htonl(0x00000001);
  }
  memcpy(&ifr.ifr_addr, &addr, sizeof(addr));
  err = ioctl(ip_fd, SIOCSIFADDR, &ifr);
  if (err < 0) {
    QUIC_PLOG(ERROR) << "SIOCSIFADDR failed";
    return -1;
  }
  // Peer address, needs to match source IP address of sent packets.
  addr.sin_addr = client_address.GetIPv4();
  if (!server) {
    addr.sin_addr.s_addr &= htonl(0xffffff00);
    addr.sin_addr.s_addr |= htonl(0x00000001);
  }
  memcpy(&ifr.ifr_addr, &addr, sizeof(addr));
  err = ioctl(ip_fd, SIOCSIFDSTADDR, &ifr);
  if (err < 0) {
    QUIC_PLOG(ERROR) << "SIOCSIFDSTADDR failed";
    return -1;
  }
  if (!server) {
    // Set MTU, to 1280 for now which should always fit (fingers crossed)
    ifr.ifr_mtu = 1280;
    err = ioctl(ip_fd, SIOCSIFMTU, &ifr);
    if (err < 0) {
      QUIC_PLOG(ERROR) << "SIOCSIFMTU failed";
      return -1;
    }
  }

  err = ioctl(ip_fd, SIOCGIFFLAGS, &ifr);
  if (err < 0) {
    QUIC_PLOG(ERROR) << "SIOCGIFFLAGS failed";
    return -1;
  }
  ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
  err = ioctl(ip_fd, SIOCSIFFLAGS, &ifr);
  if (err < 0) {
    QUIC_PLOG(ERROR) << "SIOCSIFFLAGS failed";
    return -1;
  }
  close(ip_fd);
  QUIC_DLOG(INFO) << "Successfully created TUN interface " << ifr.ifr_name
                  << " with fd " << tun_fd;
  std::move(tun_fd_closer).Cancel();
  return tun_fd;
}
#else
int CreateTunInterface(const QuicIpAddress& /*client_address*/,
                       bool /*server*/) {
  // Unsupported.
  return -1;
}
#endif  // defined(__linux__)

#if defined(__linux__)
int CreateTapInterface() {
  int tap_fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
  if (tap_fd < 0) {
    QUIC_PLOG(ERROR) << "Failed to open clone device";
    return -1;
  }
  absl::Cleanup tap_fd_closer = [tap_fd] { close(tap_fd); };

  struct ifreq ifr = {};
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
  // If we want to pick a specific device name, we can set it via
  // ifr.ifr_name. Otherwise, the kernel will pick the next available tapX
  // name.
  int err = ioctl(tap_fd, TUNSETIFF, &ifr);
  if (err < 0) {
    QUIC_PLOG(ERROR) << "TUNSETIFF failed";
    return -1;
  }

  QUIC_DLOG(INFO) << "Successfully created TAP interface " << ifr.ifr_name
                  << " with fd " << tap_fd;

  int sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    QUIC_PLOG(ERROR) << "Error opening configuration socket";
    return -1;
  }
  absl::Cleanup sock_fd_closer = [sock_fd] { close(sock_fd); };

  err = ioctl(sock_fd, SIOCGIFINDEX, &ifr);
  if (err < 0) {
    QUIC_PLOG(ERROR) << "SIOCGIFINDEX failed";
  }
  int tap_ifindex = ifr.ifr_ifindex;

  ifr.ifr_mtu = 1280;
  err = ioctl(sock_fd, SIOCSIFMTU, &ifr);
  if (err < 0) {
    QUIC_PLOG(ERROR) << "SIOCSIFMTU failed";
    return -1;
  }

  err = ioctl(sock_fd, SIOCGIFFLAGS, &ifr);
  if (err < 0) {
    QUIC_PLOG(ERROR) << "SIOCGIFFLAGS failed";
    return -1;
  }
  ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
  err = ioctl(sock_fd, SIOCSIFFLAGS, &ifr);
  if (err < 0) {
    QUIC_PLOG(ERROR) << "SIOCSIFFLAGS failed";
    return -1;
  }

  const std::string tap_bridge_interface =
      quiche::GetQuicheCommandLineFlag(FLAGS_tap_bridge_interface);

  if (!tap_bridge_interface.empty()) {
    if (tap_bridge_interface.size() >= IFNAMSIZ) {
      QUIC_LOG(ERROR) << "tap bridge interface size too long: "
                      << tap_bridge_interface.size();
      return -1;
    }
    strncpy(ifr.ifr_name, tap_bridge_interface.c_str(), IFNAMSIZ);
    ifr.ifr_ifindex = tap_ifindex;
    err = ioctl(sock_fd, SIOCBRADDIF, &ifr);
    if (err < 0) {
      QUIC_PLOG(ERROR) << "SIOCBRADDIF failed";
      return -1;
    }
  }

  std::move(tap_fd_closer).Cancel();
  return tap_fd;
}
#else
int CreateTapInterface() {
  // Unsupported.
  return -1;
}
#endif  // defined(__linux__)

std::string ComputeConcealedAuthContext(uint16_t signature_scheme,
                                        absl::string_view key_id,
                                        absl::string_view public_key,
                                        absl::string_view scheme,
                                        absl::string_view host, uint16_t port,
                                        absl::string_view realm) {
  QUIC_DVLOG(2) << "ComputeConcealedAuthContext: key_id=\"" << key_id
                << "\" public_key=" << absl::WebSafeBase64Escape(public_key)
                << " scheme=\"" << scheme << "\" host=\"" << host
                << "\" port=" << port << " realm=\"" << realm << "\"";
  std::string key_exporter_context;
  key_exporter_context.resize(
      sizeof(signature_scheme) + QuicDataWriter::GetVarInt62Len(key_id.size()) +
      key_id.size() + QuicDataWriter::GetVarInt62Len(public_key.size()) +
      public_key.size() + QuicDataWriter::GetVarInt62Len(scheme.size()) +
      scheme.size() + QuicDataWriter::GetVarInt62Len(host.size()) +
      host.size() + sizeof(port) +
      QuicDataWriter::GetVarInt62Len(realm.size()) + realm.size());
  QuicDataWriter writer(key_exporter_context.size(),
                        key_exporter_context.data());
  if (!writer.WriteUInt16(signature_scheme) ||
      !writer.WriteStringPieceVarInt62(key_id) ||
      !writer.WriteStringPieceVarInt62(public_key) ||
      !writer.WriteStringPieceVarInt62(scheme) ||
      !writer.WriteStringPieceVarInt62(host) || !writer.WriteUInt16(port) ||
      !writer.WriteStringPieceVarInt62(realm) || writer.remaining() != 0) {
    QUIC_LOG(FATAL) << "ComputeConcealedAuthContext failed";
  }
  return key_exporter_context;
}

std::string ConcealedAuthDataCoveredBySignature(
    absl::string_view signature_input) {
  return absl::StrCat(std::string(64, 0x20), "HTTP Concealed Authentication",
                      std::string(1, 0x00), signature_input);
}

namespace {

bool ParseDnsDomain(QuicDataReader& reader, DnsDomain* domain) {
  if (domain == nullptr) {
    return false;
  }
  absl::string_view piece;
  if (!reader.ReadStringPieceVarInt62(&piece)) {
    return false;
  }
  domain->domain_name = std::string(piece);
  return true;
}

size_t ComputeDnsDomainLength(const DnsDomain& domain) {
  return QuicDataWriter::GetVarInt62Len(domain.domain_name.size()) +
         domain.domain_name.size();
}

size_t ComputeDnsNameserverLength(const DnsNameserver& ns) {
  size_t total = sizeof(ns.service_priority);
  total += QuicDataWriter::GetVarInt62Len(ns.ipv4_addresses.size());
  total += ns.ipv4_addresses.size() * QuicIpAddress::kIPv4AddressSize;
  total += QuicDataWriter::GetVarInt62Len(ns.ipv6_addresses.size());
  total += ns.ipv6_addresses.size() * QuicIpAddress::kIPv6AddressSize;
  total += ComputeDnsDomainLength(ns.authentication_domain_name);
  total += QuicDataWriter::GetVarInt62Len(ns.service_parameters.size());
  total += ns.service_parameters.size();
  return total;
}

size_t ComputeDnsConfigurationLength(const DnsConfiguration& config) {
  size_t total = QuicDataWriter::GetVarInt62Len(config.nameservers.size());
  for (const DnsNameserver& ns : config.nameservers) {
    total += ComputeDnsNameserverLength(ns);
  }
  total += QuicDataWriter::GetVarInt62Len(config.internal_domains.size());
  for (const DnsDomain& domain : config.internal_domains) {
    total += ComputeDnsDomainLength(domain);
  }
  total += QuicDataWriter::GetVarInt62Len(config.search_domains.size());
  for (const DnsDomain& domain : config.search_domains) {
    total += ComputeDnsDomainLength(domain);
  }
  return total;
}

}  // namespace

bool ParseDnsAssignCapsulePayload(absl::string_view payload,
                                  DnsAssignCapsule* capsule) {
  if (capsule == nullptr) {
    return false;
  }
  capsule->dns_configurations.clear();
  QuicDataReader reader(payload);
  while (!reader.IsDoneReading()) {
    DnsConfiguration config;
    uint64_t nameserver_count = 0;
    if (!reader.ReadVarInt62(&nameserver_count)) {
      return false;
    }
    for (uint64_t i = 0; i < nameserver_count; ++i) {
      DnsNameserver ns;
      if (!reader.ReadUInt16(&ns.service_priority)) {
        return false;
      }
      if (ns.service_priority == 0) {
        return false;
      }
      uint64_t ipv4_count = 0;
      if (!reader.ReadVarInt62(&ipv4_count)) {
        return false;
      }
      for (uint64_t j = 0; j < ipv4_count; ++j) {
        absl::string_view ip_bytes;
        if (!reader.ReadStringPiece(&ip_bytes,
                                    QuicIpAddress::kIPv4AddressSize)) {
          return false;
        }
        QuicIpAddress ip;
        if (!ip.FromPackedString(ip_bytes.data(),
                                 QuicIpAddress::kIPv4AddressSize)) {
          return false;
        }
        ns.ipv4_addresses.push_back(ip);
      }
      uint64_t ipv6_count = 0;
      if (!reader.ReadVarInt62(&ipv6_count)) {
        return false;
      }
      for (uint64_t j = 0; j < ipv6_count; ++j) {
        absl::string_view ip_bytes;
        if (!reader.ReadStringPiece(&ip_bytes,
                                    QuicIpAddress::kIPv6AddressSize)) {
          return false;
        }
        QuicIpAddress ip;
        if (!ip.FromPackedString(ip_bytes.data(),
                                 QuicIpAddress::kIPv6AddressSize)) {
          return false;
        }
        ns.ipv6_addresses.push_back(ip);
      }
      if (!ParseDnsDomain(reader, &ns.authentication_domain_name)) {
        return false;
      }
      absl::string_view sp_piece;
      if (!reader.ReadStringPieceVarInt62(&sp_piece)) {
        return false;
      }
      if (!sp_piece.empty() && !DecodeSvcParams(sp_piece).has_value()) {
        return false;
      }
      ns.service_parameters = std::string(sp_piece);
      config.nameservers.push_back(std::move(ns));
    }

    uint64_t internal_domain_count = 0;
    if (!reader.ReadVarInt62(&internal_domain_count)) {
      return false;
    }
    for (uint64_t i = 0; i < internal_domain_count; ++i) {
      DnsDomain domain;
      if (!ParseDnsDomain(reader, &domain)) {
        return false;
      }
      config.internal_domains.push_back(std::move(domain));
    }

    uint64_t search_domain_count = 0;
    if (!reader.ReadVarInt62(&search_domain_count)) {
      return false;
    }
    for (uint64_t i = 0; i < search_domain_count; ++i) {
      DnsDomain domain;
      if (!ParseDnsDomain(reader, &domain)) {
        return false;
      }
      config.search_domains.push_back(std::move(domain));
    }

    capsule->dns_configurations.push_back(std::move(config));
  }
  return true;
}

bool ParsePref64CapsulePayload(absl::string_view payload,
                               Pref64Capsule* capsule) {
  if (capsule == nullptr) {
    return false;
  }
  capsule->nat64_prefixes.clear();
  if (payload.size() % 13 != 0) {
    return false;
  }
  QuicDataReader reader(payload);
  while (!reader.IsDoneReading()) {
    Pref64Prefix prefix;
    if (!reader.ReadUInt8(&prefix.prefix_length)) {
      return false;
    }
    if (prefix.prefix_length != 32 && prefix.prefix_length != 40 &&
        prefix.prefix_length != 48 && prefix.prefix_length != 56 &&
        prefix.prefix_length != 64 && prefix.prefix_length != 96) {
      return false;
    }
    absl::string_view prefix_bytes;
    if (!reader.ReadStringPiece(&prefix_bytes, 12)) {
      return false;
    }
    char full_ipv6_bytes[16] = {};
    memcpy(full_ipv6_bytes, prefix_bytes.data(), 12);
    if (!prefix.prefix.FromPackedString(full_ipv6_bytes, 16)) {
      return false;
    }
    capsule->nat64_prefixes.push_back(std::move(prefix));
  }
  return true;
}

std::string SerializeDnsAssignCapsulePayload(const DnsAssignCapsule& capsule) {
  size_t total_len = 0;
  for (const DnsConfiguration& config : capsule.dns_configurations) {
    total_len += ComputeDnsConfigurationLength(config);
  }
  std::string buffer(total_len, '\0');
  QuicDataWriter writer(buffer.size(), buffer.data());
  for (const DnsConfiguration& config : capsule.dns_configurations) {
    if (!writer.WriteVarInt62(config.nameservers.size())) {
      QUIC_BUG(dns_assign_serial_error) << "Failed writing nameservers count";
      return "";
    }
    for (const DnsNameserver& ns : config.nameservers) {
      if (!writer.WriteUInt16(ns.service_priority) ||
          !writer.WriteVarInt62(ns.ipv4_addresses.size())) {
        QUIC_BUG(dns_assign_serial_error)
            << "Failed writing priority or ipv4 count";
        return "";
      }
      for (const QuicIpAddress& ip : ns.ipv4_addresses) {
        if (!ip.IsIPv4()) {
          QUIC_BUG(dns_assign_serial_error) << "Not an IPv4 address";
          return "";
        }
        std::string packed = ip.ToPackedString();
        if (packed.size() != QuicIpAddress::kIPv4AddressSize ||
            !writer.WriteStringPiece(packed)) {
          QUIC_BUG(dns_assign_serial_error) << "Failed writing ipv4";
          return "";
        }
      }
      if (!writer.WriteVarInt62(ns.ipv6_addresses.size())) {
        QUIC_BUG(dns_assign_serial_error) << "Failed writing ipv6 count";
        return "";
      }
      for (const QuicIpAddress& ip : ns.ipv6_addresses) {
        if (!ip.IsIPv6()) {
          QUIC_BUG(dns_assign_serial_error) << "Not an IPv6 address";
          return "";
        }
        std::string packed = ip.ToPackedString();
        if (packed.size() != QuicIpAddress::kIPv6AddressSize ||
            !writer.WriteStringPiece(packed)) {
          QUIC_BUG(dns_assign_serial_error) << "Failed writing ipv6";
          return "";
        }
      }
      if (!ns.service_parameters.empty() &&
          !DecodeSvcParams(ns.service_parameters).has_value()) {
        QUIC_BUG(dns_assign_serial_error)
            << "Invalid service parameters wire format";
        return "";
      }
      if (!writer.WriteStringPieceVarInt62(
              ns.authentication_domain_name.domain_name) ||
          !writer.WriteStringPieceVarInt62(ns.service_parameters)) {
        QUIC_BUG(dns_assign_serial_error)
            << "Failed writing domain name / service params";
        return "";
      }
    }
    if (!writer.WriteVarInt62(config.internal_domains.size())) {
      QUIC_BUG(dns_assign_serial_error)
          << "Failed writing internal domains count";
      return "";
    }
    for (const DnsDomain& domain : config.internal_domains) {
      if (!writer.WriteStringPieceVarInt62(domain.domain_name)) {
        QUIC_BUG(dns_assign_serial_error) << "Failed writing internal domain";
        return "";
      }
    }
    if (!writer.WriteVarInt62(config.search_domains.size())) {
      QUIC_BUG(dns_assign_serial_error)
          << "Failed writing search domains count";
      return "";
    }
    for (const DnsDomain& domain : config.search_domains) {
      if (!writer.WriteStringPieceVarInt62(domain.domain_name)) {
        QUIC_BUG(dns_assign_serial_error) << "Failed writing search domain";
        return "";
      }
    }
  }
  if (writer.remaining() != 0) {
    QUIC_BUG(dns_assign_serial_error)
        << "Remaining length after serialization: " << writer.remaining();
    return "";
  }
  return buffer;
}

std::string SerializePref64CapsulePayload(const Pref64Capsule& capsule) {
  size_t total_len = capsule.nat64_prefixes.size() * 13;
  std::string buffer(total_len, '\0');
  QuicDataWriter writer(buffer.size(), buffer.data());
  for (const Pref64Prefix& prefix : capsule.nat64_prefixes) {
    if (!writer.WriteUInt8(prefix.prefix_length) || !prefix.prefix.IsIPv6()) {
      QUIC_BUG(pref64_serial_error)
          << "Failed writing prefix length / not ipv6";
      return "";
    }
    std::string packed = prefix.prefix.ToPackedString();
    if (packed.size() != 16 ||
        !writer.WriteStringPiece(absl::string_view(packed.data(), 12))) {
      QUIC_BUG(pref64_serial_error) << "Failed writing prefix data";
      return "";
    }
  }
  if (writer.remaining() != 0) {
    QUIC_BUG(pref64_serial_error)
        << "Remaining length after serialization: " << writer.remaining();
    return "";
  }
  return buffer;
}

namespace {
constexpr uint16_t kSvcParamKeyAlpn = 1;
constexpr uint16_t kSvcParamKeyNoDefaultAlpn = 2;
constexpr uint16_t kSvcParamKeyPort = 3;
constexpr uint16_t kSvcParamKeyIpv4Hint = 4;
constexpr uint16_t kSvcParamKeyIpv6Hint = 6;
constexpr uint16_t kSvcParamKeyDohPath = 7;
constexpr uint16_t kSvcParamKeyOhttp = 8;
}  // namespace

std::optional<std::string> EncodeSvcParams(absl::string_view presentation) {
  absl::btree_map<uint16_t, std::string> params;
  for (absl::string_view token : absl::StrSplit(
           presentation, absl::ByAnyChar(" \t\r\n"), absl::SkipWhitespace())) {
    size_t equals_pos = token.find('=');
    absl::string_view key_str = token.substr(0, equals_pos);
    absl::string_view value_str = (equals_pos == absl::string_view::npos)
                                      ? ""
                                      : token.substr(equals_pos + 1);
    uint16_t key = 0;
    std::string wire_val;
    if (key_str == "alpn") {
      key = kSvcParamKeyAlpn;
      for (absl::string_view alpn : absl::StrSplit(value_str, ',')) {
        if (alpn.empty() || alpn.size() > 255) {
          return std::nullopt;
        }
        wire_val.push_back(static_cast<char>(alpn.size()));
        wire_val.append(alpn.data(), alpn.size());
      }
      if (wire_val.empty()) {
        return std::nullopt;
      }
    } else if (key_str == "no-default-alpn") {
      key = kSvcParamKeyNoDefaultAlpn;
      if (!value_str.empty()) {
        return std::nullopt;
      }
    } else if (key_str == "port") {
      key = kSvcParamKeyPort;
      uint16_t port = 0;
      if (!absl::SimpleAtoi(value_str, &port)) {
        return std::nullopt;
      }
      char buf[2];
      QuicDataWriter writer(2, buf);
      writer.WriteUInt16(port);
      wire_val = std::string(buf, 2);
    } else if (key_str == "ipv4hint") {
      key = kSvcParamKeyIpv4Hint;
      for (absl::string_view ip_str : absl::StrSplit(value_str, ',')) {
        QuicIpAddress ip;
        if (!ip.FromString(std::string(ip_str)) || !ip.IsIPv4()) {
          return std::nullopt;
        }
        wire_val.append(ip.ToPackedString());
      }
      if (wire_val.empty()) {
        return std::nullopt;
      }
    } else if (key_str == "ipv6hint") {
      key = kSvcParamKeyIpv6Hint;
      for (absl::string_view ip_str : absl::StrSplit(value_str, ',')) {
        QuicIpAddress ip;
        if (!ip.FromString(std::string(ip_str)) || !ip.IsIPv6()) {
          return std::nullopt;
        }
        wire_val.append(ip.ToPackedString());
      }
      if (wire_val.empty()) {
        return std::nullopt;
      }
    } else if (key_str == "dohpath") {
      key = kSvcParamKeyDohPath;
      if (value_str.empty()) {
        return std::nullopt;
      }
      wire_val = std::string(value_str);
    } else if (key_str == "ohttp") {
      key = kSvcParamKeyOhttp;
      if (!value_str.empty()) {
        return std::nullopt;
      }
    } else if (absl::StartsWith(key_str, "key")) {
      uint32_t key_num;
      if (!absl::SimpleAtoi(key_str.substr(3), &key_num) || key_num > 65535) {
        return std::nullopt;
      }
      key = static_cast<uint16_t>(key_num);
      wire_val = std::string(value_str);
    } else {
      return std::nullopt;  // Unknown key string
    }

    if (!params.try_emplace(key, std::move(wire_val)).second) {
      return std::nullopt;  // Duplicate key not allowed per Section 2.2 of RFC
                            // 9460.
    }
  }

  size_t total_len = 0;
  for (const auto& [key, val] : params) {
    if (val.size() > 65535) {
      return std::nullopt;
    }
    total_len += 4 + val.size();  // 2 bytes key + 2 bytes len + value bytes
  }
  std::string wire(total_len, '\0');
  QuicDataWriter writer(wire.size(), wire.data());
  for (const auto& [key, val] : params) {
    if (!writer.WriteUInt16(key) ||
        !writer.WriteUInt16(static_cast<uint16_t>(val.size())) ||
        !writer.WriteStringPiece(val)) {
      return std::nullopt;
    }
  }
  return wire;
}

std::optional<std::string> DecodeSvcParams(absl::string_view wire_format) {
  if (wire_format.empty()) {
    return "";
  }
  QuicDataReader reader(wire_format);
  std::vector<std::string> param_strings;
  uint16_t last_key = 0;
  bool first = true;
  while (!reader.IsDoneReading()) {
    uint16_t key = 0;
    uint16_t len = 0;
    if (!reader.ReadUInt16(&key) || !reader.ReadUInt16(&len)) {
      return std::nullopt;
    }
    absl::string_view val;
    if (!reader.ReadStringPiece(&val, len)) {
      return std::nullopt;
    }
    if (!first && key <= last_key) {
      return std::nullopt;  // Keys must be strictly increasing per Section 2.2
                            // of RFC 9460.
    }
    first = false;
    last_key = key;

    if (key == kSvcParamKeyAlpn) {
      std::vector<absl::string_view> alpns;
      QuicDataReader alpn_reader(val);
      while (!alpn_reader.IsDoneReading()) {
        uint8_t alpn_len = 0;
        absl::string_view alpn;
        if (!alpn_reader.ReadUInt8(&alpn_len) || alpn_len == 0 ||
            !alpn_reader.ReadStringPiece(&alpn, alpn_len)) {
          return std::nullopt;
        }
        alpns.push_back(alpn);
      }
      if (alpns.empty()) {
        return std::nullopt;
      }
      param_strings.push_back(absl::StrCat("alpn=", absl::StrJoin(alpns, ",")));
    } else if (key == kSvcParamKeyNoDefaultAlpn) {
      if (!val.empty()) {
        return std::nullopt;
      }
      param_strings.push_back("no-default-alpn");
    } else if (key == kSvcParamKeyPort) {
      if (val.size() != 2) {
        return std::nullopt;
      }
      QuicDataReader port_reader(val);
      uint16_t port = 0;
      if (!port_reader.ReadUInt16(&port)) {
        return std::nullopt;
      }
      param_strings.push_back(absl::StrCat("port=", port));
    } else if (key == kSvcParamKeyIpv4Hint) {
      if (val.empty() || val.size() % QuicIpAddress::kIPv4AddressSize != 0) {
        return std::nullopt;
      }
      std::vector<std::string> ips;
      QuicDataReader ip_reader(val);
      while (!ip_reader.IsDoneReading()) {
        absl::string_view ip_bytes;
        if (!ip_reader.ReadStringPiece(&ip_bytes,
                                       QuicIpAddress::kIPv4AddressSize)) {
          return std::nullopt;
        }
        QuicIpAddress ip;
        if (!ip.FromPackedString(ip_bytes.data(),
                                 QuicIpAddress::kIPv4AddressSize)) {
          return std::nullopt;
        }
        ips.push_back(ip.ToString());
      }
      param_strings.push_back(
          absl::StrCat("ipv4hint=", absl::StrJoin(ips, ",")));
    } else if (key == kSvcParamKeyIpv6Hint) {
      if (val.empty() || val.size() % QuicIpAddress::kIPv6AddressSize != 0) {
        return std::nullopt;
      }
      std::vector<std::string> ips;
      QuicDataReader ip_reader(val);
      while (!ip_reader.IsDoneReading()) {
        absl::string_view ip_bytes;
        if (!ip_reader.ReadStringPiece(&ip_bytes,
                                       QuicIpAddress::kIPv6AddressSize)) {
          return std::nullopt;
        }
        QuicIpAddress ip;
        if (!ip.FromPackedString(ip_bytes.data(),
                                 QuicIpAddress::kIPv6AddressSize)) {
          return std::nullopt;
        }
        ips.push_back(ip.ToString());
      }
      param_strings.push_back(
          absl::StrCat("ipv6hint=", absl::StrJoin(ips, ",")));
    } else if (key == kSvcParamKeyDohPath) {
      if (val.empty()) {
        return std::nullopt;
      }
      param_strings.push_back(absl::StrCat("dohpath=", val));
    } else if (key == kSvcParamKeyOhttp) {
      if (!val.empty()) {
        return std::nullopt;
      }
      param_strings.push_back("ohttp");
    } else {
      param_strings.push_back(absl::StrCat("key", key, "=", val));
    }
  }
  return absl::StrJoin(param_strings, " ");
}

std::string DnsNameserver::ToString() const {
  std::string rv = absl::StrCat("(priority:", service_priority);
  for (const QuicIpAddress& ip : ipv4_addresses) {
    absl::StrAppend(&rv, ",ipv4:", ip.ToString());
  }
  for (const QuicIpAddress& ip : ipv6_addresses) {
    absl::StrAppend(&rv, ",ipv6:", ip.ToString());
  }
  if (!authentication_domain_name.domain_name.empty()) {
    absl::StrAppend(&rv,
                    ",auth_domain:", authentication_domain_name.domain_name);
  }
  if (!service_parameters.empty()) {
    std::optional<std::string> decoded = DecodeSvcParams(service_parameters);
    absl::StrAppend(&rv, ",service_params:",
                    decoded.has_value() ? absl::CEscape(*decoded)
                                        : absl::CEscape(service_parameters));
  }
  absl::StrAppend(&rv, ")");
  return rv;
}

std::string DnsConfiguration::ToString() const {
  std::string rv = "{nameservers:[";
  for (const DnsNameserver& ns : nameservers) {
    absl::StrAppend(&rv, ns.ToString());
  }
  absl::StrAppend(&rv, "],internal_domains:[");
  bool first = true;
  for (const DnsDomain& dom : internal_domains) {
    if (!first) {
      absl::StrAppend(&rv, ",");
    }
    first = false;
    absl::StrAppend(&rv, dom.domain_name.empty() ? "/root" : dom.domain_name);
  }
  absl::StrAppend(&rv, "],search_domains:[");
  first = true;
  for (const DnsDomain& dom : search_domains) {
    if (!first) {
      absl::StrAppend(&rv, ",");
    }
    first = false;
    absl::StrAppend(&rv, dom.domain_name);
  }
  absl::StrAppend(&rv, "]}");
  return rv;
}

std::string DnsAssignCapsule::ToString() const {
  std::string rv = "DNS_ASSIGN[";
  for (const DnsConfiguration& config : dns_configurations) {
    absl::StrAppend(&rv, config.ToString());
  }
  absl::StrAppend(&rv, "]");
  return rv;
}

std::string Pref64Prefix::ToString() const {
  return absl::StrCat("(", prefix.ToString(), "/",
                      static_cast<int>(prefix_length), ")");
}

std::string Pref64Capsule::ToString() const {
  std::string rv = "PREF64[";
  for (const Pref64Prefix& prefix : nat64_prefixes) {
    absl::StrAppend(&rv, prefix.ToString());
  }
  absl::StrAppend(&rv, "]");
  return rv;
}

void WriteDnsAssignCapsule(QuicSpdyStream* stream,
                           const DnsAssignCapsule& capsule, bool fin) {
  QUICHE_CHECK(stream != nullptr);
  std::string payload = SerializeDnsAssignCapsulePayload(capsule);
  stream->WriteCapsule(quiche::Capsule::Unknown(kDnsAssignCapsuleType, payload),
                       fin);
}

void WritePref64Capsule(QuicSpdyStream* stream, const Pref64Capsule& capsule,
                        bool fin) {
  QUICHE_CHECK(stream != nullptr);
  std::string payload = SerializePref64CapsulePayload(capsule);
  stream->WriteCapsule(quiche::Capsule::Unknown(kPref64CapsuleType, payload),
                       fin);
}

}  // namespace quic
