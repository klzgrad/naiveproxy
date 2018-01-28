// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/interfaces/ip_endpoint_struct_traits.h"

#include "net/interfaces/ip_address_struct_traits.h"

namespace mojo {

// static
bool StructTraits<net::interfaces::IPEndPointDataView, net::IPEndPoint>::Read(
    net::interfaces::IPEndPointDataView data,
    net::IPEndPoint* out) {
  net::IPAddress address;
  if (!data.ReadAddress(&address))
    return false;

  if (!address.IsValid())
    return false;

  *out = net::IPEndPoint(address, data.port());
  return true;
}

}  // namespace mojo
