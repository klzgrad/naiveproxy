// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_MOJO_HOST_STRUCT_TRAITS_H_
#define NET_DNS_MOJO_HOST_STRUCT_TRAITS_H_

#include "base/strings/string_piece.h"
#include "mojo/public/cpp/bindings/enum_traits.h"
#include "mojo/public/cpp/bindings/struct_traits.h"
#include "net/dns/host_resolver.h"
#include "net/interfaces/host_resolver_service.mojom.h"

namespace mojo {

template <>
struct EnumTraits<net::interfaces::AddressFamily, net::AddressFamily> {
  static net::interfaces::AddressFamily ToMojom(
      net::AddressFamily address_family);
  static bool FromMojom(net::interfaces::AddressFamily address_family,
                        net::AddressFamily* out);
};

template <>
struct StructTraits<net::interfaces::HostResolverRequestInfoDataView,
                    std::unique_ptr<net::HostResolver::RequestInfo>> {
  static base::StringPiece host(
      const std::unique_ptr<net::HostResolver::RequestInfo>& obj) {
    return obj->hostname();
  }

  static uint16_t port(
      const std::unique_ptr<net::HostResolver::RequestInfo>& obj) {
    return obj->port();
  }

  static net::AddressFamily address_family(
      const std::unique_ptr<net::HostResolver::RequestInfo>& obj) {
    return obj->address_family();
  }

  static bool is_my_ip_address(
      const std::unique_ptr<net::HostResolver::RequestInfo>& obj) {
    return obj->is_my_ip_address();
  }

  static bool Read(net::interfaces::HostResolverRequestInfoDataView obj,
                   std::unique_ptr<net::HostResolver::RequestInfo>* output);
};

template <>
struct StructTraits<net::interfaces::AddressListDataView, net::AddressList> {
  static std::vector<net::IPEndPoint> addresses(const net::AddressList& obj) {
    return obj.endpoints();
  }

  static bool Read(net::interfaces::AddressListDataView data,
                   net::AddressList* out);
};

}  // namespace mojo

#endif  // NET_DNS_MOJO_HOST_STRUCT_TRAITS_H_
