// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_IP_ADDRESS_FAMILY_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_IP_ADDRESS_FAMILY_H_

namespace quic {

// IP address family type used in QUIC. This hides platform dependant IP address
// family types.
enum class IpAddressFamily {
  IP_V4,
  IP_V6,
  IP_UNSPEC,
};

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_IP_ADDRESS_FAMILY_H_
