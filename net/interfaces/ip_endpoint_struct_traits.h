// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_INTERFACES_IP_ENDPOINT_STRUCT_TRAITS_H_
#define NET_INTERFACES_IP_ENDPOINT_STRUCT_TRAITS_H_

#include "mojo/public/cpp/bindings/struct_traits.h"
#include "net/base/ip_endpoint.h"
#include "net/interfaces/ip_endpoint.mojom.h"

namespace mojo {
template <>
struct StructTraits<net::interfaces::IPEndPointDataView, net::IPEndPoint> {
  static const net::IPAddress& address(const net::IPEndPoint& obj) {
    return obj.address();
  }
  static uint16_t port(const net::IPEndPoint& obj) { return obj.port(); }

  static bool Read(net::interfaces::IPEndPointDataView obj,
                   net::IPEndPoint* out);
};

}  // namespace mojo

#endif  // NET_INTERFACES_IP_ENDPOINT_STRUCT_TRAITS_H_
