// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_utils.h"

#if defined(__linux__)
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#endif  // defined(__linux__)

#include "absl/cleanup/cleanup.h"

namespace quic {

ParsedQuicVersionVector MasqueSupportedVersions() {
  ParsedQuicVersionVector versions;
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    // Use all versions that support IETF QUIC except QUICv2.
    if (version.UsesHttp3() && !version.AlpnDeferToRFCv1()) {
      QuicEnableVersion(version);
      versions.push_back(version);
    }
  }
  QUICHE_CHECK(!versions.empty());
  return versions;
}

QuicConfig MasqueEncapsulatedConfig() {
  QuicConfig config;
  config.SetMaxPacketSizeToSend(kMasqueMaxEncapsulatedPacketSize);
  return config;
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
  std::move(tap_fd_closer).Cancel();
  return tap_fd;
}
#else
int CreateTapInterface() {
  // Unsupported.
  return -1;
}
#endif  // defined(__linux__)

}  // namespace quic
