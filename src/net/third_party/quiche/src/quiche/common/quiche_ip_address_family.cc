// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/quiche_ip_address_family.h"

#include "quiche/common/platform/api/quiche_bug_tracker.h"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif  // defined(_WIN32)

namespace quiche {

int ToPlatformAddressFamily(IpAddressFamily family) {
  switch (family) {
    case IpAddressFamily::IP_V4:
      return AF_INET;
    case IpAddressFamily::IP_V6:
      return AF_INET6;
    case IpAddressFamily::IP_UNSPEC:
      return AF_UNSPEC;
    default:
      QUICHE_BUG(quic_bug_10126_1)
          << "Invalid IpAddressFamily " << static_cast<int32_t>(family);
      return AF_UNSPEC;
  }
}

IpAddressFamily FromPlatformAddressFamily(int family) {
  switch (family) {
    case AF_INET:
      return IpAddressFamily::IP_V4;
    case AF_INET6:
      return IpAddressFamily::IP_V6;
    case AF_UNSPEC:
      return IpAddressFamily::IP_UNSPEC;
    default:
      QUICHE_BUG(quic_FromPlatformAddressFamily_unrecognized_family)
          << "Invalid platform address family int " << family;
      return IpAddressFamily::IP_UNSPEC;
  }
}

}  // namespace quiche
