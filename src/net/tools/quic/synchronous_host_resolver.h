// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A simple class for resolving hostname synchronously.

#ifndef NET_TOOLS_QUIC_SYNCHRONOUS_HOST_RESOLVER_H_
#define NET_TOOLS_QUIC_SYNCHRONOUS_HOST_RESOLVER_H_

#include <string>

#include "net/base/address_list.h"
#include "net/dns/host_resolver.h"

namespace net {

class SynchronousHostResolver {
 public:
  static int Resolve(const std::string& host, AddressList* addresses);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_SYNCHRONOUS_HOST_RESOLVER_H_
