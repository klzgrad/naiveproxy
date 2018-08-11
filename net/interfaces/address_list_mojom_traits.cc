// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/interfaces/address_list_mojom_traits.h"

#include "net/base/address_list.h"
#include "net/interfaces/ip_endpoint_struct_traits.h"

namespace mojo {

// static
bool StructTraits<net::interfaces::AddressListDataView, net::AddressList>::Read(
    net::interfaces::AddressListDataView data,
    net::AddressList* out) {
  return data.ReadAddresses(&out->endpoints());
}

}  // namespace mojo
