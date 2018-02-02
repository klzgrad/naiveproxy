// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/mojo_host_struct_traits.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "net/base/address_list.h"
#include "net/interfaces/ip_endpoint_struct_traits.h"

namespace mojo {

// static
bool EnumTraits<net::interfaces::AddressFamily, net::AddressFamily>::FromMojom(
    net::interfaces::AddressFamily address_family,
    net::AddressFamily* out) {
  using net::interfaces::AddressFamily;
  switch (address_family) {
    case AddressFamily::UNSPECIFIED:
      *out = net::ADDRESS_FAMILY_UNSPECIFIED;
      return true;
    case AddressFamily::IPV4:
      *out = net::ADDRESS_FAMILY_IPV4;
      return true;
    case AddressFamily::IPV6:
      *out = net::ADDRESS_FAMILY_IPV6;
      return true;
  }
  return false;
}

// static
net::interfaces::AddressFamily
EnumTraits<net::interfaces::AddressFamily, net::AddressFamily>::ToMojom(
    net::AddressFamily address_family) {
  using net::interfaces::AddressFamily;
  switch (address_family) {
    case net::ADDRESS_FAMILY_UNSPECIFIED:
      return AddressFamily::UNSPECIFIED;
    case net::ADDRESS_FAMILY_IPV4:
      return AddressFamily::IPV4;
    case net::ADDRESS_FAMILY_IPV6:
      return AddressFamily::IPV6;
  }
  NOTREACHED();
  return AddressFamily::UNSPECIFIED;
}

// static
bool StructTraits<net::interfaces::HostResolverRequestInfoDataView,
                  std::unique_ptr<net::HostResolver::RequestInfo>>::
    Read(net::interfaces::HostResolverRequestInfoDataView data,
         std::unique_ptr<net::HostResolver::RequestInfo>* out) {
  base::StringPiece host;
  if (!data.ReadHost(&host))
    return false;

  net::AddressFamily address_family;
  if (!data.ReadAddressFamily(&address_family))
    return false;

  *out = std::make_unique<net::HostResolver::RequestInfo>(
      net::HostPortPair(host.as_string(), data.port()));
  net::HostResolver::RequestInfo& request = **out;
  request.set_address_family(address_family);
  request.set_is_my_ip_address(data.is_my_ip_address());
  return true;
}

// static
bool StructTraits<net::interfaces::AddressListDataView, net::AddressList>::Read(
    net::interfaces::AddressListDataView data,
    net::AddressList* out) {
  return data.ReadAddresses(&out->endpoints());
}

}  // namespace mojo
