// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/qbone_packet_processor_test_tools.h"

#include <netinet/ip6.h>

#include <string>

namespace quic {

std::string PrependIPv6HeaderForTest(const std::string& body, int hops) {
  ip6_hdr header;
  memset(&header, 0, sizeof(header));

  header.ip6_vfc = 6 << 4;
  header.ip6_plen = htons(body.size());
  header.ip6_nxt = IPPROTO_UDP;
  header.ip6_hops = hops;
  header.ip6_src = in6addr_loopback;
  header.ip6_dst = in6addr_loopback;

  std::string packet(sizeof(header) + body.size(), '\0');
  memcpy(&packet[0], &header, sizeof(header));
  memcpy(&packet[sizeof(header)], body.data(), body.size());
  return packet;
}

bool DecrementIPv6HopLimit(std::string& packet) {
  if (packet.size() < sizeof(ip6_hdr)) {
    return false;
  }
  ip6_hdr* header = reinterpret_cast<ip6_hdr*>(&packet[0]);
  if (header->ip6_vfc >> 4 != 6 || header->ip6_hops == 0) {
    return false;
  }
  header->ip6_hops--;
  return true;
}

}  // namespace quic
