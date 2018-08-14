// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/interfaces/ip_address_struct_traits.h"

namespace mojo {
// static
bool StructTraits<net::interfaces::IPAddressDataView, net::IPAddress>::Read(
    net::interfaces::IPAddressDataView data,
    net::IPAddress* out) {
  std::vector<uint8_t> bytes;
  if (!data.ReadAddressBytes(&bytes))
    return false;

  if (bytes.size() && bytes.size() != net::IPAddress::kIPv4AddressSize &&
      bytes.size() != net::IPAddress::kIPv6AddressSize) {
    return false;
  }

  *out = net::IPAddress(bytes.data(), bytes.size());
  return true;
}

}  // namespace mojo
