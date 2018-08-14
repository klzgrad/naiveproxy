// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_INTERFACES_ADDRESS_LIST_MOJOM_TRAITS_H_
#define NET_INTERFACES_ADDRESS_LIST_MOJOM_TRAITS_H_

#include <vector>

#include "mojo/public/cpp/bindings/struct_traits.h"
#include "net/interfaces/address_list.mojom.h"

namespace mojo {

template <>
struct StructTraits<net::interfaces::AddressListDataView, net::AddressList> {
  static const std::vector<net::IPEndPoint>& addresses(
      const net::AddressList& obj) {
    return obj.endpoints();
  }

  static bool Read(net::interfaces::AddressListDataView data,
                   net::AddressList* out);
};

}  // namespace mojo

#endif  // NET_INTERFACES_ADDRESS_LIST_MOJOM_TRAITS_H_
