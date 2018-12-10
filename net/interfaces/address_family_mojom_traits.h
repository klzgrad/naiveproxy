// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_INTERFACES_ADDRESS_FAMILY_MOJOM_TRAITS_H_
#define NET_INTERFACES_ADDRESS_FAMILY_MOJOM_TRAITS_H_

#include "mojo/public/cpp/bindings/enum_traits.h"
#include "net/interfaces/address_family.mojom.h"

namespace mojo {

template <>
struct EnumTraits<net::interfaces::AddressFamily, net::AddressFamily> {
  static net::interfaces::AddressFamily ToMojom(
      net::AddressFamily address_family);
  static bool FromMojom(net::interfaces::AddressFamily address_family,
                        net::AddressFamily* out);
};

}  // namespace mojo

#endif  // NET_INTERFACES_ADDRESS_FAMILY_MOJOM_TRAITS_H_
